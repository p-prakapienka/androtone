namespace rb
{

// Factory effect presets (DEV-23). Values are by parameter id (see devices/effects/*.cpp);
// anything not listed takes the spec default. The first preset of each type is its default.
void addEffectPresets (std::vector<EffectPreset>& presets)
{
    const auto add = [&presets] (const char* name, EffectType type, ParamValues values)
    {
        presets.push_back ({ name, type, std::move (values) });
    };

    //==========================================================================
    // Auto Filter. lfoDivision: 0 "4 Bars", 1 "2 Bars", 2 "1/1", 3 "1/2", 5 "1/4", 7 "1/8", 9 "1/16".
    // type: 0 LP, 1 LP24, 2 BP, 3 HP, 4 HP24, 5 Notch. lfoShape: 0 sine ... 2 saw up, 5 random.
    add ("Slow Sweep", EffectType::autoFilter,
         { { "type", 0.0f }, { "cutoff", 900.0f }, { "resonance", 0.35f }, { "lfoAmount", 0.45f },
           { "lfoSync", 1.0f }, { "lfoDivision", 1.0f }, { "lfoShape", 0.0f }, { "lfoSpread", 0.25f }, { "mix", 1.0f } });
    add ("Envelope Wah", EffectType::autoFilter,
         { { "type", 2.0f }, { "cutoff", 500.0f }, { "resonance", 0.6f }, { "envelope", 0.7f },
           { "lfoAmount", 0.0f }, { "mix", 1.0f } });
    add ("Random Steps", EffectType::autoFilter,
         { { "type", 1.0f }, { "cutoff", 700.0f }, { "resonance", 0.45f }, { "lfoAmount", 0.55f },
           { "lfoSync", 1.0f }, { "lfoDivision", 9.0f }, { "lfoShape", 5.0f }, { "mix", 1.0f } });
    add ("Dark Drive", EffectType::autoFilter,
         { { "type", 1.0f }, { "cutoff", 450.0f }, { "resonance", 0.25f }, { "drive", 9.0f },
           { "lfoAmount", 0.0f }, { "mix", 1.0f } });
    add ("Rising Highpass", EffectType::autoFilter,
         { { "type", 3.0f }, { "cutoff", 350.0f }, { "resonance", 0.3f }, { "lfoAmount", 0.35f },
           { "lfoSync", 1.0f }, { "lfoDivision", 0.0f }, { "lfoShape", 2.0f }, { "mix", 1.0f } });
    add ("Stereo Wobble", EffectType::autoFilter,
         { { "type", 0.0f }, { "cutoff", 1100.0f }, { "resonance", 0.5f }, { "lfoAmount", 0.5f },
           { "lfoSync", 1.0f }, { "lfoDivision", 7.0f }, { "lfoShape", 1.0f }, { "lfoSpread", 0.5f }, { "mix", 1.0f } });

    //==========================================================================
    // EQ
    add ("Flat", EffectType::eq, {});
    add ("Bass Cut", EffectType::eq, { { "lowCut", 180.0f } });
    add ("Air", EffectType::eq, { { "high", 6.0f }, { "mid", -1.5f }, { "midFreq", 400.0f } });
    add ("Telephone", EffectType::eq,
         { { "lowCut", 500.0f }, { "low", -15.0f }, { "mid", 8.0f }, { "midFreq", 1500.0f }, { "high", -15.0f }, { "output", -3.0f } });
    add ("Warmth", EffectType::eq, { { "low", 3.0f }, { "mid", -2.0f }, { "midFreq", 2500.0f }, { "high", -3.0f } });
    add ("Presence", EffectType::eq, { { "lowCut", 60.0f }, { "mid", 4.0f }, { "midFreq", 3000.0f }, { "high", 2.0f } });

    //==========================================================================
    // Chorus. mode: 0 Chorus, 1 Ensemble, 2 Vibrato.
    add ("Classic Chorus", EffectType::chorus, { { "mode", 0.0f }, { "rate", 0.6f }, { "amount", 0.5f }, { "mix", 0.5f } });
    add ("Wide Ensemble", EffectType::chorus, { { "mode", 1.0f }, { "rate", 0.4f }, { "amount", 0.6f }, { "mix", 0.5f } });
    add ("Gentle Vibrato", EffectType::chorus, { { "mode", 2.0f }, { "rate", 5.0f }, { "amount", 0.3f }, { "mix", 1.0f } });
    add ("Watery", EffectType::chorus,
         { { "mode", 0.0f }, { "rate", 1.8f }, { "amount", 0.8f }, { "feedback", 0.5f }, { "mix", 0.5f } });
    add ("Mono Thickener", EffectType::chorus, { { "mode", 1.0f }, { "rate", 0.25f }, { "amount", 0.35f }, { "width", 0.0f }, { "mix", 0.4f } });

    //==========================================================================
    // Delay. division: 2 "1/16", 5 "1/8", 6 "1/8D", 8 "1/4", 9 "1/4D".
    add ("Eighth Echo", EffectType::delay,
         { { "sync", 1.0f }, { "division", 5.0f }, { "feedback", 0.35f }, { "filterFreq", 1000.0f },
           { "filterWidth", 0.75f }, { "modulation", 0.1f }, { "mix", 0.3f } });
    add ("Dotted Ping Pong", EffectType::delay,
         { { "sync", 1.0f }, { "division", 6.0f }, { "feedback", 0.45f }, { "pingPong", 1.0f },
           { "filterFreq", 1500.0f }, { "filterWidth", 0.6f }, { "mix", 0.3f } });
    add ("Tape Slap", EffectType::delay,
         { { "sync", 0.0f }, { "time", 0.11f }, { "feedback", 0.15f }, { "filterFreq", 1800.0f },
           { "filterWidth", 0.45f }, { "modulation", 0.35f }, { "mix", 0.3f } });
    add ("Dub Throw", EffectType::delay,
         { { "sync", 1.0f }, { "division", 9.0f }, { "feedback", 0.75f }, { "pingPong", 1.0f },
           { "filterFreq", 900.0f }, { "filterWidth", 0.4f }, { "modulation", 0.2f }, { "mix", 0.4f } });
    add ("Sixteenth Ripple", EffectType::delay,
         { { "sync", 1.0f }, { "division", 2.0f }, { "feedback", 0.5f }, { "filterFreq", 3000.0f },
           { "filterWidth", 0.5f }, { "mix", 0.25f } });

    //==========================================================================
    // Dynamics
    add ("Glue", EffectType::dynamics,
         { { "threshold", -18.0f }, { "ratio", 2.0f }, { "attack", 0.01f }, { "release", 0.2f }, { "makeup", 1.0f } });
    add ("Punch", EffectType::dynamics,
         { { "threshold", -20.0f }, { "ratio", 4.0f }, { "attack", 0.02f }, { "release", 0.08f },
           { "makeup", 1.0f }, { "low", 2.0f }, { "high", 1.0f } });
    add ("Squash", EffectType::dynamics,
         { { "threshold", -35.0f }, { "ratio", 10.0f }, { "attack", 0.001f }, { "release", 0.1f }, { "makeup", 1.0f } });
    add ("Gentle Level", EffectType::dynamics,
         { { "threshold", -12.0f }, { "ratio", 1.5f }, { "attack", 0.03f }, { "release", 0.4f }, { "makeup", 1.0f } });
    add ("Drum Bus", EffectType::dynamics,
         { { "threshold", -22.0f }, { "ratio", 3.0f }, { "attack", 0.005f }, { "release", 0.12f },
           { "makeup", 1.0f }, { "low", 2.0f }, { "high", 2.0f } });

    //==========================================================================
    // Phaser. mode: 0 Phaser, 1 Flanger, 2 Doubler.
    add ("Slow Phase", EffectType::phaser,
         { { "mode", 0.0f }, { "rate", 0.25f }, { "amount", 0.6f }, { "feedback", 0.4f }, { "frequency", 700.0f },
           { "spread", 0.5f }, { "mix", 0.5f } });
    add ("Jet Flanger", EffectType::phaser,
         { { "mode", 1.0f }, { "rate", 0.15f }, { "amount", 0.8f }, { "feedback", 0.75f }, { "time", 0.004f }, { "mix", 0.5f } });
    add ("Wide Doubler", EffectType::phaser,
         { { "mode", 2.0f }, { "rate", 0.5f }, { "amount", 0.3f }, { "time", 0.008f }, { "spread", 0.7f }, { "mix", 0.5f } });
    add ("Swirl", EffectType::phaser,
         { { "mode", 0.0f }, { "rate", 2.5f }, { "amount", 0.5f }, { "feedback", 0.6f }, { "frequency", 1200.0f },
           { "spread", 0.8f }, { "mix", 0.5f } });
    add ("Metal Comb", EffectType::phaser,
         { { "mode", 1.0f }, { "rate", 0.05f }, { "amount", 0.3f }, { "feedback", -0.85f }, { "time", 0.0015f }, { "mix", 0.5f } });

    //==========================================================================
    // Crusher. filter: 0 Off, 1 Pre, 2 Post.
    add ("Vintage Sampler", EffectType::crusher,
         { { "bits", 12.0f }, { "rate", 22000.0f }, { "filter", 2.0f }, { "cutoff", 9000.0f }, { "mix", 1.0f } });
    add ("Eight Bit", EffectType::crusher, { { "bits", 8.0f }, { "rate", 11025.0f }, { "filter", 0.0f }, { "mix", 1.0f } });
    add ("Crunch Dust", EffectType::crusher,
         { { "bits", 6.0f }, { "rate", 8000.0f }, { "jitter", 0.3f }, { "filter", 2.0f }, { "cutoff", 5000.0f }, { "mix", 0.7f } });
    add ("Radio Static", EffectType::crusher,
         { { "bits", 4.0f }, { "rate", 4000.0f }, { "jitter", 0.6f }, { "filter", 1.0f }, { "cutoff", 3000.0f }, { "mix", 1.0f } });
    add ("Gentle Grit", EffectType::crusher, { { "bits", 10.0f }, { "rate", 30000.0f }, { "filter", 0.0f }, { "mix", 0.5f } });

    //==========================================================================
    // Reverb
    add ("Small Room", EffectType::reverb,
         { { "preDelay", 0.005f }, { "size", 0.15f }, { "decay", 0.6f }, { "diffusion", 0.7f },
           { "highCut", 8000.0f }, { "lowCut", 100.0f }, { "width", 0.8f }, { "mix", 0.25f } });
    add ("Plate", EffectType::reverb,
         { { "preDelay", 0.01f }, { "size", 0.4f }, { "decay", 1.8f }, { "diffusion", 0.9f },
           { "highCut", 12000.0f }, { "lowCut", 150.0f }, { "width", 1.0f }, { "mix", 0.3f } });
    add ("Large Hall", EffectType::reverb,
         { { "preDelay", 0.03f }, { "size", 0.85f }, { "decay", 4.0f }, { "diffusion", 0.8f },
           { "highCut", 7000.0f }, { "lowCut", 80.0f }, { "width", 1.0f }, { "mix", 0.3f } });
    add ("Dark Space", EffectType::reverb,
         { { "preDelay", 0.05f }, { "size", 1.0f }, { "decay", 8.0f }, { "diffusion", 0.85f },
           { "highCut", 2500.0f }, { "lowCut", 60.0f }, { "width", 1.0f }, { "mix", 0.35f } });
    add ("Shimmerless Ambience", EffectType::reverb,
         { { "preDelay", 0.08f }, { "size", 0.7f }, { "decay", 6.0f }, { "diffusion", 1.0f },
           { "highCut", 10000.0f }, { "lowCut", 300.0f }, { "width", 1.0f }, { "mix", 0.4f } });

    //==========================================================================
    // Saturator. curve: 0 Soft, 1 Analog, 2 Tape, 3 Hard, 4 Fold, 5 Bit.
    add ("Warm Tape", EffectType::saturator, { { "curve", 2.0f }, { "drive", 8.0f }, { "colour", -0.2f }, { "output", -3.0f } });
    add ("Crunch", EffectType::saturator, { { "curve", 1.0f }, { "drive", 18.0f }, { "colour", 0.2f }, { "output", -8.0f } });
    add ("Fuzz", EffectType::saturator, { { "curve", 3.0f }, { "drive", 30.0f }, { "colour", 0.3f }, { "output", -12.0f } });
    add ("Wavefolder", EffectType::saturator, { { "curve", 4.0f }, { "drive", 12.0f }, { "output", -4.0f }, { "mix", 0.8f } });
    add ("Soft Clip", EffectType::saturator, { { "curve", 0.0f }, { "drive", 4.0f }, { "output", -1.0f } });
    add ("Digital Grit", EffectType::saturator, { { "curve", 5.0f }, { "drive", 10.0f }, { "output", -4.0f }, { "mix", 0.6f } });
}

} // namespace rb
