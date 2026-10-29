namespace rb
{

namespace analog_presets
{
// Parameter ids and choice indices are those of AnalogSynth::createSpec(). Choice values:
// osc*Octave 0..4 = -2..+2, filterType 0 LP12 / 1 LP24 / 2 BP / 3 HP, voiceMode 0 Poly / 1 Mono /
// 2 Legato, lfoShape 0 Sine / 1 Tri / 2 Saw Up / 3 Saw Down / 4 Square / 5 Random,
// lfoDest 0 Pitch / 1 Filter / 2 Amp / 3 Pulse Width / 4 Shape / 5 Osc Mix.
// Effects use their default settings (values empty).
static void add (std::vector<TrackPreset>& list, const char* name, const char* category, ParamValues values,
                 juce::StringArray macros, EffectType fx1, EffectType fx2)
{
    TrackPreset p;
    p.name = name;
    p.category = category;
    p.type = InstrumentType::analog;
    p.values = std::move (values);
    p.macros = std::move (macros);
    p.effects[0] = { getEffectDisplayName (fx1), fx1, {} };
    p.effects[1] = { getEffectDisplayName (fx2), fx2, {} };
    list.push_back (std::move (p));
}
} // namespace analog_presets

void addAnalogPresets (std::vector<TrackPreset>& presets)
{
    using analog_presets::add;
    using E = EffectType;

    // --- Bass -----------------------------------------------------------------------------
    add (presets, "Round Bottom", "Bass",
         { { "osc1Shape", 1.45f }, { "osc2Shape", 0.0f }, { "osc2Octave", 2 }, { "osc2Detune", 0.0f }, { "oscMix", 0.3f },
           { "filterType", 1 }, { "cutoff", 900.0f }, { "resonance", 0.1f }, { "filterEnv", 0.25f }, { "keyTrack", 0.3f },
           { "drive", 0.3f }, { "ampAttack", 0.002f }, { "ampDecay", 0.6f }, { "ampSustain", 0.8f }, { "ampRelease", 0.12f },
           { "modDecay", 0.25f }, { "modSustain", 0.0f }, { "voiceMode", 1 }, { "glide", 0.03f }, { "drift", 0.15f } },
         { "cutoff", "filterEnv", "drive", "oscMix", "osc1Shape", "resonance", "ampRelease", "glide" },
         E::saturator, E::eq);

    add (presets, "Rubber Saw", "Bass",
         { { "osc1Shape", 2.0f }, { "osc2Shape", 3.0f }, { "osc2Detune", 6.0f }, { "oscMix", 0.35f }, { "subLevel", 0.5f },
           { "filterType", 1 }, { "cutoff", 450.0f }, { "resonance", 0.35f }, { "filterEnv", 0.55f }, { "keyTrack", 0.4f },
           { "drive", 0.2f }, { "ampDecay", 0.5f }, { "ampSustain", 0.7f }, { "ampRelease", 0.1f },
           { "modDecay", 0.22f }, { "modSustain", 0.15f }, { "voiceMode", 1 }, { "glide", 0.04f } },
         { "cutoff", "resonance", "filterEnv", "modDecay", "drive", "subLevel", "oscMix", "glide" },
         E::saturator, E::dynamics);

    add (presets, "Acid Drip", "Bass",
         { { "osc1Shape", 2.0f }, { "oscMix", 0.0f }, { "filterType", 1 }, { "cutoff", 450.0f }, { "resonance", 0.78f },
           { "filterEnv", 0.6f }, { "keyTrack", 0.3f }, { "drive", 0.45f }, { "ampDecay", 1.0f }, { "ampSustain", 0.85f },
           { "ampRelease", 0.08f }, { "modDecay", 0.18f }, { "modSustain", 0.0f }, { "velToFilter", 0.6f },
           { "voiceMode", 2 }, { "glide", 0.08f }, { "drift", 0.1f } },
         { "cutoff", "resonance", "filterEnv", "modDecay", "drive", "glide", "velToFilter", "osc1Shape" },
         E::saturator, E::delay);

    add (presets, "Sub Pressure", "Bass",
         { { "osc1Shape", 0.0f }, { "osc2Shape", 1.0f }, { "osc2Octave", 3 }, { "oscMix", 0.15f }, { "osc2Detune", 0.0f },
           { "filterType", 0 }, { "cutoff", 900.0f }, { "resonance", 0.0f }, { "filterEnv", 0.1f }, { "drive", 0.4f },
           { "ampAttack", 0.003f }, { "ampDecay", 1.5f }, { "ampSustain", 0.9f }, { "ampRelease", 0.15f },
           { "voiceMode", 1 }, { "drift", 0.05f } },
         { "drive", "oscMix", "cutoff", "osc2Shape", "ampDecay", "ampRelease", "glide", "filterEnv" },
         E::saturator, E::eq);

    // --- Keys -----------------------------------------------------------------------------
    add (presets, "Velvet Keys", "Keys",
         { { "osc1Shape", 0.6f }, { "osc2Shape", 1.0f }, { "osc2Octave", 3 }, { "oscMix", 0.25f }, { "fmAmount", 0.12f },
           { "modEnvFm", 0.35f }, { "filterType", 0 }, { "cutoff", 2500.0f }, { "filterEnv", 0.2f },
           { "ampDecay", 2.5f }, { "ampSustain", 0.25f }, { "ampRelease", 0.5f }, { "modDecay", 0.35f },
           { "modSustain", 0.0f }, { "velToFilter", 0.5f }, { "velToAmp", 0.6f }, { "drift", 0.2f } },
         { "osc1Shape", "modEnvFm", "cutoff", "ampDecay", "ampRelease", "oscMix", "velToAmp", "drift" },
         E::chorus, E::reverb);

    add (presets, "Felt Poly", "Keys",
         { { "osc1Shape", 2.0f }, { "osc2Shape", 2.0f }, { "osc2Detune", 10.0f }, { "filterType", 1 }, { "cutoff", 1200.0f },
           { "resonance", 0.15f }, { "filterEnv", 0.35f }, { "modDecay", 0.6f }, { "modSustain", 0.2f },
           { "ampAttack", 0.005f }, { "ampDecay", 1.2f }, { "ampSustain", 0.6f }, { "ampRelease", 0.4f },
           { "spread", 0.5f }, { "drift", 0.35f } },
         { "cutoff", "resonance", "filterEnv", "osc2Detune", "ampAttack", "ampRelease", "drift", "spread" },
         E::chorus, E::reverb);

    add (presets, "Reed Organ", "Keys",
         { { "osc1Shape", 3.0f }, { "osc2Shape", 3.0f }, { "osc2Octave", 3 }, { "osc2Detune", 2.0f }, { "oscMix", 0.4f },
           { "pulseWidth", 0.42f }, { "filterType", 0 }, { "cutoff", 3000.0f }, { "filterEnv", 0.0f },
           { "ampAttack", 0.01f }, { "ampSustain", 1.0f }, { "ampRelease", 0.08f }, { "lfoDest", 0 },
           { "lfoRate", 6.0f }, { "lfoAmount", 0.12f }, { "lfoFade", 0.3f }, { "velToAmp", 0.2f },
           { "volume", -4.0f } },
         { "oscMix", "pulseWidth", "cutoff", "lfoAmount", "lfoRate", "osc2Octave", "ampRelease", "drift" },
         E::phaser, E::reverb);

    // --- Lead -----------------------------------------------------------------------------
    add (presets, "Glide Lead", "Lead",
         { { "osc1Shape", 2.0f }, { "osc2Shape", 2.0f }, { "osc2Detune", 12.0f }, { "filterType", 1 }, { "cutoff", 2200.0f },
           { "resonance", 0.3f }, { "filterEnv", 0.25f }, { "voiceMode", 2 }, { "glide", 0.12f }, { "lfoDest", 0 },
           { "lfoRate", 5.5f }, { "lfoAmount", 0.15f }, { "lfoFade", 0.6f }, { "ampRelease", 0.2f }, { "ampSustain", 0.85f } },
         { "cutoff", "resonance", "glide", "lfoAmount", "osc2Detune", "filterEnv", "ampRelease", "drive" },
         E::delay, E::reverb);

    add (presets, "Square Hero", "Lead",
         { { "osc1Shape", 3.0f }, { "osc2Shape", 3.0f }, { "osc2Detune", 5.0f }, { "pulseWidth", 0.35f }, { "lfoShape", 1 },
           { "lfoDest", 3 }, { "lfoRate", 0.8f }, { "lfoAmount", 0.5f }, { "filterType", 0 }, { "cutoff", 3500.0f },
           { "resonance", 0.2f }, { "filterEnv", 0.15f }, { "voiceMode", 1 }, { "glide", 0.04f }, { "ampSustain", 0.9f },
           { "ampRelease", 0.15f } },
         { "pulseWidth", "lfoAmount", "lfoRate", "cutoff", "resonance", "glide", "osc2Detune", "ampRelease" },
         E::chorus, E::delay);

    add (presets, "Whistle Tip", "Lead",
         { { "osc1Shape", 0.0f }, { "osc2Shape", 1.0f }, { "osc2Octave", 3 }, { "oscMix", 0.15f }, { "noiseLevel", 0.05f },
           { "filterType", 0 }, { "cutoff", 5000.0f }, { "filterEnv", 0.0f }, { "lfoDest", 0 }, { "lfoRate", 5.0f },
           { "lfoAmount", 0.18f }, { "lfoFade", 0.4f }, { "ampAttack", 0.03f }, { "ampSustain", 1.0f },
           { "ampRelease", 0.25f }, { "voiceMode", 2 }, { "glide", 0.06f }, { "transpose", 12.0f } },
         { "lfoAmount", "lfoRate", "noiseLevel", "glide", "oscMix", "ampAttack", "ampRelease", "osc2Shape" },
         E::delay, E::reverb);

    // --- Pad ------------------------------------------------------------------------------
    add (presets, "Slow Bloom", "Pad",
         { { "osc1Shape", 2.0f }, { "osc2Shape", 2.0f }, { "osc2Detune", 14.0f }, { "filterType", 1 }, { "cutoff", 700.0f },
           { "resonance", 0.2f }, { "filterEnv", 0.3f }, { "modAttack", 1.5f }, { "modDecay", 3.0f }, { "modSustain", 0.6f },
           { "modRelease", 3.0f }, { "ampAttack", 1.2f }, { "ampDecay", 2.0f }, { "ampSustain", 0.85f },
           { "ampRelease", 3.5f }, { "lfoDest", 1 }, { "lfoRate", 0.2f }, { "lfoAmount", 0.15f }, { "drift", 0.5f },
           { "spread", 0.8f }, { "velToFilter", 0.1f } },
         { "cutoff", "filterEnv", "ampAttack", "ampRelease", "osc2Detune", "lfoAmount", "drift", "spread" },
         E::chorus, E::reverb);

    add (presets, "Night Strings", "Pad",
         { { "osc1Shape", 3.0f }, { "osc2Shape", 2.0f }, { "osc2Detune", 9.0f }, { "pulseWidth", 0.3f }, { "lfoShape", 1 },
           { "lfoDest", 3 }, { "lfoRate", 0.4f }, { "lfoAmount", 0.35f }, { "filterType", 0 }, { "cutoff", 2000.0f },
           { "filterEnv", 0.1f }, { "ampAttack", 0.6f }, { "ampDecay", 2.0f }, { "ampSustain", 0.9f },
           { "ampRelease", 2.8f }, { "spread", 0.7f }, { "drift", 0.4f } },
         { "cutoff", "ampAttack", "ampRelease", "pulseWidth", "lfoAmount", "osc2Detune", "spread", "drift" },
         E::chorus, E::reverb);

    // --- Pluck ----------------------------------------------------------------------------
    add (presets, "Crisp Pluck", "Pluck",
         { { "osc1Shape", 2.0f }, { "osc2Shape", 3.0f }, { "osc2Octave", 3 }, { "oscMix", 0.3f }, { "filterType", 1 },
           { "cutoff", 400.0f }, { "resonance", 0.25f }, { "filterEnv", 0.7f }, { "modDecay", 0.18f }, { "modSustain", 0.0f },
           { "ampDecay", 0.35f }, { "ampSustain", 0.0f }, { "ampRelease", 0.3f }, { "velToFilter", 0.5f },
           { "volume", 2.0f } },
         { "cutoff", "filterEnv", "modDecay", "resonance", "ampDecay", "oscMix", "velToFilter", "spread" },
         E::delay, E::reverb);

    add (presets, "Wooden Pluck", "Pluck",
         { { "osc1Shape", 1.0f }, { "osc2Shape", 0.0f }, { "osc2Octave", 4 }, { "oscMix", 0.0f }, { "fmAmount", 0.2f },
           { "modEnvFm", 0.4f }, { "filterType", 0 }, { "cutoff", 1200.0f }, { "filterEnv", 0.5f }, { "modDecay", 0.08f },
           { "modSustain", 0.0f }, { "ampDecay", 0.25f }, { "ampSustain", 0.0f }, { "ampRelease", 0.2f },
           { "volume", 2.0f } },
         { "fmAmount", "modEnvFm", "cutoff", "filterEnv", "ampDecay", "osc1Shape", "modDecay", "ampRelease" },
         E::eq, E::reverb);

    add (presets, "Bubble Pluck", "Pluck",
         { { "osc1Shape", 0.3f }, { "oscMix", 0.0f }, { "modEnvPitch", 12.0f }, { "modDecay", 0.04f }, { "modSustain", 0.0f },
           { "filterType", 0 }, { "cutoff", 800.0f }, { "resonance", 0.6f }, { "filterEnv", 0.5f },
           { "ampDecay", 0.3f }, { "ampSustain", 0.0f }, { "ampRelease", 0.25f }, { "volume", 2.0f } },
         { "modEnvPitch", "modDecay", "cutoff", "resonance", "filterEnv", "ampDecay", "osc1Shape", "ampRelease" },
         E::delay, E::reverb);

    // --- Mallets --------------------------------------------------------------------------
    add (presets, "Soft Marimba", "Mallets",
         { { "osc1Shape", 0.0f }, { "osc2Shape", 0.0f }, { "osc2Octave", 4 }, { "osc2Detune", 0.0f }, { "oscMix", 0.0f },
           { "fmAmount", 0.05f }, { "modEnvFm", 0.5f }, { "modDecay", 0.05f }, { "modSustain", 0.0f },
           { "filterType", 0 }, { "cutoff", 6000.0f }, { "filterEnv", 0.0f }, { "ampDecay", 0.5f }, { "ampSustain", 0.0f },
           { "ampRelease", 0.4f }, { "drift", 0.0f }, { "velToAmp", 0.7f } },
         { "modEnvFm", "modDecay", "ampDecay", "fmAmount", "cutoff", "osc2Octave", "ampRelease", "velToAmp" },
         E::eq, E::reverb);

    // --- FX -------------------------------------------------------------------------------
    add (presets, "Laser Zap", "FX",
         { { "osc1Shape", 2.0f }, { "osc2Shape", 3.0f }, { "osc2Semi", 7.0f }, { "modEnvPitch", 24.0f },
           { "modDecay", 0.15f }, { "modSustain", 0.0f }, { "filterType", 0 }, { "cutoff", 3000.0f },
           { "resonance", 0.5f }, { "filterEnv", 0.3f }, { "ampDecay", 0.4f }, { "ampSustain", 0.0f }, { "ampRelease", 0.3f } },
         { "modEnvPitch", "modDecay", "cutoff", "resonance", "osc2Semi", "ampDecay", "filterEnv", "oscMix" },
         E::crusher, E::delay);

    add (presets, "Storm Riser", "FX",
         { { "osc1Shape", 2.0f }, { "oscMix", 0.3f }, { "noiseLevel", 0.8f }, { "filterType", 2 }, { "cutoff", 1500.0f },
           { "resonance", 0.6f }, { "filterEnv", 0.0f }, { "lfoShape", 2 }, { "lfoDest", 1 }, { "lfoRate", 0.15f },
           { "lfoAmount", 0.5f }, { "lfoRetrig", 1 }, { "ampAttack", 1.0f }, { "ampSustain", 1.0f }, { "ampRelease", 2.0f },
           { "spread", 0.9f } },
         { "cutoff", "resonance", "lfoRate", "lfoAmount", "noiseLevel", "ampAttack", "ampRelease", "filterType" },
         E::phaser, E::reverb);
}

} // namespace rb
