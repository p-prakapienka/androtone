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
    for (auto& buf : trackBuffers) {
        buf.setSize(numChannels, samplesPerBlock, false, false, true);
    }

    mixer.prepare(sampleRate);
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

    sequencer.processBlock(midi, numSamples);

    for (int t = 0; t < StepSequencer::numTracks; t++) {
        trackBuffers[t].clear(0, numSamples);
        synths[t].renderNextBlock(trackBuffers[t], midi, 0, numSamples);
    }

    mixer.mix(buffer, trackBuffers);
}

juce::AudioProcessorEditor* AndrotoneAudioProcessor::createEditor() {
    return new AndrotoneAudioProcessorEditor(*this);
}

void AndrotoneAudioProcessor::getStateInformation(juce::MemoryBlock&) {}
void AndrotoneAudioProcessor::setStateInformation(const void*, int) {}

void AndrotoneAudioProcessor::loadProject(int projectIndex) {
    if (projectIndex < 0 || projectIndex >= getNumProjects()
        || projectIndex == currentProjectIndex.load()) {
        return;
    }

    // Project changes are initiated by the UI thread. Suspend the audio callback while replacing
    // clip data, and explicitly silence voices so the previous project cannot ring into the new one.
    sequencer.setPlaying(false);
    suspendProcessing(true);
    for (auto& synth : synths) {
        synth.allNotesOff(0, false);
    }
    sequencer.loadProject(*ProjectPresets::all[static_cast<std::size_t>(projectIndex)].project);
    currentProjectIndex.store(projectIndex);
    suspendProcessing(false);
}

juce::String AndrotoneAudioProcessor::getProjectName(int projectIndex) const {
    if (projectIndex < 0 || projectIndex >= getNumProjects()) {
        return {};
    }
    return ProjectPresets::all[static_cast<std::size_t>(projectIndex)].name;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new AndrotoneAudioProcessor();
}
