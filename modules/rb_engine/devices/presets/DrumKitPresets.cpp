namespace rb
{

// Factory drum kits. Every kit except Hand Percussion uses the same pad layout so patterns
// transfer between kits:
//    0 Kick       1 Snare     2 Clap      3 Rim
//    4 Closed Hat 5 Open Hat  6 Shaker    7 Perc
//    8 Low Tom    9 Mid Tom  10 High Tom 11 Crash
//   12 Cowbell   13 Conga    14 FX 1     15 FX 2
// Closed and open hats are in the choke group. effects[0] is Pad FX (send), effects[1] Kit FX.

namespace drumkitpresets
{
    static PadPreset pad (const char* name, const char* sound, ParamValues values = {})
    {
        PadPreset p;
        p.name = name;
        p.sampleRef = juce::String ("factory:") + sound;
        p.values = std::move (values);
        return p;
    }

    static TrackPreset kit (const char* name, std::array<PadPreset, numDrumPads> pads,
                     EffectPreset padFx, EffectPreset kitFx, ParamValues values = {})
    {
        TrackPreset p;
        p.name = name;
        p.category = "Drums";
        p.type = InstrumentType::drumKit;
        p.values = std::move (values);
        p.effects = { std::move (padFx), std::move (kitFx) };
        p.pads = std::move (pads);
        return p;
    }

    static EffectPreset fx (const char* name, EffectType type) { return { name, type, {} }; }

    // Index of a playback FX type in the pad spec's "fxType" choices.
    static float fxType (const char* name)
    {
        const auto& spec = getPadSpec();
        const int index = spec.indexOf ("fxType");
        jassert (index >= 0 && spec.params[(size_t) index].choices.contains (name));
        return index < 0 ? 0.0f : (float) juce::jmax (0, spec.params[(size_t) index].choices.indexOf (name));
    }

    // Common per-pad settings.
    const ParamValues choke { { "choke", 1.0f } };
    static ParamValues with (ParamValues base, const ParamValues& extra)
    {
        for (auto& [id, v] : extra)
            base[id] = v;
        return base;
    }
}

void addDrumKitPresets (std::vector<TrackPreset>& presets)
{
    using namespace drumkitpresets;

    presets.push_back (kit ("Boom Kit",
    { {
        pad ("Kick", "kick-boom", { { "decay", 2.0f } }),
        pad ("Snare", "snare-808", { { "send", 0.2f } }),
        pad ("Clap", "clap-classic", { { "send", 0.3f } }),
        pad ("Rim", "perc-rim"),
        pad ("Closed Hat", "hat-closed", with (choke, { { "pan", 0.1f } })),
        pad ("Open Hat", "hat-open", with (choke, { { "pan", 0.1f }, { "decay", 0.8f } })),
        pad ("Shaker", "perc-shaker", { { "pan", -0.2f } }),
        pad ("Clave", "perc-clave", { { "pan", 0.25f } }),
        pad ("Low Tom", "tom-electro", { { "transpose", -5.0f }, { "pan", -0.25f } }),
        pad ("Mid Tom", "tom-electro", { { "pan", 0.0f } }),
        pad ("High Tom", "tom-electro", { { "transpose", 5.0f }, { "pan", 0.25f } }),
        pad ("Crash", "cymbal-crash", { { "send", 0.15f } }),
        pad ("Cowbell", "perc-cowbell", { { "pan", 0.2f } }),
        pad ("Conga", "perc-conga-high", { { "pan", -0.3f } }),
        pad ("Zap", "fx-zap", { { "send", 0.3f } }),
        pad ("Laser", "fx-laser", { { "send", 0.4f } }),
    } },
    fx ("Plate", EffectType::reverb), fx ("Glue", EffectType::dynamics)));

    presets.push_back (kit ("Studio Kit",
    { {
        pad ("Kick", "kick-acoustic"),
        pad ("Snare", "snare-acoustic", { { "send", 0.25f } }),
        pad ("Clap", "clap-big", { { "send", 0.2f } }),
        pad ("Rim", "perc-rim", { { "send", 0.2f } }),
        pad ("Closed Hat", "hat-closed", with (choke, { { "pan", 0.2f }, { "cutoff", 14000.0f } })),
        pad ("Open Hat", "hat-open", with (choke, { { "pan", 0.2f } })),
        pad ("Shaker", "perc-shaker", { { "pan", -0.3f } }),
        pad ("Woodblock", "perc-woodblock", { { "pan", 0.3f } }),
        pad ("Low Tom", "tom-low", { { "pan", 0.35f }, { "send", 0.15f } }),
        pad ("Mid Tom", "tom-mid", { { "pan", 0.1f }, { "send", 0.15f } }),
        pad ("High Tom", "tom-high", { { "pan", -0.2f }, { "send", 0.15f } }),
        pad ("Crash", "cymbal-crash", { { "pan", -0.3f }, { "send", 0.1f } }),
        pad ("Cowbell", "perc-cowbell", { { "pan", 0.25f } }),
        pad ("Conga", "perc-conga-low", { { "pan", -0.25f } }),
        pad ("Impact", "fx-impact", { { "send", 0.3f } }),
        pad ("Reverse", "cymbal-reverse"),
    } },
    fx ("Studio Room", EffectType::reverb), fx ("Kit Compressor", EffectType::dynamics)));

    presets.push_back (kit ("Dusty Kit",
    { {
        pad ("Kick", "kick-lofi", { { "cutoff", 5000.0f } }),
        pad ("Snare", "snare-lofi", { { "send", 0.2f } }),
        pad ("Clap", "clap-lofi", { { "send", 0.25f } }),
        pad ("Rim", "perc-rim", { { "fxType", fxType ("8-Bit") }, { "fx1", 0.5f }, { "fx2", 0.3f } }),
        pad ("Closed Hat", "hat-lofi", with (choke, { { "velAmount", 0.0f } })),
        pad ("Open Hat", "hat-open", with (choke, { { "fxType", fxType ("8-Bit") }, { "fx1", 0.5f }, { "fx2", 0.35f },
                                                    { "cutoff", 7000.0f }, { "decay", 0.6f } })),
        pad ("Shaker", "perc-shaker", { { "cutoff", 7000.0f }, { "pan", -0.2f } }),
        pad ("Snap", "perc-snap", { { "send", 0.3f } }),
        pad ("Low Tom", "tom-low", { { "cutoff", 3000.0f } }),
        pad ("Mid Tom", "tom-mid", { { "cutoff", 3000.0f } }),
        pad ("High Tom", "tom-high", { { "cutoff", 3000.0f } }),
        pad ("Crash", "cymbal-crash", { { "cutoff", 6000.0f }, { "fxType", fxType ("8-Bit") }, { "fx1", 0.4f }, { "fx2", 0.4f } }),
        pad ("Cowbell", "perc-cowbell", { { "cutoff", 4000.0f } }),
        pad ("Bongo", "perc-bongo"),
        pad ("Vinyl", "fx-vinyl", { { "envMode", 1.0f } }),
        pad ("Glitch", "fx-glitch"),
    } },
    fx ("Tape Echo", EffectType::delay), fx ("Dust", EffectType::crusher)));

    presets.push_back (kit ("Electro Kit",
    { {
        pad ("Kick", "kick-punch"),
        pad ("Snare", "snare-electro", { { "send", 0.2f } }),
        pad ("Clap", "clap-tight", { { "send", 0.3f } }),
        pad ("Rim", "perc-rim"),
        pad ("Closed Hat", "hat-noise", choke),
        pad ("Open Hat", "hat-open-short", choke),
        pad ("Shaker", "perc-shaker"),
        pad ("Clave", "perc-clave", { { "send", 0.25f } }),
        pad ("Low Tom", "tom-electro", { { "transpose", -7.0f } }),
        pad ("Mid Tom", "tom-electro", { { "transpose", -2.0f } }),
        pad ("High Tom", "tom-electro", { { "transpose", 3.0f } }),
        pad ("Crash", "cymbal-splash", { { "send", 0.2f } }),
        pad ("Cowbell", "perc-cowbell"),
        pad ("Bongo", "perc-bongo"),
        pad ("Laser", "fx-laser", { { "send", 0.4f } }),
        pad ("Zap", "fx-zap", { { "send", 0.3f } }),
    } },
    fx ("Ping Echo", EffectType::delay), fx ("Drive", EffectType::saturator)));

    presets.push_back (kit ("House Kit",
    { {
        pad ("Kick", "kick-deep"),
        pad ("Snare", "snare-tight", { { "send", 0.2f } }),
        pad ("Clap", "clap-big", { { "send", 0.3f } }),
        pad ("Rim", "perc-rim", { { "send", 0.2f } }),
        pad ("Closed Hat", "hat-closed", with (choke, { { "decay", 0.2f } })),
        pad ("Open Hat", "hat-open", with (choke, { { "decay", 0.5f } })),
        pad ("Shaker", "perc-shaker", { { "pan", 0.3f } }),
        pad ("Tambourine", "perc-tambourine", { { "pan", -0.3f } }),
        pad ("Low Tom", "tom-low"),
        pad ("Mid Tom", "tom-mid"),
        pad ("High Tom", "tom-high"),
        pad ("Crash", "cymbal-crash", { { "send", 0.2f } }),
        pad ("Cowbell", "perc-cowbell", { { "transpose", 2.0f } }),
        pad ("Conga", "perc-conga-high"),
        pad ("Riser", "fx-riser"),
        pad ("Sweep", "fx-noise-sweep"),
    } },
    fx ("Hall", EffectType::reverb), fx ("Pump", EffectType::dynamics)));

    presets.push_back (kit ("Trap Kit",
    { {
        pad ("808", "kick-boom", { { "transpose", -2.0f }, { "decay", 2.5f } }),
        pad ("Snare", "snare-trap", { { "send", 0.2f } }),
        pad ("Clap", "clap-classic", { { "send", 0.35f } }),
        pad ("Rim", "perc-rim"),
        pad ("Closed Hat", "hat-trap", choke),
        pad ("Open Hat", "hat-open-short", choke),
        pad ("Shaker", "perc-shaker"),
        pad ("Snap", "perc-snap", { { "send", 0.3f } }),
        pad ("Low Tom", "tom-electro", { { "transpose", -5.0f } }),
        pad ("Mid Tom", "tom-electro"),
        pad ("High Tom", "tom-electro", { { "transpose", 5.0f } }),
        pad ("Crash", "cymbal-crash"),
        pad ("Cowbell", "perc-cowbell"),
        pad ("Conga", "perc-conga-high"),
        pad ("Impact", "fx-impact"),
        pad ("Riser", "fx-riser"),
    } },
    fx ("Dark Room", EffectType::reverb), fx ("Warmth", EffectType::saturator)));

    presets.push_back (kit ("Techno Kit",
    { {
        pad ("Kick", "kick-techno", { { "fxType", fxType ("Punch") }, { "fx1", 0.35f }, { "fx2", 0.3f } }),
        pad ("Snare", "snare-tight", { { "send", 0.25f } }),
        pad ("Clap", "clap-tight", { { "send", 0.35f } }),
        pad ("Rim", "perc-rim", { { "send", 0.3f } }),
        pad ("Closed Hat", "hat-closed-tight", choke),
        pad ("Open Hat", "hat-open-short", choke),
        pad ("Shaker", "perc-shaker"),
        pad ("Clave", "perc-clave", { { "send", 0.3f } }),
        pad ("Low Tom", "tom-electro", { { "transpose", -3.0f } }),
        pad ("Mid Tom", "tom-electro", { { "transpose", 2.0f } }),
        pad ("High Tom", "tom-electro", { { "transpose", 7.0f } }),
        pad ("China", "cymbal-china", { { "send", 0.2f } }),
        pad ("Cowbell", "perc-cowbell", { { "transpose", -3.0f } }),
        pad ("Bongo", "perc-bongo"),
        pad ("Sweep", "fx-noise-sweep"),
        pad ("Glitch", "fx-glitch"),
    } },
    fx ("Dub Echo", EffectType::delay), fx ("Squash", EffectType::dynamics)));

    presets.push_back (kit ("Heavy Kit",
    { {
        pad ("Kick", "kick-distorted"),
        pad ("Snare", "snare-fat", { { "send", 0.2f } }),
        pad ("Clap", "clap-big", { { "send", 0.2f } }),
        pad ("Rim", "perc-rim", { { "fxType", fxType ("Ring Mod") }, { "fx1", 0.6f }, { "fx2", 0.5f } }),
        pad ("Closed Hat", "hat-closed", choke),
        pad ("Open Hat", "hat-open", with (choke, { { "decay", 0.7f } })),
        pad ("Shaker", "perc-shaker"),
        pad ("Block", "perc-woodblock", { { "fxType", fxType ("FM") }, { "fx1", 0.4f }, { "fx2", 0.6f } }),
        pad ("Low Tom", "tom-floor", { { "fxType", fxType ("Punch") }, { "fx1", 0.5f } }),
        pad ("Mid Tom", "tom-low", { { "fxType", fxType ("Punch") }, { "fx1", 0.5f } }),
        pad ("High Tom", "tom-mid", { { "fxType", fxType ("Punch") }, { "fx1", 0.5f } }),
        pad ("China", "cymbal-china"),
        pad ("Cowbell", "perc-cowbell"),
        pad ("Conga", "perc-conga-low"),
        pad ("Impact", "fx-impact", { { "send", 0.2f } }),
        pad ("Zap", "fx-zap"),
    } },
    fx ("Big Room", EffectType::reverb), fx ("Distortion", EffectType::saturator)));

    presets.push_back (kit ("Glitch Kit",
    { {
        pad ("Kick", "kick-punch", { { "fxType", fxType ("8-Bit") }, { "fx1", 0.45f }, { "fx2", 0.2f } }),
        pad ("Snare", "snare-electro", { { "fxType", fxType ("Stretch") }, { "fx1", 0.6f }, { "fx2", 0.2f }, { "send", 0.2f } }),
        pad ("Clap", "clap-classic", { { "fxType", fxType ("Loop") }, { "fx1", 0.25f }, { "fx2", 0.1f },
                                       { "decay", 0.35f }, { "send", 0.25f } }),
        pad ("Rim", "perc-rim", { { "fxType", fxType ("Ring Mod") }, { "fx1", 0.7f }, { "fx2", 0.8f } }),
        pad ("Closed Hat", "hat-closed", with (choke, { { "fxType", fxType ("FM") }, { "fx1", 0.5f }, { "fx2", 0.7f } })),
        pad ("Open Hat", "hat-open-short", with (choke, { { "fxType", fxType ("8-Bit") }, { "fx1", 0.6f }, { "fx2", 0.5f } })),
        pad ("Shaker", "perc-shaker", { { "fxType", fxType ("Noise") }, { "fx1", 0.5f }, { "fx2", 0.8f } }),
        pad ("Blip", "fx-blip", { { "send", 0.4f } }),
        pad ("Low Tom", "tom-electro", { { "fxType", fxType ("Pitch Env") }, { "fx1", 0.8f }, { "fx2", 0.3f }, { "transpose", -5.0f } }),
        pad ("Mid Tom", "tom-electro", { { "fxType", fxType ("Pitch Env") }, { "fx1", 0.8f }, { "fx2", 0.3f } }),
        pad ("High Tom", "tom-electro", { { "fxType", fxType ("Pitch Env") }, { "fx1", 0.8f }, { "fx2", 0.3f }, { "transpose", 5.0f } }),
        pad ("Reverse", "cymbal-reverse"),
        pad ("Cowbell", "perc-cowbell", { { "fxType", fxType ("Ring Mod") }, { "fx1", 0.4f }, { "fx2", 0.6f } }),
        pad ("Conga", "perc-conga-low", { { "fxType", fxType ("Sub Osc") }, { "fx1", 0.5f }, { "fx2", 0.4f } }),
        pad ("Glitch", "fx-glitch"),
        pad ("Laser", "fx-laser", { { "send", 0.3f } }),
    } },
    fx ("Stutter Echo", EffectType::delay), fx ("Bitcrush", EffectType::crusher)));

    // Hand percussion has its own layout: drums low to high, then shakers and metal.
    presets.push_back (kit ("Hand Percussion",
    { {
        pad ("Low Conga", "perc-conga-low", { { "pan", -0.3f } }),
        pad ("High Conga", "perc-conga-high", { { "pan", -0.15f } }),
        pad ("Muted Conga", "perc-conga-high", { { "pan", -0.15f }, { "hold", 0.03f }, { "decay", 0.08f } }),
        pad ("Slap Conga", "perc-conga-high", { { "pan", -0.15f }, { "transpose", 3.0f }, { "fxType", fxType ("Punch") }, { "fx1", 0.6f }, { "fx2", 0.2f } }),
        pad ("Low Bongo", "perc-bongo", { { "pan", 0.2f }, { "transpose", -3.0f } }),
        pad ("High Bongo", "perc-bongo", { { "pan", 0.3f }, { "transpose", 2.0f } }),
        pad ("Shaker", "perc-shaker", { { "pan", 0.4f } }),
        pad ("Tambourine", "perc-tambourine", { { "pan", -0.4f } }),
        pad ("Clave", "perc-clave", { { "pan", 0.35f } }),
        pad ("Woodblock", "perc-woodblock", { { "pan", -0.35f } }),
        pad ("Cowbell", "perc-cowbell", { { "pan", 0.2f } }),
        pad ("Snap", "perc-snap", { { "pan", 0.1f } }),
        pad ("Rim", "perc-rim"),
        pad ("Low Tom", "tom-low", { { "pan", -0.2f } }),
        pad ("Triangle", "perc-triangle", with (choke, { { "pan", 0.25f } })),
        pad ("Muted Triangle", "perc-triangle", with (choke, { { "pan", 0.25f }, { "hold", 0.02f }, { "decay", 0.12f } })),
    } },
    fx ("Wood Room", EffectType::reverb), fx ("Kit EQ", EffectType::eq),
    { { "volume", -1.0f } }));
}

} // namespace rb
