#include "MachineRegistry.h"

#include "machines/AcidBass.h"
#include "machines/DrumMachine.h"
#include "machines/SubtractiveSynth.h"
#include "machines/TestToneMachine.h"

#include <map>

namespace {
    std::map<juce::String, MachineRegistry::Factory>& factories() {
        static std::map<juce::String, MachineRegistry::Factory> registered {
            { SubtractiveSynth::typeName, [] { return std::make_unique<SubtractiveSynth>(); } },
            { AcidBass::typeName, [] { return std::make_unique<AcidBass>(); } },
            { DrumMachine::typeName, [] { return std::make_unique<DrumMachine>(); } },
            { TestToneMachine::typeName, [] { return std::make_unique<TestToneMachine>(); } },
        };
        return registered;
    }
}

juce::StringArray MachineRegistry::getTypeNames() {
    juce::StringArray names;
    for (const auto& entry : factories()) {
        names.add(entry.first);
    }
    return names;
}

std::unique_ptr<Machine> MachineRegistry::create(const juce::String& typeName) {
    const auto it = factories().find(typeName);
    return it != factories().end() ? it->second() : nullptr;
}

void MachineRegistry::registerType(const juce::String& typeName, Factory factory) {
    factories()[typeName] = std::move(factory);
}
