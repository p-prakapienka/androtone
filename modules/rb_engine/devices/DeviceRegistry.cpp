namespace rb
{

const DeviceSpec& getInstrumentSpec (InstrumentType type)
{
    static const std::array<DeviceSpec, numInstrumentTypes> specs {
        AnalogSynth::createSpec(),
        WaveSynth::createSpec(),
        DrumKit::createSpec(),
        MelodicSampler::createSpec(),
    };
    return specs[(size_t) juce::jlimit (0, numInstrumentTypes - 1, (int) type)];
}

static DeviceSpec createNoEffectSpec()
{
    DeviceSpec spec;
    spec.typeId = "none";
    spec.displayName = "None";
    return spec;
}

const DeviceSpec& getEffectSpec (EffectType type)
{
    static const std::array<DeviceSpec, numEffectTypes> specs {
        createNoEffectSpec(),
        AutoFilter::createSpec(),
        ChannelEq::createSpec(),
        Chorus::createSpec(),
        Delay::createSpec(),
        Dynamics::createSpec(),
        Phaser::createSpec(),
        Crusher::createSpec(),
        Reverb::createSpec(),
        Saturator::createSpec(),
    };
    return specs[(size_t) juce::jlimit (0, numEffectTypes - 1, (int) type)];
}

const DeviceSpec& getPadSpec()
{
    static const DeviceSpec spec = DrumKit::createPadSpec();
    return spec;
}

static InstrumentFactory& instrumentFactoryOverride()
{
    static InstrumentFactory factory;
    return factory;
}

void setInstrumentFactoryOverride (InstrumentFactory factory)
{
    instrumentFactoryOverride() = std::move (factory);
}

std::unique_ptr<InstrumentProcessor> createInstrument (InstrumentType type)
{
    if (auto& factory = instrumentFactoryOverride())
        return factory (type);

    switch (type)
    {
        case InstrumentType::analog:  return std::make_unique<AnalogSynth>();
        case InstrumentType::wave:    return std::make_unique<WaveSynth>();
        case InstrumentType::drumKit: return std::make_unique<DrumKit>();
        case InstrumentType::sampler: return std::make_unique<MelodicSampler>();
    }
    return nullptr;
}

std::unique_ptr<EffectProcessor> createEffect (EffectType type)
{
    switch (type)
    {
        case EffectType::none:       return nullptr;
        case EffectType::autoFilter: return std::make_unique<AutoFilter>();
        case EffectType::eq:         return std::make_unique<ChannelEq>();
        case EffectType::chorus:     return std::make_unique<Chorus>();
        case EffectType::delay:      return std::make_unique<Delay>();
        case EffectType::dynamics:   return std::make_unique<Dynamics>();
        case EffectType::phaser:     return std::make_unique<Phaser>();
        case EffectType::crusher:    return std::make_unique<Crusher>();
        case EffectType::reverb:     return std::make_unique<Reverb>();
        case EffectType::saturator:  return std::make_unique<Saturator>();
    }
    return nullptr;
}

juce::String getInstrumentDisplayName (InstrumentType type) { return getInstrumentSpec (type).displayName; }
juce::String getEffectDisplayName (EffectType type)         { return getEffectSpec (type).displayName; }

std::vector<float> conformValues (const DeviceSpec& spec, std::vector<float> values)
{
    const size_t provided = values.size();
    values.resize (spec.params.size());
    for (size_t i = 0; i < spec.params.size(); ++i)
        values[i] = i < provided ? spec.params[i].clamp (values[i]) : spec.params[i].defaultValue;
    return values;
}

} // namespace rb
