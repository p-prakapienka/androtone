#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/StepSequencer.h"
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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    void setTempo(double bpm) { sequencer.setTempo(bpm); }
    void setVolume(float vol) { volume.store(vol); }
    void setPlaying(bool shouldPlay) { sequencer.setPlaying(shouldPlay); }

    bool isPlaying() const { return sequencer.isPlaying(); }
    double getTempo() const { return sequencer.getTempo(); }
    float getVolume() const { return volume.load(); }

private:
    juce::Synthesiser synth;
    StepSequencer sequencer;
    std::atomic<float> volume { 0.75f };
    double currentSampleRate { 44100.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AndrotoneAudioProcessor)
};
