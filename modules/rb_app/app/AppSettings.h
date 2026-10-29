#pragma once

namespace rb
{

// App-wide preferences (spec §11), persisted in the app data folder.
class AppSettings : public juce::ChangeBroadcaster
{
public:
    explicit AppSettings (const juce::File& file);
    ~AppSettings() override;

    enum class Theme { dark = 0, highContrast = 1 };
    enum class MelodicLayout { fourths = 0, sequential = 1 };

    Theme getTheme() const;
    void setTheme (Theme);

    bool getCaptureTimeout() const;
    void setCaptureTimeout (bool);

    bool getShowNoteNames() const;
    void setShowNoteNames (bool);

    bool getShowSampleNames() const;
    void setShowSampleNames (bool);

    MelodicLayout getMelodicLayout() const;
    void setMelodicLayout (MelodicLayout);

    bool getShowTouches() const;
    void setShowTouches (bool);

    bool getMonitorInput() const;
    void setMonitorInput (bool);

    // Identifiers of MIDI inputs the user enabled (MIDI-1).
    juce::StringArray getEnabledMidiInputs() const;
    void setEnabledMidiInputs (const juce::StringArray&);

    // MIDI mappings (MIDI-3): target name -> "channel:cc" or "channel:note:<n>".
    juce::String getMidiMapping (const juce::String& target) const;
    void setMidiMapping (const juce::String& target, const juce::String& source);

    // Favorite presets by name (DEV-3).
    juce::StringArray getFavorites() const;
    bool isFavorite (const juce::String& presetKey) const;
    void toggleFavorite (const juce::String& presetKey);

    juce::String getLastOpenedSet() const;
    void setLastOpenedSet (const juce::String&);

private:
    void set (const juce::String& key, const juce::var& value);

    std::unique_ptr<juce::PropertiesFile> props;
};

} // namespace rb
