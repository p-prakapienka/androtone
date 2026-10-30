#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <deque>
#include <vector>

struct ParameterSpec {
    juce::String id; // stable: used in song files and (later) automation
    juce::String name;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float interval = 0.0f; // step size for discrete choices (1 = whole numbers); 0 = continuous
    juce::StringArray choices {}; // names for the values minValue, minValue + 1, ... (shown as a menu)

    // A 0/1 parameter (mute, solo): shown as a toggle.
    bool isToggle() const {
        return choices.isEmpty() && juce::exactlyEqual(interval, 1.0f) && juce::exactlyEqual(minValue, 0.0f)
               && juce::exactlyEqual(maxValue, 1.0f);
    }
};

// A machine's (or effect's) fixed parameter list, declared in its constructor. Values are atomics:
// the message thread writes them, the audio thread reads them, no locks.
class ParameterSet {
public:
    // Only call while constructing the owner, before it is shared with the audio thread.
    int add(ParameterSpec spec) {
        values.emplace_back(juce::jlimit(spec.minValue, spec.maxValue, spec.defaultValue));
        specs.push_back(std::move(spec));
        return static_cast<int>(specs.size()) - 1;
    }

    int size() const { return static_cast<int>(specs.size()); }
    const ParameterSpec& getSpec(int index) const { return specs[static_cast<size_t>(index)]; }

    int indexOf(const juce::String& id) const {
        for (size_t i = 0; i < specs.size(); i++) {
            if (specs[i].id == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    float get(int index) const { return values[static_cast<size_t>(index)].load(std::memory_order_relaxed); }

    void set(int index, float value) {
        const auto& spec = getSpec(index);
        values[static_cast<size_t>(index)].store(juce::jlimit(spec.minValue, spec.maxValue, value),
                                                 std::memory_order_relaxed);
    }

private:
    std::vector<ParameterSpec> specs;
    std::deque<std::atomic<float>> values; // deque: growing it never moves existing atomics
};
