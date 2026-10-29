#pragma once

namespace rb
{

// The real-time core: transport, clip and scene launching, live notes, repeat, metronome,
// per-track device chains and the mixer. It is independent of any audio device, so the
// same code runs in the app, in offline export and in tests.
//
// Threads:
//  - message thread: publish(), post(), readCaptureEvents(), collectGarbage(), recording control
//  - audio thread:   process()
//  - prepare()/releaseResources() run while process() is not being called.
class SessionProcessor
{
public:
    static constexpr int noQueue = -2;   // TrackState::queuedSlot: nothing queued
    static constexpr int queuedStop = -1; // TrackState::queuedSlot: stop at next downbeat

    SessionProcessor();
    ~SessionProcessor();

    //==============================================================================
    void prepare (double sampleRate, int maxBlockSize);
    void releaseResources();
    double getSampleRate() const { return sampleRate; }
    int getMaxBlockSize() const { return maxBlockSize; }

    // Held while preparing processors, so a device restart and a snapshot publish can't
    // prepare processors at different sample rates.
    std::recursive_mutex& getPrepareMutex() { return prepareMutex; }

    //==============================================================================
    // Message thread.
    void publish (SnapshotPtr snapshot);
    SnapshotPtr getPublished() const { return published; }
    void collectGarbage();

    bool post (const Command& command);
    int readCaptureEvents (CaptureEvent* destination, int maxEvents);

    //==============================================================================
    // Audio thread. Renders numSamples of stereo output (the buffer is overwritten).
    // input (optional) is the device input, used for sample recording and monitoring.
    void process (juce::AudioBuffer<float>& output, int numSamples,
                  const juce::AudioBuffer<float>* input = nullptr);

    //==============================================================================
    // Read-only state for the UI (lock-free, any thread).
    struct TrackState
    {
        std::atomic<int> trackId { 0 };
        std::atomic<int> playingSlot { -1 };
        std::atomic<int> queuedSlot { noQueue };
        std::atomic<double> clipBeat { 0.0 };
        std::atomic<juce::uint64> soundingLow { 0 }, soundingHigh { 0 }; // sounding MIDI notes
        std::atomic<float> level { 0.0f };

        bool isNoteSounding (int note) const
        {
            if (note < 0 || note > 127) return false;
            return note < 64 ? ((soundingLow.load() >> note) & 1) != 0
                             : ((soundingHigh.load() >> (note - 64)) & 1) != 0;
        }
    };

    bool isRunning() const { return running.load(); }
    double getElapsedSeconds() const { return elapsedForUi.load(); } // engine time, as in CaptureEvent
    double getSongBeat() const { return songBeatForUi.load(); }
    float getMasterLevel() const { return masterLevel.load(); }

    // State of the track with this id, or nullptr if it is not in the active snapshot.
    const TrackState* findTrackState (int trackId) const;

    //==============================================================================
    // Sample recording (SMP-3, SMP-5). Message thread: start/stop. The audio thread writes
    // into a buffer allocated by startRecording.
    enum class RecordSource { input, output };
    void startRecording (RecordSource source, double maxSeconds);
    bool isRecording() const { return recordActive.load(); }
    // Stops recording and returns what was recorded (stereo, at the engine sample rate).
    juce::AudioBuffer<float> stopRecording();
    float getRecordLevel() const { return recordLevel.load(); }

    // Monitoring of the device input while recording (SMP-8).
    void setMonitorInput (bool shouldMonitor) { monitorInput.store (shouldMonitor); }

    //==============================================================================
    // Offline rendering support: makes process() acknowledge snapshots synchronously.
    void setOffline (bool isOffline) { offline = isOffline; }

private:
    struct ScheduledOff
    {
        int pitch;
        double clockBeat; // when to send the note-off, on the free-running clock
        bool fromClip;
    };

    struct PlayState
    {
        int trackId = 0;
        int playingSlot = -1;
        double clipStartBeat = 0.0; // song beat at which the clip's loop start played
        int queuedSlot = noQueue;
        double queuedBeat = 0.0;
        int armedSlot = -1; // resumes on play after a stop

        std::array<ScheduledOff, 256> offs;
        int numOffs = 0;

        std::array<bool, 128> liveHeld {};
        std::array<float, 128> liveVelocity {};
        std::array<juce::uint8, 128> soundingCount {};

        std::array<NoteEvent, 1024> events;
        int numEvents = 0;

        juce::LinearSmoothedValue<float> gain;
        float levelHold = 0.0f;

        void clearEvents() { numEvents = 0; }
    };

    void applySnapshot (const EngineSnapshot* next);
    void handleCommand (const Command&);
    void startTransport();
    void stopTransport();
    double nextDownbeat (double songBeat) const;

    void renderTrack (int index, const TrackSnapshot& track, double clock0, double beatsPerSample,
                      int numSamples);
    void addEvent (PlayState&, int offset, int pitch, float velocity, float pitchOffset);
    void emitClipNotes (PlayState&, const Clip& clip, double song0, double song1, double clock0,
                        double beatsPerSample, int numSamples);
    void endClipNotes (PlayState&, int offset);
    void pushCapture (const PlayState&, int pitch, float velocity, float pitchOffset, double clockBeat);
    double clipPositionAt (const PlayState&, const Clip&, double songBeat) const;
    void applyAutomation (const TrackSnapshot&, const PlayState&, double songBeat);
    void renderMetronome (juce::AudioBuffer<float>& out, double song0, double beatsPerSample, int numSamples);

    double sampleRate = 48000.0;
    int maxBlockSize = 512;
    std::recursive_mutex prepareMutex;

    // Snapshot hand-over.
    SnapshotPtr published;
    std::vector<SnapshotPtr> retired;
    std::atomic<const EngineSnapshot*> pending { nullptr };
    const EngineSnapshot* active = nullptr;
    std::atomic<juce::uint64> acknowledged { 0 };
    std::atomic<bool> audioActive { false };
    bool offline = false;

    // FIFOs.
    juce::AbstractFifo commandFifo { 1024 };
    std::array<Command, 1024> commands;
    juce::AbstractFifo captureFifo { 2048 };
    std::array<CaptureEvent, 2048> captures;

    // Transport.
    std::atomic<bool> running { false };
    bool wasRunning = false;
    double songBeat = 0.0;
    double clockBeat = 0.0;      // free-running, always advances (repeat when stopped)
    double songOrigin = 0.0;     // clockBeat at song beat 0
    double elapsedSeconds = 0.0;
    std::atomic<double> songBeatForUi { 0.0 };
    std::atomic<double> elapsedForUi { 0.0 };
    double tempo = 120.0;

    std::array<PlayState, maxTracks> states;
    std::array<TrackState, maxTracks> trackStates;

    juce::AudioBuffer<float> dryBuffer, sendBuffer;
    juce::AudioBuffer<float>* mixTarget = nullptr; // output view of the chunk being rendered
    std::array<float, maxDeviceParams> scratchValues {};

    // Metronome.
    float clickPhase = 0.0f, clickIncrement = 0.0f, clickEnvelope = 0.0f, clickDecay = 0.0f, clickGain = 0.0f;

    std::atomic<float> masterLevel { 0.0f };
    float masterLevelHold = 0.0f;

    // Recording.
    juce::AudioBuffer<float> recordBuffer;
    std::atomic<bool> recordActive { false };
    std::atomic<int> recordPosition { 0 };
    std::atomic<bool> recordWriting { false };
    double clockAtChunkStart = 0.0;

    void processChunk (juce::AudioBuffer<float>& output, int start, int numSamples,
                       const juce::AudioBuffer<float>* input);
    std::atomic<float> recordLevel { 0.0f };
    RecordSource recordSource = RecordSource::input;
    std::atomic<bool> monitorInput { false };

    JUCE_DECLARE_NON_COPYABLE (SessionProcessor)
};

} // namespace rb
