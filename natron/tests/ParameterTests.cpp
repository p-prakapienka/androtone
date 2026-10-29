#include "core/Parameter.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Parameters start at their default and clamp to their range", "[parameters]") {
    ParameterSet parameters;
    const int cutoff = parameters.add({ "cutoff", "Cutoff", 20.0f, 20000.0f, 1000.0f });
    const int resonance = parameters.add({ "resonance", "Resonance", 0.0f, 1.0f, 2.0f }); // default clamped

    CHECK(parameters.size() == 2);
    CHECK(juce::exactlyEqual(parameters.get(cutoff), 1000.0f));
    CHECK(juce::exactlyEqual(parameters.get(resonance), 1.0f));

    parameters.set(cutoff, 5.0f);
    CHECK(juce::exactlyEqual(parameters.get(cutoff), 20.0f));
    parameters.set(cutoff, 440.0f);
    CHECK(juce::exactlyEqual(parameters.get(cutoff), 440.0f));
}

TEST_CASE("Parameters are found by id", "[parameters]") {
    ParameterSet parameters;
    parameters.add({ "a", "A", 0.0f, 1.0f, 0.0f });
    parameters.add({ "b", "B", 0.0f, 1.0f, 0.0f });

    CHECK(parameters.indexOf("b") == 1);
    CHECK(parameters.indexOf("c") == -1);
    CHECK(parameters.getSpec(0).name == "A");
}

TEST_CASE("Adding parameters never moves earlier values", "[parameters]") {
    ParameterSet parameters;
    parameters.add({ "first", "First", 0.0f, 1.0f, 0.5f });
    parameters.set(0, 0.25f);
    for (int i = 0; i < 100; i++) {
        parameters.add({ "p" + juce::String(i), "P", 0.0f, 1.0f, 0.0f });
    }
    CHECK(juce::exactlyEqual(parameters.get(0), 0.25f));
}
