namespace rb
{

namespace
{
    constexpr int tabHeight = 40;
    constexpr int pageBarHeight = 30;
    constexpr int knobsPerPage = 8;
}

DevicePanel::DevicePanel (Studio& s, Navigator& n, int id) : studio (s), nav (n), trackId (id)
{
    studio.addListener (this);
    rebuild();
}

DevicePanel::~DevicePanel()
{
    studio.removeListener (this);
}

std::vector<DevicePanel::Tab> DevicePanel::availableTabs() const
{
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return {};
    if (track->instrumentType == InstrumentType::drumKit || track->instrumentType == InstrumentType::sampler)
        return { Tab::instrument, Tab::sample, Tab::effect1, Tab::effect2 };
    return { Tab::instrument, Tab::effect1, Tab::effect2 };
}

juce::String DevicePanel::tabLabel (Tab t) const
{
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return {};

    switch (t)
    {
        case Tab::instrument:
            if (track->isDrum())
                return track->pads[(size_t) track->perf.selectedPad].name.isNotEmpty()
                           ? track->pads[(size_t) track->perf.selectedPad].name
                           : "Pad " + juce::String (track->perf.selectedPad + 1);
            return getInstrumentDisplayName (track->instrumentType);
        case Tab::sample:
            return "Sample";
        case Tab::effect1:
        case Tab::effect2:
        {
            const int slot = t == Tab::effect1 ? 0 : 1;
            auto& fx = track->effects[(size_t) slot];
            const juce::String prefix = track->isDrum() ? (slot == 0 ? "Pad FX" : "Kit FX") : (slot == 0 ? "FX1" : "FX2");
            const auto name = fx.type == (int) EffectType::none ? juce::String ("Empty")
                                                                 : (fx.presetName.isNotEmpty() ? fx.presetName
                                                                                               : getEffectDisplayName ((EffectType) fx.type));
            return prefix + ": " + name;
        }
    }
    return {};
}

std::vector<DevicePanel::Page> DevicePanel::pagesFor (Tab t) const
{
    std::vector<Page> result;
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return result;

    auto addGroups = [&] (const DeviceSpec& spec, int device, int pad, const juce::StringArray& onlyGroups,
                          const juce::StringArray& skipIds)
    {
        juce::StringArray groups = spec.groups;
        for (auto& p : spec.params)
            if (! groups.contains (p.group))
                groups.add (p.group);

        for (auto& group : groups)
        {
            if (! onlyGroups.isEmpty() && ! onlyGroups.contains (group))
                continue;

            std::vector<ParamTarget> params;
            for (size_t i = 0; i < spec.params.size(); ++i)
                if (spec.params[i].group == group && ! skipIds.contains (spec.params[i].id))
                    params.push_back ({ device, pad, (int) i });

            for (size_t start = 0; start < params.size(); start += knobsPerPage)
            {
                Page page;
                page.name = group.isNotEmpty() ? group : "Main";
                if (params.size() > (size_t) knobsPerPage)
                    page.name << " " << juce::String ((int) (start / knobsPerPage) + 1);
                page.params.assign (params.begin() + (long) start,
                                    params.begin() + (long) juce::jmin (params.size(), start + (size_t) knobsPerPage));
                result.push_back (page);
            }
        }
    };

    const int pad = track->perf.selectedPad;

    switch (t)
    {
        case Tab::instrument:
        {
            const auto& spec = getInstrumentSpec (track->instrumentType);

            if (track->isDrum())
            {
                addGroups (getPadSpec(), ParamTarget::pad, pad, { "Page 1", "Page 2" }, {});
                addGroups (spec, ParamTarget::instrument, 0, {}, {});
                if (! result.empty() && result.back().name == "Main")
                    result.back().name = "Kit";
                break;
            }

            if (! track->macros.isEmpty())
            {
                Page macros;
                macros.name = "Macros";
                macros.macros = true;
                for (auto& id : track->macros)
                    if (const int index = spec.indexOf (id); index >= 0 && macros.params.size() < (size_t) knobsPerPage)
                        macros.params.push_back ({ ParamTarget::instrument, 0, index });
                if (! macros.params.empty())
                    result.push_back (macros);
            }

            if (track->instrumentType == InstrumentType::sampler)
                addGroups (spec, ParamTarget::instrument, 0, {}, { "gain", "sampleStart" });
            else
                addGroups (spec, ParamTarget::instrument, 0, {}, {});
            break;
        }

        case Tab::sample:
            // The sample view shows the waveform; knobs for gain/start/choke sit under it.
            if (track->isDrum())
                addGroups (getPadSpec(), ParamTarget::pad, pad, { "Sample" }, {});
            else
            {
                const auto& spec = getInstrumentSpec (track->instrumentType);
                Page page;
                page.name = "Sample";
                for (auto id : { "gain", "sampleStart", "envMode" })
                    if (const int index = spec.indexOf (id); index >= 0)
                        page.params.push_back ({ ParamTarget::instrument, 0, index });
                result.push_back (page);
            }
            break;

        case Tab::effect1:
        case Tab::effect2:
        {
            const int slot = t == Tab::effect1 ? 0 : 1;
            const auto type = (EffectType) track->effects[(size_t) slot].type;
            const int device = slot == 0 ? ParamTarget::effect1 : ParamTarget::effect2;

            std::vector<Page> fxPages;
            std::swap (result, fxPages);
            addGroups (getEffectSpec (type), device, 0, {}, {});
            std::swap (result, fxPages);

            // Pad FX always offers the selected pad's send amount first (DEV-21).
            if (track->isDrum() && slot == 0)
            {
                const int sendIndex = getPadSpec().indexOf ("send");
                if (sendIndex >= 0)
                {
                    if (fxPages.empty())
                        fxPages.push_back ({ "Main", {}, false });
                    auto& first = fxPages.front().params;
                    first.insert (first.begin(), { ParamTarget::pad, pad, sendIndex });
                    if (first.size() > (size_t) knobsPerPage)
                    {
                        Page overflow { fxPages.front().name + " 2", { first.begin() + knobsPerPage, first.end() }, false };
                        first.resize ((size_t) knobsPerPage);
                        fxPages.insert (fxPages.begin() + 1, overflow);
                    }
                }
            }

            result = fxPages;
            break;
        }
    }

    return result;
}

void DevicePanel::rebuild()
{
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return;

    // Tabs that no longer exist fall back to the instrument.
    auto tabs = availableTabs();
    if (std::find (tabs.begin(), tabs.end(), tab) == tabs.end())
        tab = Tab::instrument;

    pages = pagesFor (tab);
    pageIndex = juce::jlimit (0, juce::jmax (0, (int) pages.size() - 1), pageIndex);

    knobs.clear();
    if (! pages.empty())
    {
        auto& page = pages[(size_t) pageIndex];
        for (auto& target : page.params)
        {
            knobs.push_back (std::make_unique<ParamKnob> (studio, trackId, target));
            knobs.back()->setShowMacroLock (page.macros);
            addAndMakeVisible (*knobs.back());
        }
    }

    if (tab == Tab::sample)
    {
        if (sampleView == nullptr)
        {
            sampleView = std::make_unique<SampleView> (studio, nav, trackId);
            addAndMakeVisible (*sampleView);
        }
    }
    else
    {
        sampleView = nullptr;
    }

    resized();
    repaint();
}

void DevicePanel::studioChanged()
{
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return;

    // Rebuild when the device layout could have changed; otherwise just repaint.
    juce::String key;
    key << (int) track->instrumentType << "|" << track->effects[0].type << "|" << track->effects[1].type << "|"
        << track->perf.selectedPad << "|" << track->macros.joinIntoString (",") << "|" << (int) tab;

    if (key != structureKey)
    {
        structureKey = key;
        rebuild();
    }
    else
    {
        for (auto& k : knobs)
            k->repaint();
        repaint();
    }
}

void DevicePanel::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return;

    const auto colour = Theme::trackColour (track->colour);
    auto tabs = availableTabs();

    g.setFont (Theme::font (13.5f, true));
    for (size_t i = 0; i < tabAreas.size() && i < tabs.size(); ++i)
    {
        const bool selected = tabs[i] == tab;
        auto area = tabAreas[i].toFloat().reduced (2.0f, 4.0f);
        g.setColour (selected ? t.surfaceRaised : t.surface);
        g.fillRoundedRectangle (area, 8.0f);
        if (selected)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (area.removeFromBottom (3.0f).reduced (8.0f, 0.0f), 1.5f);
        }
        g.setColour (selected ? t.text : t.textDim);
        g.drawFittedText (tabLabel (tabs[i]), tabAreas[i].reduced (8, 0), juce::Justification::centred, 1, 0.7f);
    }

    if (collapsed)
        return;

    g.setFont (Theme::font (12.5f, true));
    for (size_t i = 0; i < pageAreas.size() && i < pages.size(); ++i)
    {
        const bool selected = (int) i == pageIndex;
        auto area = pageAreas[i].toFloat().reduced (2.0f, 3.0f);
        if (selected)
        {
            g.setColour (t.surfaceRaised);
            g.fillRoundedRectangle (area, area.getHeight() * 0.5f);
        }
        g.setColour (selected ? t.text : t.textDim);
        g.drawText (pages[i].name, pageAreas[i], juce::Justification::centred);
    }

    if (pages.empty() && tab != Tab::sample)
    {
        g.setColour (t.textDim);
        g.setFont (Theme::font (14.0f));
        g.drawText ("Tap the tab again to choose an effect", getLocalBounds().withTrimmedTop (tabHeight),
                    juce::Justification::centred);
    }
}

void DevicePanel::resized()
{
    auto bounds = getLocalBounds();
    auto tabRow = bounds.removeFromTop (tabHeight);

    tabAreas.clear();
    auto tabs = availableTabs();
    if (! tabs.empty())
    {
        // The instrument tab gets a bit more room.
        const int count = (int) tabs.size();
        const int first = tabRow.getWidth() * (count == 4 ? 28 : 36) / 100;
        tabAreas.push_back (tabRow.removeFromLeft (first));
        const int other = tabRow.getWidth() / juce::jmax (1, count - 1);
        for (int i = 1; i < count; ++i)
            tabAreas.push_back (i == count - 1 ? tabRow : tabRow.removeFromLeft (other));
    }

    for (auto& k : knobs)
        k->setVisible (! collapsed);
    if (sampleView != nullptr)
        sampleView->setVisible (! collapsed);
    if (collapsed)
        return;

    pageAreas.clear();
    if (pages.size() > 1)
    {
        auto pageRow = bounds.removeFromTop (pageBarHeight).reduced (4, 0);
        const int w = juce::jmin (110, pageRow.getWidth() / (int) pages.size());
        for (size_t i = 0; i < pages.size(); ++i)
            pageAreas.push_back (pageRow.removeFromLeft (w));
    }

    if (sampleView != nullptr)
    {
        // Short panels (landscape): waveform beside the knobs instead of above them.
        if (bounds.getHeight() < 190 && ! knobs.empty())
        {
            auto knobArea = bounds.removeFromRight (juce::jmin (bounds.getWidth() * 2 / 5, 80 * (int) knobs.size()));
            sampleView->setBounds (bounds.reduced (4, 2));
            const int w = knobArea.getWidth() / (int) knobs.size();
            for (auto& k : knobs)
            {
                k->setBounds (knobArea.removeFromLeft (w).withSizeKeepingCentre (w, juce::jmin (knobArea.getHeight(), 96)).reduced (2));
                k->setCompact (true);
            }
            return;
        }

        const int knobRow = knobs.empty() ? 0 : 84;
        sampleView->setBounds (bounds.removeFromTop (bounds.getHeight() - knobRow).reduced (4, 2));
        const int w = bounds.getWidth() / juce::jmax (4, (int) knobs.size());
        for (auto& k : knobs)
            k->setBounds (bounds.removeFromLeft (w).reduced (2));
        return;
    }

    // Two rows of four knobs.
    const int columns = 4;
    const int rows = juce::jmax (1, ((int) knobs.size() + columns - 1) / columns);
    const int cellW = bounds.getWidth() / columns;
    const int cellH = juce::jmin (100, bounds.getHeight() / juce::jmax (2, rows));
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int row = (int) i / columns, column = (int) i % columns;
        knobs[i]->setBounds (bounds.getX() + column * cellW, bounds.getY() + row * cellH, cellW, cellH);
        knobs[i]->setCompact (cellH < 84);
    }
}

void DevicePanel::mouseUp (const juce::MouseEvent& e)
{
    auto tabs = availableTabs();
    for (size_t i = 0; i < tabAreas.size() && i < tabs.size(); ++i)
        if (tabAreas[i].contains (e.getPosition()))
        {
            if (tabs[i] == tab || collapsed)
            {
                if (collapsed) { tab = tabs[i]; setCollapsed (false); if (onTabChanged) onTabChanged(); }
                else           browse (tab);
            }
            else
            {
                tab = tabs[i];
                pageIndex = 0;
                structureKey = {};
                studioChanged();
                if (onTabChanged != nullptr)
                    onTabChanged();
            }
            return;
        }

    for (size_t i = 0; i < pageAreas.size(); ++i)
        if (pageAreas[i].contains (e.getPosition()))
        {
            pageIndex = (int) i;
            rebuild();
            return;
        }
}

void DevicePanel::browse (Tab t)
{
    switch (t)
    {
        case Tab::instrument: nav.showSheet (std::make_unique<BrowserSheet> (studio, trackId, BrowserSheet::Mode::trackPreset)); break;
        case Tab::effect1:    nav.showSheet (std::make_unique<BrowserSheet> (studio, trackId, BrowserSheet::Mode::effect1)); break;
        case Tab::effect2:    nav.showSheet (std::make_unique<BrowserSheet> (studio, trackId, BrowserSheet::Mode::effect2)); break;
        case Tab::sample:     nav.showSheet (std::make_unique<BrowserSheet> (studio, trackId, BrowserSheet::Mode::sample)); break;
    }
}

} // namespace rb
