#pragma once

namespace rb
{

struct SetInfo
{
    juce::String id;   // folder name
    juce::String name;
    juce::File folder;
    juce::Time modified;
    bool isDemo = false;
};

// The sets stored on the device (LIB-1..LIB-4) plus the user library folders.
//
// Layout under the app data folder:
//   Sets/<id>/set.json, Sets/<id>/samples/*.wav
//   Demos/<id>/...
//   User/Samples/*.wav   (saved recordings, imports)
//   User/Presets/*.json  (user presets)
class SetLibrary
{
public:
    explicit SetLibrary (const juce::File& root);

    std::vector<SetInfo> listSets() const;  // newest first
    std::vector<SetInfo> listDemos() const;
    std::optional<SetInfo> find (const juce::String& id) const;

    SetInfo createNew();                    // LIB-2
    SetInfo duplicate (const SetInfo&);
    void rename (const SetInfo&, const juce::String& newName);
    void remove (const SetInfo&);

    // LIB-4: a .riffbook bundle (zip of set.json + samples) in the temp folder.
    juce::File exportBundle (const SetInfo&) const;
    std::optional<SetInfo> importBundle (const juce::File& bundle);

    void ensureDemos();

    juce::File getRoot() const { return root; }
    juce::File getUserSamplesFolder() const { return root.getChildFile ("User").getChildFile ("Samples"); }
    juce::File getUserPresetsFolder() const { return root.getChildFile ("User").getChildFile ("Presets"); }

    juce::String uniqueName (const juce::String& base) const;

    // A fresh set: three tracks with random drum, bass and melodic/sampler presets.
    static Set makeNewSet (const juce::String& name, juce::Random& random);

private:
    std::vector<SetInfo> listIn (const juce::File& folder, bool demos) const;
    SetInfo writeNew (const Set& set, const juce::File& parent);

    juce::File root;
    juce::Random random;
};


} // namespace rb
