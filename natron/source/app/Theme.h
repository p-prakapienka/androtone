#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Shared look: one colour per machine type (rack rows, song blocks, mixer strips, notes), and the
// touch sizes that differ between phones and desktops.
namespace Theme {
    inline juce::Colour machineColour(const juce::String& type) {
        static const std::pair<const char*, juce::uint32> colours[] {
            { "Subtractive", 0xff5f819d }, // blue
            { "Acid", 0xffcc6666 },        // red
            { "Drums", 0xffde935f },       // orange
            { "Supersaw", 0xff85678f },    // purple
            { "Pluck", 0xff8c9440 },       // olive
            { "Organ", 0xffa3685a },       // brown
            { "FM", 0xff5e8d87 },          // teal
            { "PadSynth", 0xff7e8fc9 },    // periwinkle
            { "Bytebeat", 0xffb5bd68 },    // lime
            { "Sampler", 0xffc9a554 },     // gold
            { "Vocoder", 0xffb294bb },     // lilac
            { "Modular", 0xff4f9fa8 },     // cyan
        };
        for (const auto& [name, colour] : colours) {
            if (type == name) {
                return juce::Colour(colour);
            }
        }
        return juce::Colour(0xff707880); // grey: unknown or test machines
    }

    // The face of a machine's panel: the dark surface with the faintest hint of its colour.
    inline juce::Colour panelColour(const juce::String& type) {
        return juce::Colour(0xff1b1d21).interpolatedWith(machineColour(type), 0.04f);
    }

    // Effect "pedals": each type its own colour.
    inline juce::Colour effectColour(const juce::String& type) {
        static const juce::uint32 palette[] { 0xffe0a458, 0xff8fb3d9, 0xffc98fb8, 0xff9cc48f, 0xffd98f8f, 0xffb3a6e0,
                                              0xffd9cf7a, 0xff8fd0c8, 0xffc4a98a };
        return juce::Colour(palette[static_cast<size_t>(type.hashCode() & 0x7fffffff) % std::size(palette)]);
    }

    // Lit mute and solo keys.
    inline const juce::Colour muteColour { 0xffe0603e };
    inline const juce::Colour soloColour { 0xffffc94a };

    // Phones get bigger knobs and buttons; a fingertip is ~8 mm.
    inline bool isTouchDevice() {
#if JUCE_ANDROID || JUCE_IOS
        return true;
#else
        return false;
#endif
    }
}
