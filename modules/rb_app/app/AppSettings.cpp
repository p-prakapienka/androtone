namespace rb
{

static juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Riffbook";
    o.filenameSuffix = ".settings";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = 500;
    return o;
}

AppSettings::AppSettings (const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    props = std::make_unique<juce::PropertiesFile> (file, settingsOptions());
}

AppSettings::~AppSettings()
{
    props->saveIfNeeded();
}

void AppSettings::set (const juce::String& key, const juce::var& value)
{
    props->setValue (key, value);
    sendChangeMessage();
}

AppSettings::Theme AppSettings::getTheme() const { return (Theme) props->getIntValue ("theme", 0); }
void AppSettings::setTheme (Theme t) { set ("theme", (int) t); }

bool AppSettings::getCaptureTimeout() const { return props->getBoolValue ("captureTimeout", true); }
void AppSettings::setCaptureTimeout (bool b) { set ("captureTimeout", b); }

bool AppSettings::getShowNoteNames() const { return props->getBoolValue ("showNoteNames", false); }
void AppSettings::setShowNoteNames (bool b) { set ("showNoteNames", b); }

bool AppSettings::getShowSampleNames() const { return props->getBoolValue ("showSampleNames", true); }
void AppSettings::setShowSampleNames (bool b) { set ("showSampleNames", b); }

AppSettings::MelodicLayout AppSettings::getMelodicLayout() const
{
    return (MelodicLayout) props->getIntValue ("melodicLayout", 0);
}
void AppSettings::setMelodicLayout (MelodicLayout l) { set ("melodicLayout", (int) l); }

bool AppSettings::getShowTouches() const { return props->getBoolValue ("showTouches", false); }
void AppSettings::setShowTouches (bool b) { set ("showTouches", b); }

bool AppSettings::getMonitorInput() const { return props->getBoolValue ("monitorInput", false); }
void AppSettings::setMonitorInput (bool b) { set ("monitorInput", b); }

juce::StringArray AppSettings::getEnabledMidiInputs() const
{
    return juce::StringArray::fromLines (props->getValue ("midiInputs"));
}

void AppSettings::setEnabledMidiInputs (const juce::StringArray& ids)
{
    set ("midiInputs", ids.joinIntoString ("\n"));
}

juce::String AppSettings::getMidiMapping (const juce::String& target) const
{
    return props->getValue ("map_" + target);
}

void AppSettings::setMidiMapping (const juce::String& target, const juce::String& source)
{
    set ("map_" + target, source);
}

juce::StringArray AppSettings::getFavorites() const
{
    return juce::StringArray::fromLines (props->getValue ("favorites"));
}

bool AppSettings::isFavorite (const juce::String& key) const
{
    return getFavorites().contains (key);
}

void AppSettings::toggleFavorite (const juce::String& key)
{
    auto favs = getFavorites();
    if (favs.contains (key))
        favs.removeString (key);
    else
        favs.add (key);
    favs.removeEmptyStrings();
    set ("favorites", favs.joinIntoString ("\n"));
}

juce::String AppSettings::getLastOpenedSet() const { return props->getValue ("lastSet"); }
void AppSettings::setLastOpenedSet (const juce::String& id) { set ("lastSet", id); }

} // namespace rb
