#pragma once

// Test-only machines, registered with MachineRegistry under their type names.

#include "core/Machine.h"
#include "core/MachineRegistry.h"
#include "TestHelpers.h"

#include <atomic>
#include <vector>

// Records every note event it receives, with absolute sample positions (counting from the first
// block it rendered). Instances list themselves in `instances` so tests can find them after
// creating them through a Session.
class RecordingMachine : public Machine {
public:
    static constexpr const char* typeName = "TestRecorder";

    RecordingMachine() {
        events.reserve(4096);
        instances().push_back(this);
        parameters.add({ "gain", "Gain", 0.0f, 2.0f, 1.0f });
    }

    ~RecordingMachine() override {
        auto& all = instances();
        all.erase(std::remove(all.begin(), all.end(), this), all.end());
        destroyed()++;
    }

    juce::String getTypeName() const override { return typeName; }
    void prepare(double, int) override {}
    void reset() override {}

    void process(juce::AudioBuffer<float>& output, const juce::MidiBuffer& midi) override {
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOnOrOff()) {
                events.push_back({ position + metadata.samplePosition, message.isNoteOn(), message.getNoteNumber() });
            }
        }
        position += output.getNumSamples();
    }

    std::vector<RecordedEvent> noteOns() const {
        std::vector<RecordedEvent> result;
        for (const auto& e : events) {
            if (e.isNoteOn) {
                result.push_back(e);
            }
        }
        return result;
    }

    std::vector<RecordedEvent> events;
    int64_t position = 0;

    static std::vector<RecordingMachine*>& instances() {
        static std::vector<RecordingMachine*> all;
        return all;
    }

    static int& destroyed() {
        static int count = 0;
        return count;
    }

    static void registerType() {
        MachineRegistry::registerType(typeName, [] { return std::make_unique<RecordingMachine>(); });
    }
};
