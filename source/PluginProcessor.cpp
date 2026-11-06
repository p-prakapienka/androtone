#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Engine/SoundSources/AndroSynth.h"
#include "Engine/SoundSources/DrumKit.h"
#include "Engine/SoundSources/SamplePlayerSound.h"
#include "Engine/SoundSources/SamplePlayerVoice.h"
// BinaryData lives in the Projucer-generated JuceLibraryCode folder, which isn't
// on the desktop CMake include path; reference it relatively so both builds resolve it.
#include "../JuceLibraryCode/BinaryData.h"

CliptoneAudioProcessor::CliptoneAudioProcessor() :
    AudioProcessor(
        BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)
    ) {
    applyDefaultDevices();
}

// Default instruments: track 1 plays the kick sample, tracks 2 and 3 are synths voiced like the
// original saw (dark, short tail) and sine tracks; the rest are plain synths.
void CliptoneAudioProcessor::applyDefaultDevices() {
    for (int t = 0; t < NUM_TRACKS; t++) {
        auto& device = devices[static_cast<size_t>(t)];
        device.effectParams.resetTo(effectSpecs());
        const auto type = t == 0 ? InstrumentType::Sampler : InstrumentType::Synth;
        device.instrumentParams.resetTo(instrumentSpecs(type));
        configureInstrument(t, type);
    }
    auto& saw = devices[1].instrumentParams;
    saw.set(SynthParam::Wave, 1.0f);
    saw.set(SynthParam::Cutoff, 500.0f);
    saw.set(SynthParam::Resonance, 0.0f);
    saw.set(SynthParam::FilterEnv, 0.0f);
    saw.set(SynthParam::Sustain, 1.0f);
    saw.set(SynthParam::Release, 0.05f);
    auto& sine = devices[2].instrumentParams;
    sine.set(SynthParam::Wave, 0.0f);
    sine.set(SynthParam::Cutoff, 18000.0f);
    sine.set(SynthParam::FilterEnv, 0.0f);
    sine.set(SynthParam::Sustain, 1.0f);
    sine.set(SynthParam::Release, 0.05f);
}

// Rebuilds a track's synthesiser for an instrument type. The audio thread must not be rendering
// this synth (call from the constructor or with processing suspended).
void CliptoneAudioProcessor::configureInstrument(int t, InstrumentType type) {
    auto& synth = synths[static_cast<size_t>(t)];
    auto& device = devices[static_cast<size_t>(t)];
    const int channel = t + 1;
    synth.clearVoices();
    synth.clearSounds();
    device.instrument.store(type);

    switch (type) {
        case InstrumentType::Sampler:
            for (int i = 0; i < 8; i++) {
                synth.addVoice(new SamplePlayerVoice(&device.instrumentParams));
            }
            synth.addSound(SamplePlayerSound::fromMemory(channel, BinaryData::wa_free_ldrum_kick_06_t1_wav,
                                                         BinaryData::wa_free_ldrum_kick_06_t1_wavSize));
            break;
        case InstrumentType::Synth:
            for (int i = 0; i < 8; i++) {
                synth.addVoice(new AndroSynthVoice(device.instrumentParams));
            }
            synth.addSound(new AndroSynthSound(channel));
            break;
        case InstrumentType::Drums:
            for (int i = 0; i < 12; i++) {
                synth.addVoice(new DrumKitVoice(device.instrumentParams));
            }
            synth.addSound(new DrumKitSound(channel));
            break;
    }
    synth.setCurrentPlaybackSampleRate(currentSampleRate);
}

void CliptoneAudioProcessor::setInstrument(int t, InstrumentType type) {
    if (t < 0 || t >= NUM_TRACKS || devices[static_cast<size_t>(t)].instrument.load() == type) {
        return;
    }
    suspendProcessing(true);
    devices[static_cast<size_t>(t)].instrumentParams.resetTo(instrumentSpecs(type));
    configureInstrument(t, type);
    suspendProcessing(false);
}

void CliptoneAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
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
    for (auto& fx : trackEffects) {
        fx.prepare(sampleRate);
    }
}

void CliptoneAudioProcessor::releaseResources() {
    mixer.reset();
}

bool CliptoneAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& main = layouts.getMainOutputChannelSet();
    return main == juce::AudioChannelSet::mono() || main == juce::AudioChannelSet::stereo();
}

void CliptoneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
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
        trackEffects[static_cast<size_t>(t)].process(trackBuffers[t], numSamples, devices[static_cast<size_t>(t)].effectParams);
    }

    // Arrangement automation drives the mixer while the transport runs.
    if (sequencer.isPlaying()) {
        if (const auto arrangement = sequencer.getArrangement()) {
            automation.apply(*arrangement, sequencer.getSongPosition(), mixer);
        }
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

juce::AudioProcessorEditor* CliptoneAudioProcessor::createEditor() {
    return new CliptoneAudioProcessorEditor(*this);
}

juce::ValueTree CliptoneAudioProcessor::createSetState() {
    SetSerializer::Extras extras { metronome.getVolume(), exclusiveSolo.load(), exclusiveArm.load(), recorder.getQuantization(), {} };
    for (int t = 0; t < NUM_TRACKS; t++) {
        auto& snapshot = extras.devices[static_cast<size_t>(t)];
        const auto& device = devices[static_cast<size_t>(t)];
        snapshot.present = true;
        snapshot.instrument = static_cast<int>(device.instrument.load());
        for (int i = 0; i < MAX_DEVICE_PARAMS; i++) {
            snapshot.instrumentValues[static_cast<size_t>(i)] = device.instrumentParams.get(i);
            snapshot.effectValues[static_cast<size_t>(i)] = device.effectParams.get(i);
        }
    }
    return SetSerializer::save(sequencer, mixer, extras);
}

bool CliptoneAudioProcessor::restoreSetState(const juce::ValueTree& set) {
    if (!set.hasType("CLIPTONE_SET")) {
        return false;
    }
    // Swap the whole set with the audio thread held off, then start a fresh undo history.
    suspendProcessing(true);
    SetSerializer::Extras extras;
    const bool ok = SetSerializer::load(set, sequencer, mixer, extras);
    if (ok) {
        applyDefaultDevices();
        for (int t = 0; t < NUM_TRACKS; t++) {
            const auto& snapshot = extras.devices[static_cast<size_t>(t)];
            if (!snapshot.present) {
                continue;
            }
            auto& device = devices[static_cast<size_t>(t)];
            configureInstrument(t, static_cast<InstrumentType>(snapshot.instrument));
            for (int i = 0; i < MAX_DEVICE_PARAMS; i++) {
                device.instrumentParams.set(i, snapshot.instrumentValues[static_cast<size_t>(i)]);
                device.effectParams.set(i, snapshot.effectValues[static_cast<size_t>(i)]);
            }
        }
        metronome.setVolume(extras.metronomeVolume);
        exclusiveSolo.store(extras.exclusiveSolo);
        exclusiveArm.store(extras.exclusiveArm);
        recorder.setQuantization(extras.recordQuantization);
    }
    suspendProcessing(false);
    undoManager.clearUndoHistory();
    setVersion++;
    return ok;
}

bool CliptoneAudioProcessor::saveSet(const juce::File& file) {
    const auto xml = createSetState().createXml();
    if (xml == nullptr || !xml->writeTo(file)) {
        return false;
    }
    currentSetFile = file;
    return true;
}

bool CliptoneAudioProcessor::loadSet(const juce::File& file) {
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || !restoreSetState(juce::ValueTree::fromXml(*xml))) {
        return false;
    }
    currentSetFile = file;
    return true;
}

void CliptoneAudioProcessor::newSet() {
    suspendProcessing(true);
    sequencer.setPlaying(false);
    sequencer.loadProject(ProjectPresets::defaultProject);
    sequencer.setArrangement(new ArrangementData());
    applyDefaultDevices();
    suspendProcessing(false);
    undoManager.clearUndoHistory();
    currentSetFile = juce::File();
    setVersion++;
}

// Plugin state (host session): the whole set.
void CliptoneAudioProcessor::getStateInformation(juce::MemoryBlock& destination) {
    if (const auto xml = createSetState().createXml()) {
        copyXmlToBinary(*xml, destination);
    }
}

void CliptoneAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (const auto xml = getXmlFromBinary(data, sizeInBytes)) {
        restoreSetState(juce::ValueTree::fromXml(*xml));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new CliptoneAudioProcessor();
}
