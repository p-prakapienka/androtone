#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Constants.h"
#include "Sequencer/StepSequencer.h"
#include "Mixer/Mixer.h"
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
    bool isClipEmpty(int trackIndex, int clipIndex) const { return sequencer.isClipEmpty(trackIndex, clipIndex); }
    int getCurrentScene() const { return sequencer.getCurrentScene(); }
    int getQueuedScene() const { return sequencer.getQueuedScene(); }
    bool isStopAllQueued() const { return sequencer.isStopAllQueued(); }
    static constexpr int getNumTracks() { return NUM_TRACKS; }
    int getNumActiveTracks() const { return sequencer.getNumActiveTracks(); }
    int getNumActiveClips() const { return sequencer.getNumActiveClips(); }

private:
    std::array<juce::Synthesiser, NUM_TRACKS> synths;
    std::array<juce::AudioBuffer<float>, NUM_TRACKS> trackBuffers;
    StepSequencer sequencer;
    Mixer mixer;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AndrotoneAudioProcessor)
};
