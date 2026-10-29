namespace rb
{

namespace wave_presets
{
// Parameter ids and choice indices are those of WaveSynth::createSpec(). Choice values:
// osc*Table 0 Basic / 1 Harmonics / 2 Vocal / 3 PWM / 4 Bell / 5 Digital / 6 Sync / 7 Organ,
// osc*Octave 0..4 = -2..+2, filterType 0 LP12 / 1 LP24 / 2 BP / 3 HP, voiceMode 0 Poly / 1 Mono /
// 2 Legato, unison 0..6 = 1..7 voices, lfoShape 0 Sine / 1 Tri / 2 Saw Up / 3 Saw Down /
// 4 Square / 5 Random, lfoDest 0 Position / 1 Position 1 / 2 Position 2 / 3 Pitch / 4 Filter / 5 Amp.
// Effects use their default settings (values empty).
static void add (std::vector<TrackPreset>& list, const char* name, const char* category, ParamValues values,
                 juce::StringArray macros, EffectType fx1, EffectType fx2)
{
    TrackPreset p;
    p.name = name;
    p.category = category;
    p.type = InstrumentType::wave;
    p.values = std::move (values);
    p.macros = std::move (macros);
    p.effects[0] = { getEffectDisplayName (fx1), fx1, {} };
    p.effects[1] = { getEffectDisplayName (fx2), fx2, {} };
    list.push_back (std::move (p));
}
} // namespace wave_presets

void addWavePresets (std::vector<TrackPreset>& presets)
{
    using wave_presets::add;
    using E = EffectType;

    // --- Bass -----------------------------------------------------------------------------
    add (presets, "Growl Table", "Bass",
         { { "osc1Table", 6 }, { "osc1Pos", 0.2f }, { "osc1Level", 0.8f }, { "osc2Table", 0 }, { "osc2Pos", 0.0f },
           { "osc2Octave", 1 }, { "osc2Detune", 0.0f }, { "osc2Level", 0.6f }, { "lfoShape", 1 }, { "lfoDest", 1 },
           { "lfoRate", 2.0f }, { "lfoAmount", 0.35f }, { "lfoRetrig", 1 }, { "filterType", 1 }, { "cutoff", 900.0f },
           { "resonance", 0.3f }, { "filterEnv", 0.3f }, { "modDecay", 0.3f }, { "modSustain", 0.1f }, { "drive", 0.3f },
           { "ampSustain", 0.85f }, { "ampRelease", 0.12f }, { "voiceMode", 1 }, { "glide", 0.03f } },
         { "osc1Pos", "lfoAmount", "lfoRate", "cutoff", "resonance", "drive", "osc2Level", "glide" },
         E::saturator, E::dynamics);

    add (presets, "Vowel Bass", "Bass",
         { { "osc1Table", 2 }, { "osc1Pos", 0.1f }, { "osc1Level", 0.8f }, { "osc2Level", 0.0f }, { "subLevel", 0.6f },
           { "modEnvPos", 0.5f }, { "modDecay", 0.4f }, { "modSustain", 0.2f }, { "filterType", 0 }, { "cutoff", 2000.0f },
           { "filterEnv", 0.15f }, { "resonance", 0.15f }, { "ampSustain", 0.85f }, { "ampRelease", 0.12f },
           { "voiceMode", 1 }, { "glide", 0.03f }, { "drive", 0.2f } },
         { "osc1Pos", "modEnvPos", "modDecay", "subLevel", "cutoff", "drive", "glide", "ampRelease" },
         E::saturator, E::eq);

    // --- Keys -----------------------------------------------------------------------------
    add (presets, "Glass Keys", "Keys",
         { { "osc1Table", 4 }, { "osc1Pos", 0.25f }, { "osc1Level", 0.7f }, { "osc2Table", 0 }, { "osc2Pos", 0.0f },
           { "osc2Octave", 3 }, { "osc2Detune", 3.0f }, { "osc2Level", 0.3f }, { "modEnvPos", -0.2f }, { "modDecay", 0.8f },
           { "modSustain", 0.0f }, { "filterType", 0 }, { "cutoff", 6000.0f }, { "filterEnv", 0.15f },
           { "ampDecay", 2.5f }, { "ampSustain", 0.2f }, { "ampRelease", 0.8f }, { "velToAmp", 0.6f } },
         { "osc1Pos", "modEnvPos", "osc2Level", "cutoff", "ampDecay", "ampRelease", "velToAmp", "unisonDetune" },
         E::chorus, E::reverb);

    add (presets, "Drawbar Organ", "Keys",
         { { "osc1Table", 7 }, { "osc1Pos", 0.6f }, { "osc1Level", 0.75f }, { "osc2Table", 7 }, { "osc2Pos", 0.3f },
           { "osc2Octave", 3 }, { "osc2Detune", 1.5f }, { "osc2Level", 0.25f }, { "filterType", 0 }, { "cutoff", 9000.0f },
           { "filterEnv", 0.0f }, { "ampAttack", 0.005f }, { "ampSustain", 1.0f }, { "ampRelease", 0.06f },
           { "lfoDest", 3 }, { "lfoRate", 6.5f }, { "lfoAmount", 0.1f }, { "velToAmp", 0.15f }, { "volume", -4.0f } },
         { "osc1Pos", "osc2Pos", "osc2Level", "lfoAmount", "lfoRate", "cutoff", "ampRelease", "drive" },
         E::chorus, E::reverb);

    // --- Lead -----------------------------------------------------------------------------
    add (presets, "Sync Scream", "Lead",
         { { "osc1Table", 6 }, { "osc1Pos", 0.1f }, { "osc1Level", 0.75f }, { "osc2Table", 0 }, { "osc2Pos", 0.67f },
           { "osc2Detune", 8.0f }, { "osc2Level", 0.3f }, { "modEnvPos", 0.7f }, { "modDecay", 0.6f }, { "modSustain", 0.3f },
           { "unison", 2 }, { "unisonDetune", 0.2f }, { "filterType", 1 }, { "cutoff", 5000.0f }, { "resonance", 0.2f },
           { "filterEnv", 0.1f }, { "lfoDest", 3 }, { "lfoRate", 5.5f }, { "lfoAmount", 0.12f }, { "lfoFade", 0.5f },
           { "voiceMode", 2 }, { "glide", 0.07f }, { "ampSustain", 0.9f }, { "ampRelease", 0.2f } },
         { "osc1Pos", "modEnvPos", "modDecay", "cutoff", "unisonDetune", "lfoAmount", "glide", "drive" },
         E::saturator, E::delay);

    add (presets, "Vox Lead", "Lead",
         { { "osc1Table", 2 }, { "osc1Pos", 0.0f }, { "osc1Level", 0.8f }, { "osc2Table", 3 }, { "osc2Pos", 0.3f },
           { "osc2Level", 0.3f }, { "osc2Detune", 6.0f }, { "lfoShape", 1 }, { "lfoDest", 1 }, { "lfoRate", 0.5f },
           { "lfoAmount", 0.6f }, { "filterType", 0 }, { "cutoff", 4500.0f }, { "filterEnv", 0.1f },
           { "ampAttack", 0.01f }, { "ampSustain", 0.9f }, { "ampRelease", 0.25f }, { "voiceMode", 2 }, { "glide", 0.05f } },
         { "osc1Pos", "lfoAmount", "lfoRate", "osc2Level", "cutoff", "glide", "ampAttack", "ampRelease" },
         E::chorus, E::delay);

    // --- Pad ------------------------------------------------------------------------------
    add (presets, "Choir Haze", "Pad",
         { { "osc1Table", 2 }, { "osc1Pos", 0.3f }, { "osc1Level", 0.75f }, { "osc2Table", 2 }, { "osc2Pos", 0.7f },
           { "osc2Octave", 3 }, { "osc2Level", 0.35f }, { "lfoShape", 1 }, { "lfoDest", 0 }, { "lfoRate", 0.12f },
           { "lfoAmount", 0.5f }, { "unison", 3 }, { "unisonDetune", 0.25f }, { "unisonSpread", 0.8f },
           { "filterType", 0 }, { "cutoff", 3500.0f }, { "filterEnv", 0.0f }, { "ampAttack", 1.0f }, { "ampDecay", 2.0f },
           { "ampSustain", 0.9f }, { "ampRelease", 3.5f } },
         { "osc1Pos", "osc2Pos", "lfoAmount", "lfoRate", "unisonDetune", "cutoff", "ampAttack", "ampRelease" },
         E::chorus, E::reverb);

    add (presets, "Wide Saws", "Pad",
         { { "osc1Table", 0 }, { "osc1Pos", 0.67f }, { "osc1Level", 0.8f }, { "osc2Table", 0 }, { "osc2Pos", 0.67f },
           { "osc2Octave", 3 }, { "osc2Level", 0.25f }, { "unison", 4 }, { "unisonDetune", 0.4f }, { "unisonSpread", 1.0f },
           { "filterType", 1 }, { "cutoff", 2500.0f }, { "resonance", 0.1f }, { "filterEnv", 0.1f },
           { "ampAttack", 0.4f }, { "ampDecay", 2.0f }, { "ampSustain", 0.9f }, { "ampRelease", 2.5f } },
         { "cutoff", "unisonDetune", "unisonSpread", "osc2Level", "resonance", "ampAttack", "ampRelease", "osc1Pos" },
         E::chorus, E::reverb);

    add (presets, "Harmonic Sky", "Pad",
         { { "osc1Table", 1 }, { "osc1Pos", 0.3f }, { "osc1Level", 0.75f }, { "osc2Table", 7 }, { "osc2Pos", 0.4f },
           { "osc2Octave", 3 }, { "osc2Level", 0.3f }, { "lfoShape", 0 }, { "lfoDest", 1 }, { "lfoRate", 0.08f },
           { "lfoAmount", 0.5f }, { "unison", 2 }, { "unisonDetune", 0.2f }, { "filterType", 0 }, { "cutoff", 4000.0f },
           { "filterEnv", 0.0f }, { "ampAttack", 1.5f }, { "ampDecay", 2.0f }, { "ampSustain", 0.9f },
           { "ampRelease", 4.0f } },
         { "osc1Pos", "lfoAmount", "lfoRate", "osc2Level", "cutoff", "unisonDetune", "ampAttack", "ampRelease" },
         E::chorus, E::reverb);

    // --- Pluck ----------------------------------------------------------------------------
    add (presets, "Bell Pluck", "Pluck",
         { { "osc1Table", 4 }, { "osc1Pos", 0.5f }, { "osc1Level", 0.8f }, { "osc2Level", 0.0f }, { "modEnvPos", -0.5f },
           { "modDecay", 0.3f }, { "modSustain", 0.0f }, { "filterType", 0 }, { "cutoff", 3000.0f }, { "filterEnv", 0.4f },
           { "ampDecay", 0.6f }, { "ampSustain", 0.0f }, { "ampRelease", 0.5f }, { "volume", 2.0f } },
         { "osc1Pos", "modEnvPos", "modDecay", "cutoff", "filterEnv", "ampDecay", "ampRelease", "unisonDetune" },
         E::delay, E::reverb);

    add (presets, "Digi Pluck", "Pluck",
         { { "osc1Table", 5 }, { "osc1Pos", 0.3f }, { "osc1Level", 0.75f }, { "osc2Table", 1 }, { "osc2Pos", 0.5f },
           { "osc2Level", 0.4f }, { "filterType", 1 }, { "cutoff", 500.0f }, { "resonance", 0.3f }, { "filterEnv", 0.65f },
           { "modDecay", 0.2f }, { "modSustain", 0.0f }, { "ampDecay", 0.4f }, { "ampSustain", 0.0f },
           { "ampRelease", 0.3f }, { "unison", 1 }, { "unisonDetune", 0.15f }, { "volume", 2.0f } },
         { "osc1Pos", "cutoff", "filterEnv", "modDecay", "resonance", "ampDecay", "osc2Level", "unisonDetune" },
         E::delay, E::reverb);

    // --- Mallets --------------------------------------------------------------------------
    add (presets, "Crystal Mallet", "Mallets",
         { { "osc1Table", 4 }, { "osc1Pos", 0.6f }, { "osc1Level", 0.7f }, { "osc2Table", 0 }, { "osc2Pos", 0.0f },
           { "osc2Octave", 4 }, { "osc2Detune", 0.0f }, { "osc2Level", 0.25f }, { "modEnvPos", -0.4f }, { "modDecay", 0.15f },
           { "modSustain", 0.0f }, { "filterType", 0 }, { "cutoff", 8000.0f }, { "filterEnv", 0.0f },
           { "ampAttack", 0.001f }, { "ampDecay", 1.2f }, { "ampSustain", 0.0f }, { "ampRelease", 1.0f },
           { "velToAmp", 0.7f } },
         { "osc1Pos", "modEnvPos", "modDecay", "osc2Level", "ampDecay", "ampRelease", "cutoff", "velToAmp" },
         E::chorus, E::reverb);

    add (presets, "Kalimba Tines", "Mallets",
         { { "osc1Table", 1 }, { "osc1Pos", 0.15f }, { "osc1Level", 0.8f }, { "osc2Table", 4 }, { "osc2Pos", 0.1f },
           { "osc2Octave", 3 }, { "osc2Detune", 0.0f }, { "osc2Level", 0.35f }, { "filterType", 0 }, { "cutoff", 2500.0f },
           { "filterEnv", 0.3f }, { "modDecay", 0.1f }, { "modSustain", 0.0f }, { "ampAttack", 0.001f },
           { "ampDecay", 0.7f }, { "ampSustain", 0.0f }, { "ampRelease", 0.6f }, { "velToAmp", 0.7f } },
         { "osc1Pos", "osc2Level", "osc2Pos", "cutoff", "filterEnv", "ampDecay", "ampRelease", "velToAmp" },
         E::eq, E::reverb);

    // --- FX -------------------------------------------------------------------------------
    add (presets, "Robot Talk", "FX",
         { { "osc1Table", 2 }, { "osc1Pos", 0.5f }, { "osc1Level", 0.8f }, { "osc2Table", 5 }, { "osc2Pos", 0.5f },
           { "osc2Level", 0.3f }, { "lfoShape", 5 }, { "lfoDest", 1 }, { "lfoRate", 6.0f }, { "lfoAmount", 1.0f },
           { "filterType", 0 }, { "cutoff", 4000.0f }, { "resonance", 0.3f }, { "filterEnv", 0.0f },
           { "ampSustain", 1.0f }, { "ampRelease", 0.2f } },
         { "lfoRate", "lfoAmount", "osc1Pos", "osc2Level", "cutoff", "resonance", "ampRelease", "lfoShape" },
         E::crusher, E::delay);
}

} // namespace rb
