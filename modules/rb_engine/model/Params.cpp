namespace rb
{

static const char* instrumentIds[] = { "analog", "wave", "drumKit", "sampler" };
static const char* effectIds[] = { "none", "autoFilter", "eq", "chorus", "delay",
                                   "dynamics", "phaser", "crusher", "reverb", "saturator" };

juce::String toTypeId (InstrumentType t) { return instrumentIds[(int) t]; }
juce::String toTypeId (EffectType t)     { return effectIds[(int) t]; }

InstrumentType instrumentTypeFromId (const juce::String& id, InstrumentType fallback)
{
    for (int i = 0; i < numInstrumentTypes; ++i)
        if (id == instrumentIds[i])
            return (InstrumentType) i;
    return fallback;
}

EffectType effectTypeFromId (const juce::String& id, EffectType fallback)
{
    for (int i = 0; i < numEffectTypes; ++i)
        if (id == effectIds[i])
            return (EffectType) i;
    return fallback;
}

//==============================================================================
ParamSpec ParamSpec::continuous (juce::String id, juce::String name, float min, float max, float def,
                                 ParamUnit unit, float skewCentre, juce::String group)
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.minValue = min;
    p.maxValue = max;
    p.defaultValue = def;
    p.unit = unit;
    p.skewCentre = skewCentre;
    p.group = std::move (group);
    return p;
}

ParamSpec ParamSpec::choice (juce::String id, juce::String name, juce::StringArray choices, int def,
                             juce::String group)
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.minValue = 0.0f;
    p.maxValue = (float) juce::jmax (0, choices.size() - 1);
    p.defaultValue = (float) def;
    p.unit = ParamUnit::choice;
    p.choices = std::move (choices);
    p.group = std::move (group);
    return p;
}

ParamSpec ParamSpec::toggle (juce::String id, juce::String name, bool def, juce::String group)
{
    ParamSpec p;
    p.id = std::move (id);
    p.name = std::move (name);
    p.minValue = 0.0f;
    p.maxValue = 1.0f;
    p.defaultValue = def ? 1.0f : 0.0f;
    p.unit = ParamUnit::toggle;
    p.group = std::move (group);
    return p;
}

// Skewed mapping: with skewCentre set, normalised 0.5 maps to skewCentre (as in
// juce::NormalisableRange::setSkewForCentre).
static float skewExponent (const ParamSpec& p)
{
    if (p.skewCentre <= p.minValue || p.skewCentre >= p.maxValue)
        return 1.0f;
    const float proportion = (p.skewCentre - p.minValue) / (p.maxValue - p.minValue);
    return std::log (0.5f) / std::log (proportion);
}

float ParamSpec::clamp (float value) const
{
    value = juce::jlimit (minValue, maxValue, value);
    return isDiscrete() ? std::round (value) : value;
}

float ParamSpec::toNormalised (float value) const
{
    if (maxValue <= minValue)
        return 0.0f;
    const float proportion = juce::jlimit (0.0f, 1.0f, (value - minValue) / (maxValue - minValue));
    const float skew = skewExponent (*this);
    return skew == 1.0f ? proportion : std::pow (proportion, skew);
}

float ParamSpec::fromNormalised (float normalised) const
{
    normalised = juce::jlimit (0.0f, 1.0f, normalised);
    const float skew = skewExponent (*this);
    const float proportion = skew == 1.0f ? normalised : std::pow (normalised, 1.0f / skew);
    return clamp (minValue + proportion * (maxValue - minValue));
}

juce::String ParamSpec::format (float value) const
{
    switch (unit)
    {
        case ParamUnit::percent:   return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        case ParamUnit::decibels:  return value <= -69.5f ? juce::String ("-inf dB")
                                                          : juce::String (value, 1) + " dB";
        case ParamUnit::hertz:     return value >= 1000.0f ? juce::String (value / 1000.0f, 2) + " kHz"
                                                           : juce::String (value, value < 10.0f ? 2 : 0) + " Hz";
        case ParamUnit::seconds:   return value < 1.0f ? juce::String (juce::roundToInt (value * 1000.0f)) + " ms"
                                                       : juce::String (value, 2) + " s";
        case ParamUnit::semitones: return (value > 0 ? "+" : "") + juce::String (juce::roundToInt (value)) + " st";
        case ParamUnit::cents:     return (value > 0 ? "+" : "") + juce::String (juce::roundToInt (value)) + " ct";
        case ParamUnit::ratio:     return juce::String (value, 1) + ":1";
        case ParamUnit::choice:    return choices[juce::jlimit (0, choices.size() - 1, juce::roundToInt (value))];
        case ParamUnit::toggle:    return value >= 0.5f ? "On" : "Off";
        case ParamUnit::none:
        default:                   return juce::String (value, 2);
    }
}

int DeviceSpec::indexOf (const juce::String& paramId) const
{
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].id == paramId)
            return (int) i;
    return -1;
}

std::vector<float> DeviceSpec::defaults() const
{
    std::vector<float> values;
    values.reserve (params.size());
    for (auto& p : params)
        values.push_back (p.defaultValue);
    return values;
}

} // namespace rb
