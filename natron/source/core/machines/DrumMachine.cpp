#include "DrumMachine.h"

#include <cmath>

namespace {
    const juce::Identifier dataType { "Drums" };
    const juce::Identifier slotType { "Slot" };

    const std::array<juce::AudioBuffer<float>, DrumMachine::numSlots>& sharedKit() {
        static const auto generated = DrumKit::generate(); // generated once, shared by every instance
        return generated;
    }

    SampleData kitSound(int slot) {
        SampleData sound;
        sound.name = DrumKit::names()[static_cast<size_t>(slot)];
        sound.sampleRate = DrumKit::sampleRate;
        sound.audio.makeCopyOf(sharedKit()[static_cast<size_t>(slot)]);
        return sound;
    }
}

DrumMachine::DrumMachine() {
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
    setData({});
}

juce::ValueTree DrumMachine::getData() const {
    juce::ValueTree tree(dataType);
    if (const auto* current = kit.get()) {
        for (int slot = 0; slot < numSlots; slot++) {
            if (current->custom[static_cast<size_t>(slot)]) {
                juce::ValueTree slotTree(slotType);
                slotTree.setProperty("index", slot, nullptr);
                slotTree.appendChild(current->sounds[static_cast<size_t>(slot)].toTree(), nullptr);
                tree.appendChild(slotTree, nullptr);
            }
        }
    }
    return tree;
}

juce::Result DrumMachine::setData(const juce::ValueTree& data) {
    auto next = std::make_unique<Kit>();
    for (int slot = 0; slot < numSlots; slot++) {
        next->sounds[static_cast<size_t>(slot)] = kitSound(slot);
    }
    for (const auto& slotTree : data) {
        if (!slotTree.hasType(slotType)) {
            continue;
        }
        const int slot = slotTree.getProperty("index", -1);
        if (slot < 0 || slot >= numSlots) {
            return juce::Result::fail("Drum slot out of range: " + juce::String(slot));
        }
        if (const auto loaded = SampleData::fromTree(slotTree.getChildWithName("Sample"), next->sounds[static_cast<size_t>(slot)]);
            loaded.failed()) {
            return loaded;
        }
        next->custom[static_cast<size_t>(slot)] = true;
    }
    kit.set(std::move(next));
    return juce::Result::ok();
}

juce::ValueTree DrumMachine::withSample(const juce::ValueTree& data, int slot, const SampleData& sample) {
    auto result = withoutSample(data, slot);
    juce::ValueTree slotTree(slotType);
    slotTree.setProperty("index", slot, nullptr);
    slotTree.appendChild(sample.toTree(), nullptr);
    result.appendChild(slotTree, nullptr);
    return result;
}

juce::ValueTree DrumMachine::withoutSample(const juce::ValueTree& data, int slot) {
    juce::ValueTree result(dataType);
    for (const auto& child : data) {
        if (child.hasType(slotType) && static_cast<int>(child.getProperty("index", -1)) != slot) {
            result.appendChild(child.createCopy(), nullptr);
        }
    }
    return result;
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
    blockKit = kit.acquire();
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

        const auto& sound = blockKit->sounds[static_cast<size_t>(slot)];
        const float* data = sound.audio.getReadPointer(0); // drums play mono (then pan)
        const int length = sound.audio.getNumSamples();

        const auto get = [&](SlotParameter parameter) { return parameters.get(parameterIndex(slot, parameter)); };
        const double rate = sound.sampleRate / sampleRate * std::pow(2.0, get(tune) / 12.0);
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
