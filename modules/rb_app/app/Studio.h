#pragma once

namespace rb
{

// The app's controller: owns the engine, the open document, the set library, settings and the
// capture buffer, and offers the actions the UI performs (transport, launching, capture).
// Message thread only.
class Studio : private juce::Timer,
               private Document::Listener
{
public:
    explicit Studio (const juce::File& dataRoot);
    ~Studio() override;

    AudioEngine& getEngine() { return engine; }
    Document& getDocument() { return engine.getDocument(); }
    const Set& getSet() { return engine.getDocument().get(); }
    SessionProcessor& getSession() { return engine.getSession(); }
    SetLibrary& getLibrary() { return library; }
    AppSettings& getSettings() { return settings; }
    CaptureBuffer& getCapture() { return capture; }
    MidiInputs& getMidiInputs() { return *midiInputs; }

    // The track that external MIDI plays (the one open in the instrument view, else the
    // first track). Safe to read from the MIDI thread.
    int getActiveTrackId() const { return activeTrackId.load(); }
    void setActiveTrackId (int trackId);

    //==============================================================================
    // Sets
    void openSet (const SetInfo&);
    void closeSet();
    juce::String getOpenSetId() const { return openSetId; }
    bool isSetOpen() const { return openSetId.isNotEmpty(); }

    // Library preview playback (LIB-3): opens the set silently in the background engine.
    void previewSet (const SetInfo&);
    void stopPreview();
    juce::String getPreviewSetId() const { return previewSetId; }

    //==============================================================================
    // Transport and launching
    void play();
    void stop();
    void togglePlay();
    bool isPlaying() const { return engine.getSession().isRunning(); }

    void launchClip (int trackId, int slot);
    void stopClip (int trackId);
    void launchScene (int scene);
    void switchClipSeamless (int trackId, int slot);

    int getPlayingSlot (int trackId) const;
    int getQueuedSlot (int trackId) const;
    double getClipPosition (int trackId) const;

    //==============================================================================
    // Live input (pads, keys, MIDI)
    void noteOn (int trackId, int pitch, float velocity);
    void noteOff (int trackId, int pitch);
    void allNotesOff (int trackId);
    bool isNoteSounding (int trackId, int pitch) const;

    //==============================================================================
    // Capture (CAP)
    bool canCaptureFree (int trackId) const;     // free notes waiting
    bool hasPendingNotes (int trackId, int slot) const;
    std::vector<Note> getPendingNotes (int trackId, int slot);

    // CAP-3/CAP-4: creates a clip from free notes; returns its slot or -1.
    int captureFree (int trackId, int preferredSlot = -1);
    // CAP-2/CAP-5: commits pending notes (and pending automation) into the clip.
    void addPending (int trackId, int slot);
    void clearCapture (int trackId);
    // CAP-2: a new empty clip of `bars` that starts looping.
    void createFixedLengthClip (int trackId, int slot, int bars);

    //==============================================================================
    // Parameter automation recording (DEV-30..32)
    struct PendingAutomation
    {
        ParamTarget target;
        std::vector<AutomationPoint> points;
    };
    void recordParameterChange (int trackId, const ParamTarget&, float value);
    const std::vector<PendingAutomation>& getPendingAutomation (int trackId) const;

    //==============================================================================
    // Helpers for the UI
    juce::File getUserSamplesFolder() const { return library.getUserSamplesFolder(); }
    juce::File getUserPresetsFolder() const { return library.getUserPresetsFolder(); }

    // Tempo set by tapping (SET-1).
    void tapTempo();

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void studioChanged() {}        // document edits, set opened/closed
        virtual void captureChanged() {}       // capture buffer contents changed
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

private:
    void timerCallback() override;
    void documentChanged() override;
    void post (Command::Type, int trackId = 0, int slot = 0, int pitch = 0, float velocity = 0.0f);

    AudioEngine engine;
    SetLibrary library;
    AppSettings settings;
    CaptureBuffer capture;
    std::unique_ptr<MidiInputs> midiInputs;
    std::atomic<int> activeTrackId { 0 };
    int preferredActiveTrackId = 0;

    juce::String openSetId, previewSetId;
    std::map<int, std::vector<PendingAutomation>> pendingAutomation;
    std::vector<double> tapTimes;

    juce::ListenerList<Listener> listeners;
    std::array<CaptureEvent, 512> eventBuffer;
};

} // namespace rb
