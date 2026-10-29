namespace rb
{

static juce::var makeObject() { return juce::var (new juce::DynamicObject()); }

static double getDouble (const juce::var& v, const char* key, double fallback)
{
    const auto& p = v[key];
    return p.isVoid() ? fallback : (double) p;
}

static int getInt (const juce::var& v, const char* key, int fallback)
{
    const auto& p = v[key];
    return p.isVoid() ? fallback : (int) p;
}

static bool getBool (const juce::var& v, const char* key, bool fallback)
{
    const auto& p = v[key];
    return p.isVoid() ? fallback : (bool) p;
}

juce::var valuesToVar (const DeviceSpec& spec, const std::vector<float>& values)
{
    auto obj = makeObject();
    for (size_t i = 0; i < spec.params.size() && i < values.size(); ++i)
        obj.getDynamicObject()->setProperty (spec.params[i].id, values[i]);
    return obj;
}

std::vector<float> valuesFromVar (const DeviceSpec& spec, const juce::var& v)
{
    auto values = spec.defaults();
    if (auto* obj = v.getDynamicObject())
        for (size_t i = 0; i < spec.params.size(); ++i)
            if (obj->hasProperty (spec.params[i].id))
                values[i] = spec.params[i].clamp ((float) obj->getProperty (spec.params[i].id));
    return values;
}

const DeviceSpec* specForTarget (const Track& track, const ParamTarget& target)
{
    switch (target.device)
    {
        case ParamTarget::instrument: return &getInstrumentSpec (track.instrumentType);
        case ParamTarget::effect1:    return &getEffectSpec ((EffectType) track.effects[0].type);
        case ParamTarget::effect2:    return &getEffectSpec ((EffectType) track.effects[1].type);
        case ParamTarget::pad:        return &getPadSpec();
        default:                      return nullptr;
    }
}

juce::var clipToVar (const Clip& clip, const Track& track)
{
    auto obj = makeObject();
    auto* o = obj.getDynamicObject();
    o->setProperty ("bars", clip.lengthBars);
    o->setProperty ("loopStart", clip.loopStart);
    o->setProperty ("loopLength", clip.loopLength);

    juce::Array<juce::var> notes;
    for (auto& n : clip.notes)
    {
        juce::Array<juce::var> a { n.start, n.length, n.pitch, n.velocity, n.pitchOffset };
        if (n.unquantizedStart >= 0.0)
            a.add (n.unquantizedStart);
        notes.add (a);
    }
    o->setProperty ("notes", notes);

    juce::Array<juce::var> lanes;
    for (auto& lane : clip.automation)
    {
        auto* spec = specForTarget (track, lane.target);
        if (spec == nullptr || lane.target.paramIndex >= (int) spec->params.size())
            continue;

        auto l = makeObject();
        l.getDynamicObject()->setProperty ("device", lane.target.device);
        l.getDynamicObject()->setProperty ("pad", lane.target.padIndex);
        l.getDynamicObject()->setProperty ("param", spec->params[(size_t) lane.target.paramIndex].id);

        juce::Array<juce::var> points;
        for (auto& p : lane.points)
            points.add (juce::Array<juce::var> { p.beat, p.value });
        l.getDynamicObject()->setProperty ("points", points);
        lanes.add (l);
    }
    o->setProperty ("automation", lanes);
    return obj;
}

ClipPtr clipFromVar (const juce::var& v, const Track& track)
{
    if (! v.isObject())
        return nullptr;

    auto clip = std::make_shared<Clip>();
    clip->lengthBars = juce::jlimit (1, maxClipBars, getInt (v, "bars", 1));
    clip->loopStart = juce::jlimit (0.0, clip->lengthBeats(), getDouble (v, "loopStart", 0.0));
    clip->loopLength = juce::jlimit (0.0625, clip->lengthBeats() - clip->loopStart,
                                     getDouble (v, "loopLength", clip->lengthBeats()));

    if (auto* notes = v["notes"].getArray())
        for (auto& n : *notes)
        {
            if (! n.isArray() || n.size() < 4)
                continue;
            Note note;
            note.start = (double) n[0];
            note.length = juce::jmax (1.0 / 128.0, (double) n[1]);
            note.pitch = juce::jlimit (0, 127, (int) n[2]);
            note.velocity = juce::jlimit (0.0f, 1.0f, (float) n[3]);
            note.pitchOffset = n.size() > 4 ? (float) n[4] : 0.0f;
            note.unquantizedStart = n.size() > 5 ? (double) n[5] : -1.0;
            clip->notes.push_back (note);
        }
    clip->sortNotes();

    if (auto* lanes = v["automation"].getArray())
        for (auto& l : *lanes)
        {
            AutomationLane lane;
            lane.target.device = juce::jlimit (0, 3, getInt (l, "device", 0));
            lane.target.padIndex = juce::jlimit (0, numDrumPads - 1, getInt (l, "pad", 0));

            auto* spec = specForTarget (track, lane.target);
            if (spec == nullptr)
                continue;
            lane.target.paramIndex = spec->indexOf (l["param"].toString());
            if (lane.target.paramIndex < 0)
                continue;

            if (auto* points = l["points"].getArray())
                for (auto& p : *points)
                    if (p.isArray() && p.size() >= 2)
                        lane.points.push_back ({ (double) p[0], (float) p[1] });

            if (! lane.points.empty())
                clip->automation.push_back (std::move (lane));
        }

    return clip;
}

static juce::var trackToVar (const Track& t)
{
    auto obj = makeObject();
    auto* o = obj.getDynamicObject();
    o->setProperty ("id", t.id);
    o->setProperty ("name", t.name);
    o->setProperty ("colour", t.colour);
    o->setProperty ("preset", t.presetName);
    o->setProperty ("category", t.presetCategory);
    o->setProperty ("modified", t.presetModified);
    o->setProperty ("instrument", toTypeId (t.instrumentType));
    o->setProperty ("params", valuesToVar (getInstrumentSpec (t.instrumentType), t.instrument.values));

    juce::Array<juce::var> macros;
    for (auto& m : t.macros)
        macros.add (m);
    o->setProperty ("macros", macros);

    juce::Array<juce::var> effects;
    for (auto& fx : t.effects)
    {
        auto e = makeObject();
        e.getDynamicObject()->setProperty ("type", toTypeId ((EffectType) fx.type));
        e.getDynamicObject()->setProperty ("preset", fx.presetName);
        e.getDynamicObject()->setProperty ("params", valuesToVar (getEffectSpec ((EffectType) fx.type), fx.values));
        effects.add (e);
    }
    o->setProperty ("effects", effects);

    if (t.isDrum())
    {
        juce::Array<juce::var> pads;
        for (auto& pad : t.pads)
        {
            auto p = makeObject();
            p.getDynamicObject()->setProperty ("name", pad.name);
            p.getDynamicObject()->setProperty ("sample", pad.sampleRef);
            p.getDynamicObject()->setProperty ("params", valuesToVar (getPadSpec(), pad.values));
            pads.add (p);
        }
        o->setProperty ("pads", pads);
    }

    o->setProperty ("sample", t.samplerSampleRef);
    o->setProperty ("volume", t.volume);
    o->setProperty ("mute", t.mute);
    o->setProperty ("solo", t.solo);

    auto perf = makeObject();
    perf.getDynamicObject()->setProperty ("layout", t.perf.layout);
    perf.getDynamicObject()->setProperty ("octave", t.perf.octave);
    perf.getDynamicObject()->setProperty ("repeat", t.perf.repeat);
    perf.getDynamicObject()->setProperty ("selectedPad", t.perf.selectedPad);
    o->setProperty ("perf", perf);

    juce::Array<juce::var> clips;
    for (auto& c : t.slots)
        clips.add (c != nullptr ? clipToVar (*c, t) : juce::var());
    o->setProperty ("clips", clips);
    return obj;
}

static Track trackFromVar (const juce::var& v)
{
    Track t;
    t.id = getInt (v, "id", 0);
    t.name = v["name"].toString();
    t.colour = getInt (v, "colour", 0);
    t.presetName = v["preset"].toString();
    t.presetCategory = v["category"].toString();
    t.presetModified = getBool (v, "modified", false);
    t.instrumentType = instrumentTypeFromId (v["instrument"].toString());
    t.instrument.type = (int) t.instrumentType;
    t.instrument.values = valuesFromVar (getInstrumentSpec (t.instrumentType), v["params"]);

    if (auto* macros = v["macros"].getArray())
        for (auto& m : *macros)
            t.macros.add (m.toString());

    if (auto* effects = v["effects"].getArray())
        for (int i = 0; i < 2 && i < effects->size(); ++i)
        {
            auto& e = effects->getReference (i);
            auto type = effectTypeFromId (e["type"].toString());
            t.effects[(size_t) i].type = (int) type;
            t.effects[(size_t) i].presetName = e["preset"].toString();
            t.effects[(size_t) i].values = valuesFromVar (getEffectSpec (type), e["params"]);
        }

    for (auto& pad : t.pads)
        pad.values = getPadSpec().defaults();

    if (auto* pads = v["pads"].getArray())
        for (int i = 0; i < numDrumPads && i < pads->size(); ++i)
        {
            auto& p = pads->getReference (i);
            t.pads[(size_t) i].name = p["name"].toString();
            t.pads[(size_t) i].sampleRef = p["sample"].toString();
            t.pads[(size_t) i].values = valuesFromVar (getPadSpec(), p["params"]);
        }

    t.samplerSampleRef = v["sample"].toString();
    t.volume = (float) getDouble (v, "volume", 0.8);
    t.mute = getBool (v, "mute", false);
    t.solo = getBool (v, "solo", false);

    const auto& perf = v["perf"];
    t.perf.layout = getInt (perf, "layout", 0);
    t.perf.octave = getInt (perf, "octave", 0);
    t.perf.repeat = juce::jlimit (0, (int) getRepeatRates().size() - 1, getInt (perf, "repeat", 0));
    t.perf.selectedPad = juce::jlimit (0, numDrumPads - 1, getInt (perf, "selectedPad", 0));

    if (auto* clips = v["clips"].getArray())
        for (int i = 0; i < numScenes && i < clips->size(); ++i)
            t.slots[(size_t) i] = clipFromVar (clips->getReference (i), t);

    return t;
}

juce::var setToVar (const Set& set)
{
    auto obj = makeObject();
    auto* o = obj.getDynamicObject();
    o->setProperty ("format", 1);
    o->setProperty ("name", set.name);
    o->setProperty ("tempo", set.tempo);
    o->setProperty ("metronome", set.metronome);
    o->setProperty ("inKey", set.inKey);
    o->setProperty ("root", set.root);
    o->setProperty ("scale", set.scale);
    o->setProperty ("nextTrackId", set.nextTrackId);

    juce::Array<juce::var> tracks;
    for (auto& t : set.tracks)
        tracks.add (trackToVar (t));
    o->setProperty ("tracks", tracks);
    return obj;
}

Set setFromVar (const juce::var& v)
{
    Set set;
    set.name = v["name"].toString();
    if (set.name.isEmpty())
        set.name = "Untitled";
    set.tempo = juce::jlimit (minTempo, maxTempo, getDouble (v, "tempo", 120.0));
    set.metronome = getBool (v, "metronome", false);
    set.inKey = getBool (v, "inKey", false);
    set.root = juce::jlimit (0, 11, getInt (v, "root", 0));
    set.scale = juce::jlimit (0, (int) getScales().size() - 1, getInt (v, "scale", 0));

    int maxId = 0;
    if (auto* tracks = v["tracks"].getArray())
        for (int i = 0; i < maxTracks && i < tracks->size(); ++i)
        {
            auto t = trackFromVar (tracks->getReference (i));
            if (t.id <= 0 || set.findTrack (t.id) != nullptr)
                t.id = maxId + 1;
            maxId = juce::jmax (maxId, t.id);
            set.tracks.push_back (std::move (t));
        }

    set.nextTrackId = juce::jmax (maxId + 1, getInt (v, "nextTrackId", 1));
    return set;
}

} // namespace rb
