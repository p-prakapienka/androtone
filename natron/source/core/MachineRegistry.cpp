#include "MachineRegistry.h"

#include "machines/TestToneMachine.h"

#include <map>

namespace {
    std::map<juce::String, MachineRegistry::Factory>& factories() {
        static std::map<juce::String, MachineRegistry::Factory> registered {
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
