namespace rb
{

Document::Document (SessionProcessor& s, SampleLibrary& lib) : session (s), samples (lib) {}

Document::~Document()
{
    stopTimer();
    if (dirty && autosave)
        saveNow();
}

void Document::open (const juce::File& setFolder, const Set& initialIfMissing)
{
    close();
    folder = setFolder;
    folder.createDirectory();

    const auto file = folder.getChildFile ("set.json");
    if (file.existsAsFile())
        set = setFromVar (juce::JSON::parse (file));
    else
        set = initialIfMissing;

    isOpenFlag = true;
    autosave = true;
    undoStack.clear();
    redoStack.clear();
    dirty = ! file.existsAsFile();
    changed (dirty);
}

void Document::openInMemory (const Set& s, const juce::File& sampleFolder)
{
    close();
    set = s;
    folder = sampleFolder;
    autosave = false;
    isOpenFlag = true;
    undoStack.clear();
    redoStack.clear();
    changed (false);
}

void Document::close()
{
    if (dirty && autosave)
        saveNow();
    stopTimer();
    isOpenFlag = false;
    autosave = false;
    dirty = false;
    folder = juce::File();
}

//==============================================================================
void Document::edit (const std::function<void (Set&)>& change)
{
    if (gestureDepth == 0 || ! gesturePushed)
    {
        undoStack.push_back (set);
        if (undoStack.size() > maxUndo)
            undoStack.erase (undoStack.begin());
        redoStack.clear();
        gesturePushed = gestureDepth > 0;
    }

    change (set);
    changed();
}

void Document::editWithoutUndo (const std::function<void (Set&)>& change)
{
    change (set);
    changed();
}

void Document::beginGesture()
{
    if (gestureDepth++ == 0)
        gesturePushed = false;
}

void Document::endGesture()
{
    gestureDepth = juce::jmax (0, gestureDepth - 1);
    if (gestureDepth == 0)
        gesturePushed = false;
}

void Document::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back (set);
    set = std::move (undoStack.back());
    undoStack.pop_back();
    changed();
}

void Document::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back (set);
    set = std::move (redoStack.back());
    redoStack.pop_back();
    changed();
}

//==============================================================================
void Document::changed (bool markDirty)
{
    publish();

    if (markDirty && autosave)
    {
        dirty = true;
        startTimer (1000);
    }

    listeners.call ([] (Listener& l) { l.documentChanged(); });
}

void Document::timerCallback()
{
    stopTimer();
    saveNow();
}

void Document::saveNow()
{
    if (! autosave || ! isOpenFlag)
        return;

    folder.createDirectory();
    const auto file = folder.getChildFile ("set.json");
    const auto temp = folder.getChildFile ("set.json.tmp");

    // Write then rename, so a crash mid-write never destroys the previous save.
    if (temp.replaceWithText (juce::JSON::toString (setToVar (set), false)))
        temp.moveFileTo (file);

    dirty = false;
}

SamplePtr Document::getSample (const juce::String& ref)
{
    return samples.get (ref, folder);
}

//==============================================================================
void Document::publish()
{
    std::lock_guard<std::recursive_mutex> lock (session.getPrepareMutex());
    session.publish (buildSnapshot());
}

SnapshotPtr Document::buildSnapshot()
{
    auto snapshot = std::make_shared<EngineSnapshot>();
    snapshot->sequence = nextSequence++;
    snapshot->tempo = set.tempo;
    snapshot->metronome = set.metronome;

    const double sampleRate = session.getSampleRate();
    const int blockSize = session.getMaxBlockSize();

    bool anySolo = false;
    for (auto& t : set.tracks)
        anySolo = anySolo || t.solo;

    std::map<int, Runtime> nextRuntimes;

    for (size_t i = 0; i < set.tracks.size() && i < (size_t) maxTracks; ++i)
    {
        auto& track = set.tracks[i];
        TrackSnapshot ts;
        ts.id = track.id;
        ts.instrumentType = track.instrumentType;

        // Reuse processors while their type is unchanged so voices and tails survive edits.
        Runtime runtime;
        if (auto it = runtimes.find (track.id); it != runtimes.end())
            runtime = it->second;

        if (runtime.instrument == nullptr || runtime.instrumentType != track.instrumentType)
        {
            runtime.instrumentType = track.instrumentType;
            runtime.instrument = createInstrument (track.instrumentType);
            if (runtime.instrument != nullptr)
                runtime.instrument->prepare (sampleRate, blockSize);
        }

        for (int slot = 0; slot < 2; ++slot)
        {
            const auto type = (EffectType) track.effects[(size_t) slot].type;
            if (runtime.effectTypes[(size_t) slot] != type || (type != EffectType::none && runtime.effects[(size_t) slot] == nullptr))
            {
                runtime.effectTypes[(size_t) slot] = type;
                runtime.effects[(size_t) slot] = createEffect (type);
                if (runtime.effects[(size_t) slot] != nullptr)
                    runtime.effects[(size_t) slot]->prepare (sampleRate, blockSize);
            }
        }

        ts.instrument = runtime.instrument;
        ts.effects = runtime.effects;
        nextRuntimes[track.id] = runtime;

        ts.instrumentValues = conformValues (getInstrumentSpec (track.instrumentType), track.instrument.values);
        for (int slot = 0; slot < 2; ++slot)
            ts.effectValues[(size_t) slot] = conformValues (getEffectSpec ((EffectType) track.effects[(size_t) slot].type),
                                                            track.effects[(size_t) slot].values);

        if (track.isDrum())
        {
            for (int p = 0; p < numDrumPads; ++p)
            {
                ts.padValues[(size_t) p] = conformValues (getPadSpec(), track.pads[(size_t) p].values);
                ts.padSamples[(size_t) p] = getSample (track.pads[(size_t) p].sampleRef);
            }
        }
        else if (track.instrumentType == InstrumentType::sampler)
        {
            ts.samplerSample = getSample (track.samplerSampleRef);
        }

        const bool audible = ! track.mute && (! anySolo || track.solo);
        ts.gain = audible ? volumeToGain (track.volume) : 0.0f;

        const auto& rates = getRepeatRates();
        ts.repeatBeats = rates[(size_t) juce::jlimit (0, (int) rates.size() - 1, track.perf.repeat)].beats;
        ts.clips = track.slots;

        snapshot->tracks.push_back (std::move (ts));
    }

    runtimes = std::move (nextRuntimes);
    return snapshot;
}

} // namespace rb
