#pragma once

#include "core/Parameter.h"

#include <juce_core/juce_core.h>
#include <vector>

// How a machine's parameters split into titled sections on its panel. Each rule names ids; an id
// ending in '*' matches every id that starts with the rest. The first matching rule wins, and
// parameters no rule names go into a final "More" section, so a new parameter always shows up.
namespace PanelLayouts {
    struct Section {
        juce::String title;
        std::vector<int> parameters; // indices into the ParameterSet
    };

    struct Rule {
        juce::String title;
        juce::StringArray ids;
    };

    inline std::vector<Rule> rulesFor(const juce::String& type) {
        if (type == "Subtractive") {
            return { { "Oscillators", { "osc*" } },
                     { "Filter", { "filterType", "cutoff", "resonance", "filterEnvAmount" } },
                     { "Filter Envelope", { "filter*" } },
                     { "Amp Envelope", { "amp*" } },
                     { "LFO", { "lfo*" } },
                     { "Out", { "level" } } };
        }
        if (type == "Acid") {
            return { { "Oscillator", { "shape", "tune" } },
                     { "Filter", { "cutoff", "resonance", "envMod", "decay", "accent" } },
                     { "Glide", { "slide" } },
                     { "Out", { "level" } } };
        }
        if (type == "Supersaw") {
            return { { "Saws", { "saws", "detune", "sideMix" } },
                     { "Filter", { "cutoff", "resonance", "filterEnvAmount" } },
                     { "Filter Envelope", { "filter*" } },
                     { "Amp Envelope", { "amp*" } },
                     { "Out", { "level" } } };
        }
        if (type == "FM") {
            return { { "Operator 1", { "op1*" } },
                     { "Operator 2", { "op2*" } },
                     { "Operator 3", { "op3*" } },
                     { "Routing", { "algorithm", "feedback" } },
                     { "Out", { "level" } } };
        }
        if (type == "Organ") {
            return { { "Drawbars", { "drawbar*" } },
                     { "Percussion", { "percussion*", "click" } },
                     { "Vibrato and Rotary", { "vibrato*", "rotary*" } },
                     { "Out", { "level" } } };
        }
        if (type == "PadSynth") {
            return { { "Harmonics", { "harmonic*" } },
                     { "Bandwidth", { "bandwidth*" } },
                     { "Envelope", { "attack", "decay", "sustain", "release" } },
                     { "Out", { "level" } } };
        }
        if (type == "Pluck") {
            return { { "String", { "decay", "damping", "brightness" } },
                     { "Out", { "release", "level" } } };
        }
        if (type == "Sampler") {
            return { { "Pitch", { "tune" } },
                     { "Filter", { "cutoff", "resonance" } },
                     { "Envelope", { "attack", "decay", "sustain", "release" } },
                     { "Out", { "level" } } };
        }
        if (type == "Vocoder") {
            return { { "Voice", { "formantShift", "speed", "noise", "sibilance" } },
                     { "Bands", { "band*" } },
                     { "Envelope", { "attack", "release" } },
                     { "Out", { "level" } } };
        }
        if (type == "Bytebeat") {
            return { { "Clock", { "rate" } },
                     { "Envelope", { "attack", "release" } },
                     { "Out", { "level" } } };
        }
        if (type == "Master") {
            return { { "Delay", { "delay*" } },
                     { "Reverb", { "reverb*" } },
                     { "Equalizer", { "eq*" } },
                     { "Limiter", { "limiter*" } },
                     { "Master", { "volume" } } };
        }
        if (type == "Modular") {
            return { { "Oscillators", { "osc*", "noise" } },
                     { "Filter", { "filterType", "cutoff", "resonance" } },
                     { "Envelope 1", { "env1*" } },
                     { "Envelope 2", { "env2*" } },
                     { "LFOs", { "lfo*" } },
                     { "Patch Cables", { "patch*" } },
                     { "Out", { "level" } } };
        }
        return {};
    }

    inline bool matches(const juce::String& pattern, const juce::String& id) {
        return pattern.endsWithChar('*') ? id.startsWith(pattern.dropLastCharacters(1)) : id == pattern;
    }

    // Drums: one section per kit sound (ids "<sound>.<parameter>"), titled with the sound's name.
    inline std::vector<Section> drumSections(const ParameterSet& parameters) {
        std::vector<Section> sections;
        juce::String currentPrefix;
        for (int i = 0; i < parameters.size(); i++) {
            const auto& spec = parameters.getSpec(i);
            const auto prefix = spec.id.upToFirstOccurrenceOf(".", false, false);
            if (sections.empty() || prefix != currentPrefix) {
                currentPrefix = prefix;
                sections.push_back({ spec.name.upToLastOccurrenceOf(" ", false, false), {} });
            }
            sections.back().parameters.push_back(i);
        }
        return sections;
    }

    // Sections for `type`'s panel; one untitled section when there are no rules for it.
    inline std::vector<Section> sectionsFor(const juce::String& type, const ParameterSet& parameters) {
        if (type == "Drums") {
            return drumSections(parameters);
        }
        const auto rules = rulesFor(type);
        std::vector<Section> sections;
        for (const auto& rule : rules) {
            sections.push_back({ rule.title, {} });
        }
        Section more { rules.empty() ? juce::String() : juce::String("More"), {} };
        for (int i = 0; i < parameters.size(); i++) {
            const auto& id = parameters.getSpec(i).id;
            bool placed = false;
            for (size_t r = 0; r < rules.size() && !placed; r++) {
                for (const auto& pattern : rules[r].ids) {
                    if (matches(pattern, id)) {
                        sections[r].parameters.push_back(i);
                        placed = true;
                        break;
                    }
                }
            }
            if (!placed) {
                more.parameters.push_back(i);
            }
        }
        if (!more.parameters.empty()) {
            sections.push_back(std::move(more));
        }
        sections.erase(std::remove_if(sections.begin(), sections.end(), [](const Section& s) { return s.parameters.empty(); }),
                       sections.end());
        return sections;
    }
}
