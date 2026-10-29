namespace rb
{

const juce::StringArray& getPresetCategories()
{
    static const juce::StringArray categories { "Drums", "Bass", "Keys", "Lead", "Pad", "Pluck", "Mallets", "Sampler", "FX" };
    return categories;
}

const std::vector<TrackPreset>& getFactoryPresets()
{
    static const std::vector<TrackPreset> presets = []
    {
        std::vector<TrackPreset> list;
        addDrumKitPresets (list);
        addAnalogPresets (list);
        addWavePresets (list);
        addSamplerPresets (list);
        return list;
    }();
    return presets;
}

const std::vector<EffectPreset>& getFactoryEffectPresets()
{
    static const std::vector<EffectPreset> presets = []
    {
        std::vector<EffectPreset> list;
        addEffectPresets (list);

        // Every effect type has at least a default preset.
        for (int t = 1; t < numEffectTypes; ++t)
        {
            const auto type = (EffectType) t;
            const bool present = std::any_of (list.begin(), list.end(), [&] (const EffectPreset& p) { return p.type == type; });
            if (! present)
                list.push_back (defaultEffectPreset (type));
        }
        return list;
    }();
    return presets;
}

const TrackPreset* findFactoryPreset (const juce::String& name)
{
    for (auto& p : getFactoryPresets())
        if (p.name == name)
            return &p;
    return nullptr;
}

EffectPreset defaultEffectPreset (EffectType type)
{
    EffectPreset p;
    p.type = type;
    p.name = getEffectDisplayName (type);
    return p;
}

static std::vector<float> valuesFrom (const DeviceSpec& spec, const ParamValues& values)
{
    auto result = spec.defaults();
    for (size_t i = 0; i < spec.params.size(); ++i)
        if (auto it = values.find (spec.params[i].id); it != values.end())
            result[i] = spec.params[i].clamp (it->second);
    return result;
}

static ParamValues valuesTo (const DeviceSpec& spec, const std::vector<float>& values)
{
    ParamValues result;
    for (size_t i = 0; i < spec.params.size() && i < values.size(); ++i)
        if (values[i] != spec.params[i].defaultValue)
            result[spec.params[i].id] = values[i];
    return result;
}

void applyEffectPreset (Track& track, int slot, const EffectPreset& preset)
{
    auto& fx = track.effects[(size_t) juce::jlimit (0, 1, slot)];
    fx.type = (int) preset.type;
    fx.presetName = preset.name;
    fx.values = valuesFrom (getEffectSpec (preset.type), preset.values);

    // Automation of the replaced effect no longer applies.
    const int device = slot == 0 ? ParamTarget::effect1 : ParamTarget::effect2;
    for (auto& clip : track.slots)
    {
        if (clip == nullptr || clip->automation.empty())
            continue;
        auto copy = std::make_shared<Clip> (*clip);
        copy->automation.erase (std::remove_if (copy->automation.begin(), copy->automation.end(),
                                                [&] (const AutomationLane& l) { return l.target.device == device; }),
                                copy->automation.end());
        clip = copy;
    }
}

void applyPreset (Track& track, const TrackPreset& preset)
{
    const bool typeChanged = track.instrumentType != preset.type;

    track.instrumentType = preset.type;
    track.instrument.type = (int) preset.type;
    track.instrument.presetName = preset.name;
    track.instrument.values = valuesFrom (getInstrumentSpec (preset.type), preset.values);
    track.macros = preset.macros;
    track.presetName = preset.name;
    track.presetCategory = preset.category;
    track.presetModified = false;
    track.name = preset.name;

    for (int slot = 0; slot < 2; ++slot)
    {
        auto fx = preset.effects[(size_t) slot];
        if (fx.type == EffectType::none && fx.name.isEmpty())
            fx = defaultEffectPreset (slot == 0 ? (preset.type == InstrumentType::drumKit ? EffectType::reverb : EffectType::chorus)
                                                : (preset.type == InstrumentType::drumKit ? EffectType::dynamics : EffectType::reverb));
        applyEffectPreset (track, slot, fx);
    }

    for (int p = 0; p < numDrumPads; ++p)
    {
        auto& pad = track.pads[(size_t) p];
        auto& source = preset.pads[(size_t) p];
        pad.name = source.name;
        pad.sampleRef = source.sampleRef;
        pad.values = valuesFrom (getPadSpec(), source.values);
    }

    track.samplerSampleRef = preset.sampleRef;

    // Instrument automation refers to parameters of the old instrument.
    for (auto& clip : track.slots)
    {
        if (clip == nullptr || clip->automation.empty())
            continue;
        auto copy = std::make_shared<Clip> (*clip);
        copy->automation.erase (std::remove_if (copy->automation.begin(), copy->automation.end(),
                                                [] (const AutomationLane& l)
                                                { return l.target.device == ParamTarget::instrument || l.target.device == ParamTarget::pad; }),
                                copy->automation.end());
        clip = copy;
    }

    if (typeChanged)
        track.perf.layout = PerformanceOptions::pads;
}

TrackPreset presetFromTrack (const Track& track, const juce::String& name)
{
    TrackPreset p;
    p.name = name;
    p.category = track.presetCategory.isNotEmpty() ? track.presetCategory
                                                   : (track.isDrum() ? "Drums" : track.instrumentType == InstrumentType::sampler ? "Sampler" : "Keys");
    p.type = track.instrumentType;
    p.values = valuesTo (getInstrumentSpec (track.instrumentType), track.instrument.values);
    p.macros = track.macros;

    for (int slot = 0; slot < 2; ++slot)
    {
        auto& fx = track.effects[(size_t) slot];
        p.effects[(size_t) slot].type = (EffectType) fx.type;
        p.effects[(size_t) slot].name = fx.presetName;
        p.effects[(size_t) slot].values = valuesTo (getEffectSpec ((EffectType) fx.type), fx.values);
    }

    for (int i = 0; i < numDrumPads; ++i)
    {
        p.pads[(size_t) i].name = track.pads[(size_t) i].name;
        p.pads[(size_t) i].sampleRef = track.pads[(size_t) i].sampleRef;
        p.pads[(size_t) i].values = valuesTo (getPadSpec(), track.pads[(size_t) i].values);
    }

    p.sampleRef = track.samplerSampleRef;
    p.isUser = true;
    return p;
}

//==============================================================================
static juce::var mapToVar (const ParamValues& values)
{
    auto obj = juce::var (new juce::DynamicObject());
    for (auto& [id, v] : values)
        obj.getDynamicObject()->setProperty (id, v);
    return obj;
}

static ParamValues mapFromVar (const juce::var& v)
{
    ParamValues values;
    if (auto* obj = v.getDynamicObject())
        for (auto& prop : obj->getProperties())
            values[prop.name.toString()] = (float) prop.value;
    return values;
}

juce::var presetToVar (const TrackPreset& p)
{
    auto obj = juce::var (new juce::DynamicObject());
    auto* o = obj.getDynamicObject();
    o->setProperty ("name", p.name);
    o->setProperty ("category", p.category);
    o->setProperty ("instrument", toTypeId (p.type));
    o->setProperty ("params", mapToVar (p.values));

    juce::Array<juce::var> macros;
    for (auto& m : p.macros)
        macros.add (m);
    o->setProperty ("macros", macros);

    juce::Array<juce::var> effects;
    for (auto& fx : p.effects)
    {
        auto e = juce::var (new juce::DynamicObject());
        e.getDynamicObject()->setProperty ("type", toTypeId (fx.type));
        e.getDynamicObject()->setProperty ("name", fx.name);
        e.getDynamicObject()->setProperty ("params", mapToVar (fx.values));
        effects.add (e);
    }
    o->setProperty ("effects", effects);

    juce::Array<juce::var> pads;
    for (auto& pad : p.pads)
    {
        auto e = juce::var (new juce::DynamicObject());
        e.getDynamicObject()->setProperty ("name", pad.name);
        e.getDynamicObject()->setProperty ("sample", pad.sampleRef);
        e.getDynamicObject()->setProperty ("params", mapToVar (pad.values));
        pads.add (e);
    }
    o->setProperty ("pads", pads);
    o->setProperty ("sample", p.sampleRef);
    return obj;
}

TrackPreset presetFromVar (const juce::var& v)
{
    TrackPreset p;
    p.name = v["name"].toString();
    p.category = v["category"].toString();
    p.type = instrumentTypeFromId (v["instrument"].toString());
    p.values = mapFromVar (v["params"]);

    if (auto* macros = v["macros"].getArray())
        for (auto& m : *macros)
            p.macros.add (m.toString());

    if (auto* effects = v["effects"].getArray())
        for (int i = 0; i < 2 && i < effects->size(); ++i)
        {
            auto& e = effects->getReference (i);
            p.effects[(size_t) i].type = effectTypeFromId (e["type"].toString());
            p.effects[(size_t) i].name = e["name"].toString();
            p.effects[(size_t) i].values = mapFromVar (e["params"]);
        }

    if (auto* pads = v["pads"].getArray())
        for (int i = 0; i < numDrumPads && i < pads->size(); ++i)
        {
            auto& e = pads->getReference (i);
            p.pads[(size_t) i].name = e["name"].toString();
            p.pads[(size_t) i].sampleRef = e["sample"].toString();
            p.pads[(size_t) i].values = mapFromVar (e["params"]);
        }

    p.sampleRef = v["sample"].toString();
    p.isUser = true;
    return p;
}

std::vector<TrackPreset> loadUserPresets (const juce::File& folder)
{
    std::vector<TrackPreset> presets;
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        auto p = presetFromVar (juce::JSON::parse (f));
        if (p.name.isNotEmpty())
            presets.push_back (std::move (p));
    }
    std::sort (presets.begin(), presets.end(), [] (auto& a, auto& b) { return a.name.compareNatural (b.name) < 0; });
    return presets;
}

bool saveUserPreset (const juce::File& folder, const TrackPreset& preset)
{
    folder.createDirectory();
    const auto file = folder.getChildFile (juce::File::createLegalFileName (preset.name) + ".json");
    return file.replaceWithText (juce::JSON::toString (presetToVar (preset)));
}

} // namespace rb
