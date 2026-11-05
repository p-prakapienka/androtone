#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Constants.h"
#include "Sequencer/StepSequencer.h"
#include "Mixer/Mixer.h"
#include "Engine/SoundSources/Metronome.h"
#include "Sequencer/TapTempo.h"
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
    bool isMetronomeOn() const { return sequencer.isMetronomeOn(); }
    void setMetronomeVolume(float volume) { metronome.setVolume(volume); }
    float getMetronomeVolume() const { return metronome.getVolume(); }
    void setSwing(double timing, double velocity) { sequencer.setSwing(timing, velocity); }
    double getSwingTiming() const { return sequencer.getSwingTiming(); }
    double getSwingVelocity() const { return sequencer.getSwingVelocity(); }

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
    void setTrackVolume(int trackIndex, float vol) { mixer.setTrackVolume(trackIndex, vol); }
    void setTrackReverbSend(int trackIndex, float amount) { mixer.setTrackReverbSend(trackIndex, amount); }
    void setTrackDelaySend(int trackIndex, float amount) { mixer.setTrackDelaySend(trackIndex, amount); }
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

    // Frees clip snapshots replaced by the editor once the audio thread has let go of them.
    struct ReleasePoolCollector : juce::Timer {
        ReleasePoolCollector() { startTimer(1000); }
        ~ReleasePoolCollector() override { stopTimer(); }
        void timerCallback() override { ReleasePool::get().collect(); }
    };

    std::array<juce::Synthesiser, NUM_TRACKS> synths;
    std::array<juce::AudioBuffer<float>, NUM_TRACKS> trackBuffers;
    StepSequencer sequencer;
    Mixer mixer;
    double currentSampleRate = 44100.0;
    ReleasePoolCollector releasePoolCollector;
    Metronome metronome;
    TapTempo tapTempoState;
    juce::UndoManager undoManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AndrotoneAudioProcessor)
};
