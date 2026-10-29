namespace rb
{

SetLibrary::SetLibrary (const juce::File& r) : root (r)
{
    root.getChildFile ("Sets").createDirectory();
    root.getChildFile ("Demos").createDirectory();
    getUserSamplesFolder().createDirectory();
    getUserPresetsFolder().createDirectory();
}

std::vector<SetInfo> SetLibrary::listIn (const juce::File& parent, bool demos) const
{
    std::vector<SetInfo> sets;

    for (auto& folder : parent.findChildFiles (juce::File::findDirectories, false))
    {
        const auto file = folder.getChildFile ("set.json");
        if (! file.existsAsFile())
            continue;

        SetInfo info;
        info.id = folder.getFileName();
        info.folder = folder;
        info.modified = file.getLastModificationTime();
        info.isDemo = demos;

        juce::FileInputStream in (file);
        info.name = juce::JSON::parse (in)["name"].toString();
        if (info.name.isEmpty())
            info.name = "Untitled";
        sets.push_back (info);
    }

    std::sort (sets.begin(), sets.end(), [] (const SetInfo& a, const SetInfo& b) { return a.modified > b.modified; });
    return sets;
}

std::vector<SetInfo> SetLibrary::listSets() const { return listIn (root.getChildFile ("Sets"), false); }

std::vector<SetInfo> SetLibrary::listDemos() const
{
    auto demos = listIn (root.getChildFile ("Demos"), true);
    std::sort (demos.begin(), demos.end(), [] (auto& a, auto& b) { return a.name < b.name; });
    return demos;
}

std::optional<SetInfo> SetLibrary::find (const juce::String& id) const
{
    for (auto& list : { listSets(), listDemos() })
        for (auto& s : list)
            if (s.id == id)
                return s;
    return std::nullopt;
}

juce::String SetLibrary::uniqueName (const juce::String& base) const
{
    juce::StringArray names;
    for (auto& s : listSets())
        names.add (s.name);

    if (! names.contains (base))
        return base;

    for (int i = 2;; ++i)
    {
        const auto candidate = base + " " + juce::String (i);
        if (! names.contains (candidate))
            return candidate;
    }
}

SetInfo SetLibrary::writeNew (const Set& set, const juce::File& parent)
{
    auto folder = parent.getChildFile (juce::Uuid().toString());
    folder.createDirectory();
    folder.getChildFile ("set.json").replaceWithText (juce::JSON::toString (setToVar (set), false));

    SetInfo info;
    info.id = folder.getFileName();
    info.name = set.name;
    info.folder = folder;
    info.modified = juce::Time::getCurrentTime();
    return info;
}

Set SetLibrary::makeNewSet (const juce::String& name, juce::Random& random)
{
    Set set;
    set.name = name;
    set.tempo = 120.0;

    auto pick = [&] (std::function<bool (const TrackPreset&)> matches) -> const TrackPreset*
    {
        std::vector<const TrackPreset*> candidates;
        for (auto& p : getFactoryPresets())
            if (matches (p))
                candidates.push_back (&p);
        if (candidates.empty())
            return nullptr;
        return candidates[(size_t) random.nextInt ((int) candidates.size())];
    };

    const TrackPreset* drums = pick ([] (auto& p) { return p.type == InstrumentType::drumKit; });
    const TrackPreset* bass = pick ([] (auto& p) { return p.category == "Bass"; });
    const TrackPreset* melodic = pick ([] (auto& p)
    {
        return p.type != InstrumentType::drumKit && p.category != "Bass" && p.category != "FX";
    });

    for (auto* preset : { drums, bass, melodic })
    {
        if (preset != nullptr)
            addTrack (set, *preset);
        else
            addTrack (set, TrackPreset {});
    }

    return set;
}

SetInfo SetLibrary::createNew()
{
    return writeNew (makeNewSet (uniqueName ("Set " + juce::String (listSets().size() + 1)), random),
                     root.getChildFile ("Sets"));
}

SetInfo SetLibrary::duplicate (const SetInfo& source)
{
    auto folder = root.getChildFile ("Sets").getChildFile (juce::Uuid().toString());
    source.folder.copyDirectoryTo (folder);

    auto file = folder.getChildFile ("set.json");
    auto set = setFromVar (juce::JSON::parse (file));
    set.name = uniqueName (source.name + " Copy");
    file.replaceWithText (juce::JSON::toString (setToVar (set), false));

    SetInfo info;
    info.id = folder.getFileName();
    info.name = set.name;
    info.folder = folder;
    info.modified = juce::Time::getCurrentTime();
    return info;
}

void SetLibrary::rename (const SetInfo& info, const juce::String& newName)
{
    auto file = info.folder.getChildFile ("set.json");
    auto set = setFromVar (juce::JSON::parse (file));
    set.name = newName.trim().isNotEmpty() ? newName.trim() : set.name;
    file.replaceWithText (juce::JSON::toString (setToVar (set), false));
}

void SetLibrary::remove (const SetInfo& info)
{
    if (info.folder.isAChildOf (root))
        info.folder.deleteRecursively();
}

juce::File SetLibrary::exportBundle (const SetInfo& info) const
{
    auto out = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile (juce::File::createLegalFileName (info.name) + ".riffbook");
    out.deleteFile();

    juce::ZipFile::Builder builder;
    for (auto& f : info.folder.findChildFiles (juce::File::findFiles, true))
        builder.addFile (f, 6, f.getRelativePathFrom (info.folder).replaceCharacter ('\\', '/'));

    juce::FileOutputStream stream (out);
    if (! stream.openedOk() || ! builder.writeToStream (stream, nullptr))
        return {};
    stream.flush();
    return out;
}

std::optional<SetInfo> SetLibrary::importBundle (const juce::File& bundle)
{
    juce::ZipFile zip (bundle);
    if (zip.getIndexOfFileName ("set.json") < 0)
        return std::nullopt;

    auto folder = root.getChildFile ("Sets").getChildFile (juce::Uuid().toString());
    folder.createDirectory();

    // Only accept plain relative paths inside the bundle.
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto name = zip.getEntry (i)->filename;
        if (name.contains ("..") || name.startsWithChar ('/') || name.containsChar (':'))
        {
            folder.deleteRecursively();
            return std::nullopt;
        }
    }

    if (zip.uncompressTo (folder, true).failed())
    {
        folder.deleteRecursively();
        return std::nullopt;
    }

    auto file = folder.getChildFile ("set.json");
    auto set = setFromVar (juce::JSON::parse (file));
    set.name = uniqueName (set.name);
    file.replaceWithText (juce::JSON::toString (setToVar (set), false));

    SetInfo info;
    info.id = folder.getFileName();
    info.name = set.name;
    info.folder = folder;
    info.modified = juce::Time::getCurrentTime();
    return info;
}

void SetLibrary::ensureDemos()
{
    auto demosFolder = root.getChildFile ("Demos");
    const auto existing = listDemos();

    for (auto& demo : makeDemoSets())
    {
        const bool present = std::any_of (existing.begin(), existing.end(), [&] (auto& s) { return s.name == demo.name; });
        if (! present)
            writeNew (demo, demosFolder);
    }
}

} // namespace rb
