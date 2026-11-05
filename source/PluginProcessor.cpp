#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/SoundSources/SineSound.h"
#include "Engine/SoundSources/SineVoice.h"
#include "Engine/SoundSources/SawSound.h"
#include "Engine/SoundSources/SawVoice.h"
#include "Engine/SoundSources/SamplePlayerSound.h"
#include "Engine/SoundSources/SamplePlayerVoice.h"
// BinaryData lives in the Projucer-generated JuceLibraryCode folder, which isn't
// on the desktop CMake include path; reference it relatively so both builds resolve it.
#include "../JuceLibraryCode/BinaryData.h"

AndrotoneAudioProcessor::AndrotoneAudioProcessor() :
    AudioProcessor(
        BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
    ) {
        for (int i = 0; i < 8; i++) {
            synths[0].addVoice(new SamplePlayerVoice());
        }
        synths[0].addSound(
            SamplePlayerSound::fromMemory(
                1,
                BinaryData::wa_free_ldrum_kick_06_t1_wav,
                BinaryData::wa_free_ldrum_kick_06_t1_wavSize
            )
        );

        for (int i = 0; i < 8; i++) {
            synths[1].addVoice(new SawVoice());
        }
        synths[1].addSound(new SawSound(2));

        for (int i = 0; i < 8; i++) {
            synths[2].addVoice(new SineVoice());
        }
        synths[2].addSound(new SineSound(3));
    }

void AndrotoneAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    currentSampleRate = sampleRate;
    for (auto& synth : synths) {
        synth.setCurrentPlaybackSampleRate(sampleRate);
    }
    sequencer.prepareToPlay(sampleRate);

    const int numChannels = getTotalNumOutputChannels();
    inputMidi.ensureSize(4096);
    for (auto& buf : trackBuffers) {
        buf.setSize(numChannels, samplesPerBlock, false, false, true);
    }

    mixer.prepare(sampleRate);
    metronome.prepare(sampleRate);
}

void AndrotoneAudioProcessor::releaseResources() {
    mixer.reset();
}

bool AndrotoneAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& main = layouts.getMainOutputChannelSet();
    return main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo();
}

void AndrotoneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    // Plugin builds follow the host's tempo and transport; the standalone app runs its own clock.
    if (wrapperType != wrapperType_Standalone) {
        if (auto* playHead = getPlayHead()) {
            if (const auto position = playHead->getPosition()) {
                if (const auto hostBpm = position->getBpm()) {
                    sequencer.setTempo(*hostBpm);
                }
                sequencer.setHostPosition(position->getIsPlaying(), position->getPpqPosition().orFallback(0.0));
            }
        }
    }

    // Incoming MIDI is input for the sequencer (monitored on armed tracks, recorded when
    // recording); the synths only see what the sequencer outputs.
    inputMidi.clear();
    inputMidi.addEvents(midi, 0, numSamples, 0);
    midi.clear();
    sequencer.processBlock(midi, numSamples, &inputMidi);

    for (int t = 0; t < NUM_TRACKS; t++) {
        trackBuffers[t].clear(0, numSamples);
        synths[t].renderNextBlock(trackBuffers[t], midi, 0, numSamples);
    }

    mixer.mix(buffer, trackBuffers);

    // Metronome ticks, split at their sample positions.
    int rendered = 0;
    for (int i = 0; i < sequencer.getNumClicks(); i++) {
        const auto& click = sequencer.getClick(i);
        metronome.render(buffer, rendered, click.sample - rendered);
        metronome.trigger(click.accent);
        rendered = click.sample;
    }
    metronome.render(buffer, rendered, numSamples - rendered);
}

juce::AudioProcessorEditor* AndrotoneAudioProcessor::createEditor() {
    return new AndrotoneAudioProcessorEditor(*this);
}

void AndrotoneAudioProcessor::getStateInformation(juce::MemoryBlock&) {}
void AndrotoneAudioProcessor::setStateInformation(const void*, int) {}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AndrotoneAudioProcessor();
}
