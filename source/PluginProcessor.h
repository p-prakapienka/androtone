#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
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
    double getTailLengthSeconds() const override { return 0.0; }

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
    void setCurrentClip(int trackIndex, int clipIndex) { sequencer.setCurrentClip(trackIndex, clipIndex); }

    bool isPlaying() const { return sequencer.isPlaying(); }
    double getTempo() const { return sequencer.getTempo(); }
    float getVolume() const { return mixer.getMasterVolume(); }
    float getTrackVolume(int trackIndex) const { return mixer.getTrackVolume(trackIndex); }
    int getCurrentClip(int trackIndex) const { return sequencer.getCurrentClip(trackIndex); }
    int getNextClip(int trackIndex) const { return sequencer.getNextClip(trackIndex); }
    static constexpr int getNumTracks() { return StepSequencer::numTracks; }

private:
    std::array<juce::Synthesiser, StepSequencer::numTracks> synths;
    std::array<juce::AudioBuffer<float>, StepSequencer::numTracks> trackBuffers;
    StepSequencer sequencer;
    Mixer mixer;
    double currentSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AndrotoneAudioProcessor)
};
