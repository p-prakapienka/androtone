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

    // Phones get bigger knobs and buttons; a fingertip is ~8 mm.
    inline bool isTouchDevice() {
#if JUCE_ANDROID || JUCE_IOS
        return true;
#else
        return false;
#endif
    }
}
