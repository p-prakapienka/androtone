#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SoundSources/SineSound.h"
#include "SoundSources/SineVoice.h"
#include "SoundSources/SawSound.h"
#include "SoundSources/SawVoice.h"

AndrotoneAudioProcessor::AndrotoneAudioProcessor() :
    AudioProcessor(
        BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
    ) {
        for (int i = 0; i < 8; ++i) {
            synths[0].addVoice(new SineVoice());
        }
        synths[0].addSound(new SineSound(1));

        for (int i = 0; i < 8; ++i) {
            synths[1].addVoice(new SawVoice());
        }
        synths[1].addSound(new SawSound(2));
    }

void AndrotoneAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/) {
    currentSampleRate = sampleRate;
    for (auto& synth : synths) {
        synth.setCurrentPlaybackSampleRate(sampleRate);
    }
    sequencer.prepareToPlay(sampleRate);
}

void AndrotoneAudioProcessor::releaseResources() {}

bool AndrotoneAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& main = layouts.getMainOutputChannelSet();
    return main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo();
}

void AndrotoneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    sequencer.processBlock(midi, buffer.getNumSamples());
    for (auto& synth : synths) {
        synth.renderNextBlock(buffer, midi, 0, buffer.getNumSamples());
    }

    buffer.applyGain(volume.load());
}

juce::AudioProcessorEditor* AndrotoneAudioProcessor::createEditor() {
    return new AndrotoneAudioProcessorEditor(*this);
}

void AndrotoneAudioProcessor::getStateInformation(juce::MemoryBlock&) {}
void AndrotoneAudioProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AndrotoneAudioProcessor();
}
