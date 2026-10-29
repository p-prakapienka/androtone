#pragma once

namespace rb
{

// Browser for track presets, effect presets and samples (DEV-3, DEV-4, DEV-23, SMP-2).
//
// Tapping an item loads it on the track right away so it can be heard (and played on the
// audition pads under the list). The whole browse session is one undo step: Done keeps the
// current sound, Cancel (the close button) restores the track as it was when the browser
// opened. Dismissing the sheet by tapping outside it keeps the sound, like Done.
class BrowserSheet : public Sheet,
                     private Studio::Listener,
                     private juce::Timer
{
public:
    enum class Mode { trackPreset, effect1, effect2, sample };

    BrowserSheet (Studio&, int trackId, Mode);
    ~BrowserSheet() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void done();
    void cancel();

    // Chip labels in display order, and selecting one by label (also used by snapshots).
    juce::StringArray getChips() const { return chips; }
    void selectChip (const juce::String& label);

private:
    struct Item
    {
        enum class Kind { header, preset, effect, clearEffect, sample };

        Kind kind = Kind::header;
        juce::String label, detail;
        Icon icon = Icon::waveform;
        juce::String key;         // matches loadedKey() when this item is what the track uses
        juce::String favoriteKey; // empty: no heart
        int index = -1;           // into shownPresets / shownEffects
        juce::String ref;         // samples
        bool userPreset = false;
    };

    class ChipBar;
    class ItemList;
    class AuditionPads;

    void studioChanged() override;
    void timerCallback() override;

    const Track* track() const;
    void buildChips();
    void rebuildItems();
    juce::String loadedKey() const;
    juce::String emptyMessage() const;
    void updateTitle();

    void activate (const Item&);
    void toggleFavorite (const Item&);
    void showItemMenu (const Item&, juce::Component& target);
    void showTypeFilterMenu();
    void saveUserPreset();
    void reloadUserPresets();
    void applyEdit (std::function<void (Track&)> change);

    void audition (int note, int durationMs);
    void stopAudition();
    int auditionNote() const;

    Studio& studio;
    int trackId;
    Mode mode;

    Track original;
    bool edited = false, finished = false;
    bool wasDrum = false;

    std::vector<TrackPreset> userPresets;
    std::vector<TrackPreset> shownPresets;
    std::vector<EffectPreset> shownEffects;
    std::vector<Item> items;

    juce::StringArray chips;
    int chipIndex = 0;
    int typeFilter = -1; // InstrumentType, -1 = all

    struct SearchIcon : public juce::Component
    {
        SearchIcon() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { drawIcon (g, Icon::search, getLocalBounds().toFloat().reduced (2.0f), Theme::get().textDim); }
    };

    juce::TextEditor searchEditor;
    SearchIcon searchIcon;
    IconButton clearSearchButton { Icon::close }, saveButton { Icon::save };
    PillButton filterButton { "All", PillButton::Style::outline }; // instrument type filter (DEV-3)
    PillButton doneButton { "Done", PillButton::Style::accent };

    std::unique_ptr<ChipBar> chipBar;
    std::unique_ptr<ItemList> list;
    std::unique_ptr<AuditionPads> pads;

    int playingNote = -1;
    bool scrolledToLoaded = false;
};

} // namespace rb
