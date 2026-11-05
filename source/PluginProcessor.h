#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Constants.h"
#include "Sequencer/StepSequencer.h"
#include "Mixer/Mixer.h"
#include "Engine/SoundSources/Metronome.h"
#include "Engine/Devices.h"
#include "Engine/Processors/TrackEffects.h"
#include "Sequencer/TapTempo.h"
#include "Sequencer/Recorder.h"
#include "Project/SetSerializer.h"
#include <array>

class AndrotoneAudioProcessor : public juce::AudioProcessor {
public:
    AndrotoneAudioProcessor();
    ~AndrotoneAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; } // reverb send tail

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    void setTempo(double bpm) { sequencer.setTempo(bpm); }
    void setMetronome(bool enabled) { sequencer.setMetronome(enabled); }
    void setFollowActionsEnabled(bool enabled) { sequencer.setFollowActionsEnabled(enabled); }
    bool areFollowActionsEnabled() const { return sequencer.areFollowActionsEnabled(); }
    bool isMetronomeOn() const { return sequencer.isMetronomeOn(); }
    void setMetronomeVolume(float volume) { metronome.setVolume(volume); }
    float getMetronomeVolume() const { return metronome.getVolume(); }
    void setSwing(double timing, double velocity) { sequencer.setSwing(timing, velocity); }
    double getSwingTiming() const { return sequencer.getSwingTiming(); }
    double getSwingVelocity() const { return sequencer.getSwingVelocity(); }

    // ---- recording (UI thread) ----
    void toggleSessionRecord() { recorder.toggleSessionRecord(getCurrentScene()); }
    void recordIntoSlot(int t, int slot) { recorder.recordIntoSlot(t, slot); }
    void stopRecording(int t) { sequencer.requestStopRecord(t); }
    bool isRecording() const {
        for (int t = 0; t < NUM_TRACKS; t++) {
            if (sequencer.isRecordingOrPending(t)) {
                return true;
            }
        }
        return false;
    }
    int getRecordingSlot(int t) const { return sequencer.getRecordingSlot(t); }
    void setRecordQuantization(double beats) { recorder.setQuantization(beats); }
    double getRecordQuantization() const { return recorder.getQuantization(); }
    void setCountInBars(int bars) { sequencer.setCountInBars(bars); }
    int getCountInBars() const { return sequencer.getCountInBars(); }

    // Tap tempo (UI thread): returns the new tempo, or 0 on the first tap of a sequence.
    double tapTempo() {
        const double bpm = tapTempoState.tap(juce::Time::getMillisecondCounterHiRes() / 1000.0);
        if (bpm > 0.0) {
            setTempo(bpm);
        }
        return bpm;
    }
    void setVolume(float vol) { mixer.setMasterVolume(vol); }
    void setPlaying(bool shouldPlay) { sequencer.setPlaying(shouldPlay); }
    // ---- mixer (UI thread) ----
    void setTrackVolume(int t, float vol) { mixer.setTrackVolume(t, vol); }
    void setTrackPan(int t, float p) { mixer.setTrackPan(t, p); }
    void setTrackReverbSend(int t, float amount) { mixer.setTrackReverbSend(t, amount); }
    void setTrackDelaySend(int t, float amount) { mixer.setTrackDelaySend(t, amount); }
    void setTrackActive(int t, bool on) { mixer.setTrackActive(t, on); }
    void setReturnLevel(int r, float level) { mixer.setReturnLevel(r, level); }
    void setReturnPre(int r, bool pre) { mixer.setReturnPre(r, pre); }

    // Solo / arm are exclusive unless `multi` (e.g. a modifier) or the option is off.
    void setTrackSolo(int t, bool on, bool multi = false) {
        if (on && exclusiveSolo.load() && !multi) {
            for (int other = 0; other < NUM_TRACKS; other++) {
                mixer.setTrackSolo(other, false);
            }
        }
        mixer.setTrackSolo(t, on);
    }

    void setTrackArmed(int t, bool on, bool multi = false) {
        if (on && exclusiveArm.load() && !multi) {
            for (int other = 0; other < NUM_TRACKS; other++) {
                sequencer.setArmed(other, false);
            }
        }
        sequencer.setArmed(t, on);
    }

    void setExclusiveSolo(bool on) { exclusiveSolo.store(on); }
    bool isExclusiveSolo() const { return exclusiveSolo.load(); }
    void setExclusiveArm(bool on) { exclusiveArm.store(on); }
    bool isExclusiveArm() const { return exclusiveArm.load(); }

    float getTrackPan(int t) const { return mixer.getTrackPan(t); }
    bool isTrackActive(int t) const { return mixer.isTrackActive(t); }
    bool isTrackSoloed(int t) const { return mixer.isTrackSoloed(t); }
    bool isTrackArmed(int t) const { return sequencer.isArmed(t); }
    float getReturnLevel(int r) const { return mixer.getReturnLevel(r); }
    bool isReturnPre(int r) const { return mixer.isReturnPre(r); }
    float takeTrackPeak(int t) { return mixer.takeTrackPeak(t); }
    float takeMasterPeak() { return mixer.takeMasterPeak(); }
    void setSongMode(bool enabled) { sequencer.setSongMode(enabled); }
    void setQuantizationIndex(int index) { sequencer.setQuantizationIndex(index); }

    // Session launching (UI thread). Launches are queued until the next quantization boundary.
    void pressClip(int trackIndex, int clipIndex) { sequencer.pressClip(trackIndex, clipIndex); }
    void releaseClip(int trackIndex, int clipIndex) { sequencer.releaseClip(trackIndex, clipIndex); }
    void stopTrack(int trackIndex) { sequencer.stopTrack(trackIndex); }
    void launchScene(int sceneIndex) { sequencer.launchScene(sceneIndex); }
    void stopAllClips() { sequencer.stopAllClips(); }
    ClipSettings& getClipSettings(int trackIndex, int clipIndex) { return sequencer.getClipSettings(trackIndex, clipIndex); }

    bool isPlaying() const { return sequencer.isPlaying(); }
    bool isSongMode() const { return sequencer.isSongMode(); }
    double getTempo() const { return sequencer.getTempo(); }
    float getVolume() const { return mixer.getMasterVolume(); }
    float getTrackVolume(int trackIndex) const { return mixer.getTrackVolume(trackIndex); }
    float getTrackReverbSend(int trackIndex) const { return mixer.getTrackReverbSend(trackIndex); }
    float getTrackDelaySend(int trackIndex) const { return mixer.getTrackDelaySend(trackIndex); }
    int getQuantizationIndex() const { return sequencer.getQuantizationIndex(); }
    int getPlayingClip(int trackIndex) const { return sequencer.getPlayingClip(trackIndex); }
    int getQueuedClip(int trackIndex) const { return sequencer.getQueuedClip(trackIndex); }
    bool isTrackStopQueued(int trackIndex) const { return sequencer.isTrackStopQueued(trackIndex); }
    bool hasClip(int trackIndex, int clipIndex) const { return sequencer.hasClip(trackIndex, clipIndex); }
    ClipData::Ptr getClipData(int trackIndex, int clipIndex) const { return sequencer.getClipData(trackIndex, clipIndex); }
    void setClipData(int trackIndex, int clipIndex, ClipData::Ptr data) { sequencer.setClipData(trackIndex, clipIndex, data); }
    // An undoable change of a slot's content (one undo step). `before` is what the slot held when
    // the gesture started; the slot already shows `after` if the UI previewed it live.
    void performClipEdit(int trackIndex, int clipIndex, ClipData::Ptr before, ClipData::Ptr after,
                         const juce::String& name) {
        undoManager.beginNewTransaction(name);
        undoManager.perform(new ClipEditAction(sequencer, trackIndex, clipIndex, before, after));
    }

    juce::UndoManager& getUndoManager() { return undoManager; }

    // ---- devices (UI thread) ----
    void setInstrument(int t, InstrumentType type);
    InstrumentType getInstrument(int t) const { return devices[static_cast<size_t>(t)].instrument.load(); }
    DeviceParams& getInstrumentParams(int t) { return devices[static_cast<size_t>(t)].instrumentParams; }
    DeviceParams& getEffectParams(int t) { return devices[static_cast<size_t>(t)].effectParams; }
    double getGainReductionDb(int t) const { return trackEffects[static_cast<size_t>(t)].getGainReductionDb(); }

    // ---- sets (UI thread) ----
    juce::ValueTree createSetState();
    bool restoreSetState(const juce::ValueTree& set);
    bool saveSet(const juce::File& file);
    bool loadSet(const juce::File& file);
    void newSet();
    bool exportClipAsMidi(int t, int slot, const juce::File& file) {
        const auto data = getClipData(t, slot);
        return data != nullptr && SetSerializer::exportMidiClip(*data, getTempo(), file);
    }
    juce::File getCurrentSetFile() const { return currentSetFile; }
    // Bumped whenever a different set is loaded, so the editor can rebuild its views.
    int getSetVersion() const { return setVersion.load(); }

    // ---- arrangement (UI thread) ----
    ArrangementData::Ptr getArrangement() const { return sequencer.getArrangement(); }

    // One undoable arrangement change; `after` must be fresh and rendered.
    void performArrangementEdit(ArrangementData::Ptr before, ArrangementData::Ptr after, const juce::String& name) {
        undoManager.beginNewTransaction(name);
        undoManager.perform(new ArrangementEditAction(sequencer, before, after));
    }

    // Live preview during a drag (not undoable; the gesture commits with performArrangementEdit).
    void setArrangementPreview(ArrangementData::Ptr data) { sequencer.setArrangement(data); }

    // Puts a session clip on the arrangement at the insert marker (one pass long).
    void placeClipInArrangement(int t, int slot) {
        const auto content = getClipData(t, slot);
        if (content == nullptr) {
            return;
        }
        const auto before = getArrangement();
        auto after = before->copy();
        after->place(t, { getStartPosition(), content->getFirstPassLength(), content, 0.0, slot });
        after->render();
        performArrangementEdit(before, after, "Add clip to arrangement");
    }

    void setStartPosition(double beats) { sequencer.setStartPosition(beats); }
    double getStartPosition() const { return sequencer.getStartPosition(); }
    void pressStop() { sequencer.pressStop(); }
    void setLoop(bool on, double start, double end) { sequencer.setLoop(on, start, end); }
    bool isLoopEnabled() const { return sequencer.isLoopEnabled(); }
    double getLoopStart() const { return sequencer.getLoopStart(); }
    double getLoopEnd() const { return sequencer.getLoopEnd(); }
    void backToArrangement() { sequencer.requestBackToArrangement(); }
    bool isArrangementOverridden() const { return sequencer.isArrangementOverridden(); }
    bool followsArrangement(int t) const { return sequencer.followsArrangement(t); }
    void setArrangementRecord(bool on) { sequencer.setArrangementRecord(on); }
    bool isArrangementRecording() const { return sequencer.isArrangementRecording(); }

    // Colour of a track (used for its clips unless a clip has its own).
    static juce::Colour getTrackColour(int trackIndex) {
        static const juce::uint32 colours[] { 0xFF26A69A, 0xFFFFB74D, 0xFFBA68C8, 0xFF9CCC65,
                                              0xFF4FC3F7, 0xFFFF8A65, 0xFF9575CD, 0xFFF06292 };
        return juce::Colour(colours[static_cast<size_t>(trackIndex) % 8]);
    }

    juce::Colour getClipColour(int trackIndex, int clipIndex) const {
        const auto data = getClipData(trackIndex, clipIndex);
        return data != nullptr && data->colour != 0 ? juce::Colour(data->colour) : getTrackColour(trackIndex);
    }

    double getClipPlayPosition(int trackIndex) const { return sequencer.getClipPlayPosition(trackIndex); }
    double getSongPosition() const { return sequencer.getSongPosition(); }
    int getCurrentScene() const { return sequencer.getCurrentScene(); }
    int getQueuedScene() const { return sequencer.getQueuedScene(); }
    bool isStopAllQueued() const { return sequencer.isStopAllQueued(); }
    static constexpr int getNumTracks() { return NUM_TRACKS; }
    int getNumActiveTracks() const { return sequencer.getNumActiveTracks(); }
    int getNumActiveClips() const { return sequencer.getNumActiveClips(); }

private:
    struct ArrangementEditAction : juce::UndoableAction {
        ArrangementEditAction(StepSequencer& s, ArrangementData::Ptr b, ArrangementData::Ptr a) :
            sequencer(s), before(std::move(b)), after(std::move(a)) {}
        bool perform() override { sequencer.setArrangement(after); return true; }
        bool undo() override { sequencer.setArrangement(before); return true; }
        int getSizeInUnits() override { return 1; }
        StepSequencer& sequencer;
        ArrangementData::Ptr before, after;
    };

    struct ClipEditAction : juce::UndoableAction {
        ClipEditAction(StepSequencer& s, int t, int c, ClipData::Ptr b, ClipData::Ptr a) :
            sequencer(s), track(t), clip(c), before(std::move(b)), after(std::move(a)) {}
        bool perform() override { sequencer.setClipData(track, clip, after); return true; }
        bool undo() override { sequencer.setClipData(track, clip, before); return true; }
        int getSizeInUnits() override { return 1; }
        StepSequencer& sequencer;
        int track, clip;
        ClipData::Ptr before, after;
    };

    // Message-thread housekeeping: drains recorded notes into clips (30 Hz) and frees clip
    // snapshots the audio thread has let go of (about once a second).
    struct Housekeeping : juce::Timer {
        Housekeeping(Recorder& r, ArrangementLogger& l) : recorder(r), logger(l) { startTimerHz(30); }
        ~Housekeeping() override { stopTimer(); }
        void timerCallback() override {
            recorder.drain();
            logger.drain();
            if (++ticks % 30 == 0) {
                ReleasePool::get().collect();
            }
        }
        Recorder& recorder;
        ArrangementLogger& logger;
        int ticks = 0;
    };

    void applyDefaultDevices();
    void configureInstrument(int t, InstrumentType type);

    std::array<juce::Synthesiser, NUM_TRACKS> synths;
    std::array<TrackDevices, NUM_TRACKS> devices;
    std::array<TrackEffects, NUM_TRACKS> trackEffects;
    std::array<juce::AudioBuffer<float>, NUM_TRACKS> trackBuffers;
    StepSequencer sequencer;
    Mixer mixer;
    double currentSampleRate = 44100.0;
    Recorder recorder { sequencer, [this](int t, int slot, ClipData::Ptr before, ClipData::Ptr after) {
        performClipEdit(t, slot, before, after, "Record");
    } };
    ArrangementLogger arrangementLogger { sequencer, [this](ArrangementData::Ptr before, ArrangementData::Ptr after) {
        performArrangementEdit(before, after, "Record arrangement");
    } };
    Housekeeping housekeeping { recorder, arrangementLogger };
    juce::MidiBuffer inputMidi;
    juce::File currentSetFile;
    std::atomic<int> setVersion { 0 };
    Metronome metronome;
    std::atomic<bool> exclusiveSolo { true };
    std::atomic<bool> exclusiveArm { true };
    TapTempo tapTempoState;
    juce::UndoManager undoManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AndrotoneAudioProcessor)
};
