namespace rb
{

struct BrowserSheet::Model : public juce::ListBoxModel
{
    Model (BrowserSheet& o) : owner (o)
    {
        if (owner.mode == Mode::trackPreset)
            for (auto& p : getFactoryPresets())
                names.add (p.category + " / " + p.name);
        else if (owner.mode == Mode::sample)
            for (auto& s : FactorySounds::list())
                names.add (s.category + " / " + s.name);
        else
            for (auto& p : getFactoryEffectPresets())
                names.add (getEffectDisplayName (p.type) + " / " + p.name);
    }

    int getNumRows() override { return names.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        g.fillAll (selected ? Theme::get().surfaceRaised : Theme::get().surface);
        g.setColour (Theme::get().text);
        g.setFont (Theme::font (16.0f));
        g.drawText (names[row], 12, 0, w - 24, h, juce::Justification::centredLeft);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        auto& studio = owner.studio;
        const int id = owner.trackId;

        if (owner.mode == Mode::trackPreset)
        {
            const auto preset = getFactoryPresets()[(size_t) row];
            studio.getDocument().edit ([&] (Set& s) { if (auto* t = s.findTrack (id)) applyPreset (*t, preset); });
        }
        else if (owner.mode == Mode::sample)
        {
            const auto ref = "factory:" + FactorySounds::list()[(size_t) row].id;
            studio.getDocument().edit ([&] (Set& s)
            {
                if (auto* t = s.findTrack (id))
                {
                    if (t->isDrum()) t->pads[(size_t) t->perf.selectedPad].sampleRef = ref;
                    else t->samplerSampleRef = ref;
                }
            });
        }
        else
        {
            const auto preset = getFactoryEffectPresets()[(size_t) row];
            const int slot = owner.mode == Mode::effect1 ? 0 : 1;
            studio.getDocument().edit ([&] (Set& s) { if (auto* t = s.findTrack (id)) applyEffectPreset (*t, slot, preset); });
        }
    }

    BrowserSheet& owner;
    juce::StringArray names;
};

BrowserSheet::BrowserSheet (Studio& s, int id, Mode m) : Sheet ("Browse"), studio (s), trackId (id), mode (m)
{
    model = std::make_unique<Model> (*this);
    list.setModel (model.get());
    list.setRowHeight (Theme::touch);
    addAndMakeVisible (list);
}

void BrowserSheet::resized()
{
    Sheet::resized();
    list.setBounds (getContentArea());
}

} // namespace rb
