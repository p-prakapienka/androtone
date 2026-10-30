#pragma once

#include "../Machine.h"
#include "../SampleData.h"
#include "../SwappableResource.h"
#include "../dsp/DrumKit.h"

#include <array>

// 8-slot drum sampler (the BeatBox slot in the spec), loaded with Natron's synthesised kit.
// Slot n plays on MIDI note firstNote + n (36 = kick ... 43 = cowbell); a pattern's rows are
// those pitches. Each slot has tune, decay, pan, level, and mute. Retriggering a slot restarts it.
// Any slot can hold an imported sample instead (the machine's data); the rest use the kit.
class DrumMachine : public Machine {
public:
    static constexpr const char* typeName = "Drums";
    static constexpr int numSlots = DrumKit::numSounds;
    static constexpr int firstNote = 36;

    enum SlotParameter { tune, decay, pan, level, mute, parametersPerSlot };

    static int parameterIndex(int slot, SlotParameter parameter) { return slot * parametersPerSlot + parameter; }

    struct Kit {
        std::array<SampleData, numSlots> sounds;
        std::array<bool, numSlots> custom {};
    };

    DrumMachine();

    juce::String getTypeName() const override { return typeName; }
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override;
    void reset() override;

    // <Drums><Slot index="0"><Sample .../></Slot>...</Drums>: only the custom slots.
    juce::ValueTree getData() const override;
    juce::Result setData(const juce::ValueTree& data) override;
    void collectGarbage() override { kit.collect(); }

    // Helpers for building data: `data` with slot replaced by a sample, or back to the kit sound.
    static juce::ValueTree withSample(const juce::ValueTree& data, int slot, const SampleData& sample);
    static juce::ValueTree withoutSample(const juce::ValueTree& data, int slot);

private:
    struct Playback {
        bool active = false;
        double position = 0.0; // in kit samples
        float gain = 0.0f;
        float decayGain = 1.0f;
    };

    SwappableResource<Kit> kit;
    const Kit* blockKit = nullptr;
    std::array<Playback, numSlots> playing {};
    double sampleRate = 48000.0;

    void render(juce::AudioBuffer<float>& output, int start, int end);
};
