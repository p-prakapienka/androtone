#pragma once

#include "Machine.h"

#include <functional>
#include <memory>

// Creates machines by type name, so songs can be loaded and the UI can offer a machine list.
// Message thread only.
namespace MachineRegistry {
    using Factory = std::function<std::unique_ptr<Machine>()>;

    juce::StringArray getTypeNames();

    // nullptr for an unknown type.
    std::unique_ptr<Machine> create(const juce::String& typeName);

    // Adds (or replaces) a type. The built-in machines are always registered; this is for tests
    // and future add-on machines.
    void registerType(const juce::String& typeName, Factory factory);
}
