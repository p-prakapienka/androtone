namespace rb
{

// Factory Sampler presets, built on the tonal factory sounds (all recorded at C3, so they play
// at their original pitch on the Sampler's root key). Effects are FX1 -> FX2 in series.

namespace samplerpresets
{
    // envMode: 0 = Trig, 1 = Gate. filterType: 0 = LP, 1 = BP, 2 = HP.
    static TrackPreset sampler (const char* name, const char* category, const char* sound, ParamValues values,
                         EffectPreset fx1, EffectPreset fx2)
    {
        TrackPreset p;
        p.name = name;
        p.category = category;
        p.type = InstrumentType::sampler;
        p.sampleRef = juce::String ("factory:") + sound;
        p.values = std::move (values);
        p.effects = { std::move (fx1), std::move (fx2) };
        return p;
    }

    static EffectPreset fx (const char* name, EffectType type) { return { name, type, {} }; }
}

void addSamplerPresets (std::vector<TrackPreset>& presets)
{
    using namespace samplerpresets;

    // Sampler
    presets.push_back (sampler ("Choir Ahh", "Sampler", "tonal-choir",
                                { { "attack", 0.15f }, { "release", 1.0f }, { "cutoff", 9000.0f } },
                                fx ("Ensemble", EffectType::chorus), fx ("Cathedral", EffectType::reverb)));
    presets.push_back (sampler ("Brass Stab", "Sampler", "tonal-brass",
                                { { "envMode", 0.0f }, { "decay", 0.9f }, { "cutoff", 7000.0f }, { "resonance", 0.15f } },
                                fx ("Warm Drive", EffectType::saturator), fx ("Room", EffectType::reverb)));
    presets.push_back (sampler ("String Section", "Sampler", "tonal-strings",
                                { { "attack", 0.2f }, { "release", 0.9f } },
                                fx ("Ensemble", EffectType::chorus), fx ("Hall", EffectType::reverb)));
    presets.push_back (sampler ("Nylon Strings", "Sampler", "tonal-nylon",
                                { { "envMode", 0.0f }, { "cutoff", 6000.0f } },
                                fx ("Body EQ", EffectType::eq), fx ("Room", EffectType::reverb)));
    presets.push_back (sampler ("Breath Flute", "Sampler", "tonal-flute",
                                { { "attack", 0.04f }, { "release", 0.35f } },
                                fx ("Soft Echo", EffectType::delay), fx ("Hall", EffectType::reverb)));
    presets.push_back (sampler ("Plucked Wire", "Sampler", "tonal-pluck",
                                { { "envMode", 0.0f }, { "cutoff", 8000.0f } },
                                fx ("Dotted Echo", EffectType::delay), fx ("Room", EffectType::reverb)));
    presets.push_back (sampler ("Saw Lead", "Sampler", "tonal-lead",
                                { { "release", 0.2f }, { "cutoff", 6000.0f }, { "resonance", 0.25f } },
                                fx ("Drive", EffectType::saturator), fx ("Echo", EffectType::delay)));
    presets.push_back (sampler ("Rubber Bass", "Sampler", "tonal-bass",
                                { { "transpose", -24.0f }, { "envMode", 0.0f }, { "decay", 1.4f }, { "cutoff", 2500.0f },
                                  { "resonance", 0.3f } },
                                fx ("Bass Drive", EffectType::saturator), fx ("Bass Comp", EffectType::dynamics)));
    presets.push_back (sampler ("Lo-Fi Keys", "Sampler", "tonal-epiano",
                                { { "cutoff", 2600.0f }, { "filterLfo", 0.12f }, { "lfoRate", 0.4f }, { "detune", -8.0f },
                                  { "release", 0.5f } },
                                fx ("Wobble", EffectType::chorus), fx ("Crunch", EffectType::crusher)));
    presets.push_back (sampler ("Kalimba Dreams", "Sampler", "tonal-kalimba",
                                { { "envMode", 0.0f } },
                                fx ("Ping Pong", EffectType::delay), fx ("Hall", EffectType::reverb)));
    presets.push_back (sampler ("Wobble Pad", "Sampler", "tonal-pad",
                                { { "attack", 0.3f }, { "release", 1.2f }, { "cutoff", 900.0f }, { "resonance", 0.45f },
                                  { "filterLfo", 0.45f }, { "lfoRate", 2.0f } },
                                fx ("Phaser", EffectType::phaser), fx ("Hall", EffectType::reverb)));
    presets.push_back (sampler ("Hollow Choir", "Sampler", "tonal-choir",
                                { { "filterType", 1.0f }, { "cutoff", 1200.0f }, { "resonance", 0.3f }, { "attack", 0.4f },
                                  { "release", 1.5f }, { "filterLfo", 0.15f }, { "lfoRate", 0.2f } },
                                fx ("Ensemble", EffectType::chorus), fx ("Cathedral", EffectType::reverb)));

    // Keys
    presets.push_back (sampler ("Tine Keys", "Keys", "tonal-epiano",
                                { { "release", 0.4f }, { "cutoff", 9000.0f } },
                                fx ("Tremolo Chorus", EffectType::chorus), fx ("Room", EffectType::reverb)));
    presets.push_back (sampler ("Drawbar Organ", "Keys", "tonal-organ",
                                { { "attack", 0.005f }, { "release", 0.08f } },
                                fx ("Rotary", EffectType::chorus), fx ("Room", EffectType::reverb)));

    // Mallets
    presets.push_back (sampler ("Glass Bell", "Mallets", "tonal-bell",
                                { { "envMode", 0.0f } },
                                fx ("Echo", EffectType::delay), fx ("Hall", EffectType::reverb)));
    presets.push_back (sampler ("Wood Marimba", "Mallets", "tonal-marimba",
                                { { "envMode", 0.0f } },
                                fx ("Warm EQ", EffectType::eq), fx ("Room", EffectType::reverb)));
    presets.push_back (sampler ("Vibe Bars", "Mallets", "tonal-vibes",
                                { { "release", 1.2f } },
                                fx ("Chorus", EffectType::chorus), fx ("Hall", EffectType::reverb)));

    // Pad
    presets.push_back (sampler ("Velvet Pad", "Pad", "tonal-pad",
                                { { "attack", 0.4f }, { "release", 1.5f }, { "cutoff", 3000.0f }, { "filterLfo", 0.15f },
                                  { "lfoRate", 0.3f } },
                                fx ("Ensemble", EffectType::chorus), fx ("Hall", EffectType::reverb)));
}

} // namespace rb
