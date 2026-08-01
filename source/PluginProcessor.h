#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/StepSequencer.h"
#include "Mixer/Mixer.h"
#include "Project/Projects.h"
#include <array>
#include <atomic>

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
    void setCurrentClip(int trackIndex, int clipIndex) { sequencer.setCurrentClip(trackIndex, clipIndex); }
    void setCurrentScene(int sceneIndex) { sequencer.setCurrentScene(sceneIndex); }
    void setSongMode(bool enabled) { sequencer.setSongMode(enabled); }
    void loadProject(int projectIndex);

    bool isPlaying() const { return sequencer.isPlaying(); }
    bool isSongMode() const { return sequencer.isSongMode(); }
    double getTempo() const { return sequencer.getTempo(); }
    float getVolume() const { return mixer.getMasterVolume(); }
    float getTrackVolume(int trackIndex) const { return mixer.getTrackVolume(trackIndex); }
    float getTrackReverbSend(int trackIndex) const { return mixer.getTrackReverbSend(trackIndex); }
    float getTrackDelaySend(int trackIndex) const { return mixer.getTrackDelaySend(trackIndex); }
    int getCurrentClip(int trackIndex) const { return sequencer.getCurrentClip(trackIndex); }
    bool isClipEmpty(int trackIndex, int clipIndex) const { return sequencer.isClipEmpty(trackIndex, clipIndex); }
    int getCurrentScene() const { return sequencer.getCurrentScene(); }
    int getNextScene() const { return sequencer.getNextScene(); }
    int getNextClip(int trackIndex) const { return sequencer.getNextClip(trackIndex); }
    static constexpr int getNumTracks() { return StepSequencer::numTracks; }
    int getNumActiveTracks() const { return sequencer.getNumActiveTracks(); }
    int getNumActiveClips() const { return sequencer.getNumActiveClips(); }
    int getNumProjects() const { return static_cast<int>(ProjectPresets::all.size()); }
    juce::String getProjectName(int projectIndex) const;
    int getCurrentProjectIndex() const { return currentProjectIndex.load(); }

private:
    std::array<juce::Synthesiser, StepSequencer::numTracks> synths;
    std::array<juce::AudioBuffer<float>, StepSequencer::numTracks> trackBuffers;
    StepSequencer sequencer;
    Mixer mixer;
    double currentSampleRate = 44100.0;
    std::atomic<int> currentProjectIndex { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AndrotoneAudioProcessor)
};
