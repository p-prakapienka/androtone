#include "DrumMachine.h"

#include <cmath>

namespace {
    const std::array<juce::AudioBuffer<float>, DrumMachine::numSlots>& sharedKit() {
        static const auto kit = DrumKit::generate(); // generated once, shared by every instance
        return kit;
    }
}

DrumMachine::DrumMachine() :
    kit(sharedKit()) {
    for (int slot = 0; slot < numSlots; slot++) {
        const juce::String prefix = "s" + juce::String(slot + 1) + ".";
        const juce::String name = DrumKit::names()[static_cast<size_t>(slot)];
        parameters.add({ prefix + "tune", name + " Tune", -12.0f, 12.0f, 0.0f });
        parameters.add({ prefix + "decay", name + " Decay", 0.05f, 1.0f, 1.0f }); // 1 = the whole sound
        parameters.add({ prefix + "pan", name + " Pan", -1.0f, 1.0f, 0.0f });
        parameters.add({ prefix + "level", name + " Level", 0.0f, 1.0f, 0.8f });
        parameters.add({ prefix + "mute", name + " Mute", 0.0f, 1.0f, 0.0f, 1.0f });
    }
    jassert(parameters.size() == numSlots * parametersPerSlot);
}

void DrumMachine::prepare(double newSampleRate, int) {
    sampleRate = newSampleRate;
    reset();
}

void DrumMachine::reset() {
    for (auto& p : playing) {
        p = {};
    }
}

void DrumMachine::process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) {
    int position = 0;
    for (const auto metadata : midi) {
        const int eventPosition = juce::jlimit(0, output.getNumSamples(), metadata.samplePosition);
        render(output, position, eventPosition);
        position = eventPosition;

        const auto message = metadata.getMessage();
        const int slot = message.getNoteNumber() - firstNote;
        if (message.isNoteOn() && slot >= 0 && slot < numSlots) {
            auto& p = playing[static_cast<size_t>(slot)];
            p.active = true;
            p.position = 0.0;
            p.gain = message.getFloatVelocity();
            p.decayGain = 1.0f;
        }
        // Note-offs are ignored: drum sounds are one-shots.
    }
    render(output, position, output.getNumSamples());
}

void DrumMachine::render(juce::AudioBuffer<float>& output, int start, int end) {
    if (end <= start) {
        return;
    }
    const int rightChannel = output.getNumChannels() > 1 ? 1 : 0;

    for (int slot = 0; slot < numSlots; slot++) {
        auto& p = playing[static_cast<size_t>(slot)];
        if (!p.active) {
            continue;
        }

        const auto& sound = kit[static_cast<size_t>(slot)];
        const float* data = sound.getReadPointer(0);
        const int length = sound.getNumSamples();

        const auto get = [&](SlotParameter parameter) { return parameters.get(parameterIndex(slot, parameter)); };
        const double rate = DrumKit::sampleRate / sampleRate * std::pow(2.0, get(tune) / 12.0);
        const float muteGain = get(mute) >= 0.5f ? 0.0f : 1.0f;
        const float slotLevel = get(level) * muteGain * p.gain;
        const float panValue = get(pan);
        const float leftGain = slotLevel * juce::jmin(1.0f, 1.0f - panValue);
        const float rightGain = slotLevel * juce::jmin(1.0f, 1.0f + panValue);

        // Decay below 1 fades the sound out over that fraction of its length.
        const float decayValue = get(decay);
        const float decayCoefficient = decayValue >= 0.999f
                                           ? 1.0f
                                           : static_cast<float>(std::exp(-1.0 / (decayValue * 0.3 * length / rate)));

        for (int i = start; i < end; i++) {
            const auto index = static_cast<int>(p.position);
            if (index >= length - 1) {
                p.active = false;
                break;
            }
            const auto fraction = static_cast<float>(p.position - index);
            const float sample = (data[index] + fraction * (data[index + 1] - data[index])) * p.decayGain;
            p.decayGain *= decayCoefficient;
            p.position += rate;

            output.addSample(0, i, sample * leftGain);
            if (rightChannel != 0) {
                output.addSample(rightChannel, i, sample * rightGain);
            }
        }
    }
}
