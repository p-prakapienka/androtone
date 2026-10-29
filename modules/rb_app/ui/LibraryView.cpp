namespace rb
{

struct LibraryView::Row : public juce::Component
{
    Row (LibraryView& o, SetInfo i) : owner (o), info (std::move (i))
    {
        addAndMakeVisible (playButton);
        addAndMakeVisible (optionsButton);
        playButton.onClick = [this]
        {
            auto& studio = owner.studio;
            if (studio.getPreviewSetId() == info.id) studio.stopPreview();
            else studio.previewSet (info);
            owner.refresh();
        };
        optionsButton.onClick = [this] { owner.showOptions (info, optionsButton); };
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = Theme::get();
        auto bounds = getLocalBounds().toFloat().reduced (0.0f, 3.0f);
        g.setColour (t.surface);
        g.fillRoundedRectangle (bounds, 10.0f);

        // Artwork tile: a colour derived from the set id.
        auto tile = bounds.removeFromLeft (bounds.getHeight()).reduced (8.0f);
        g.setColour (Theme::trackColour ((int) (info.id.hashCode() & 15)).withAlpha (0.85f));
        g.fillRoundedRectangle (tile, 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        drawIcon (g, info.isDemo ? Icon::star : Icon::waveform, tile.reduced (tile.getWidth() * 0.25f),
                  juce::Colours::black.withAlpha (0.55f));

        auto text = bounds.withTrimmedRight (2.0f * Theme::touch + 8.0f).reduced (4.0f, 10.0f);
        g.setColour (t.text);
        g.setFont (Theme::font (17.0f, true));
        g.drawFittedText (info.name, text.removeFromTop (text.getHeight() * 0.55f).toNearestInt(),
                          juce::Justification::bottomLeft, 1, 0.8f);
        g.setColour (t.textDim);
        g.setFont (Theme::font (13.0f));
        g.drawText (info.isDemo ? juce::String ("Demo") : info.modified.toString (true, true, false),
                    text, juce::Justification::topLeft);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().reduced (6, 6);
        optionsButton.setBounds (bounds.removeFromRight (Theme::touch));
        playButton.setBounds (bounds.removeFromRight (Theme::touch));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked())
            owner.openSet (info);
    }

    void update()
    {
        const bool previewing = owner.studio.getPreviewSetId() == info.id;
        playButton.setIcon (previewing ? Icon::stop : Icon::play);
        playButton.setIconColour (previewing ? std::optional<juce::Colour> (Theme::get().playing) : std::nullopt);
    }

    LibraryView& owner;
    SetInfo info;
    IconButton playButton { Icon::play };
    IconButton optionsButton { Icon::dots };
};

LibraryView::LibraryView (Studio& s, Navigator& n) : studio (s), nav (n)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false, true, false);

    addAndMakeVisible (settingsButton);
    addAndMakeVisible (importButton);
    addAndMakeVisible (newButton);

    settingsButton.onClick = [this] { nav.showSheet (std::make_unique<AppSettingsSheet> (studio)); };
    importButton.onClick = [this] { importFile(); };
    newButton.onClick = [this] { openSet (studio.getLibrary().createNew()); };

    studio.addListener (this);
    refresh();
    startTimerHz (4);
}

LibraryView::~LibraryView()
{
    studio.removeListener (this);
}

void LibraryView::refresh()
{
    rows.clear();
    sectionLabels.clear();

    auto addSection = [&] (const juce::String& title, const std::vector<SetInfo>& sets)
    {
        auto label = std::make_unique<juce::Label> (juce::String(), title);
        label->setFont (Theme::font (14.0f, true));
        label->setColour (juce::Label::textColourId, Theme::get().textDim);
        content.addAndMakeVisible (*label);
        sectionLabels.push_back (std::move (label));

        for (auto& info : sets)
        {
            rows.push_back (std::make_unique<Row> (*this, info));
            content.addAndMakeVisible (*rows.back());
        }
    };

    addSection ("YOUR SETS", studio.getLibrary().listSets());
    addSection ("DEMOS", studio.getLibrary().listDemos());

    for (auto& r : rows)
        r->update();
    resized();
}

void LibraryView::studioChanged()
{
    for (auto& r : rows)
        r->update();
}

void LibraryView::timerCallback()
{
    for (auto& r : rows)
        r->update();
}

void LibraryView::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    g.fillAll (t.background);

    auto header = getLocalBounds().removeFromTop (72).reduced (20, 0);
    g.setColour (t.text);
    g.setFont (Theme::font (30.0f, true));
    g.drawText ("Riffbook", header, juce::Justification::centredLeft);

    g.setColour (t.surface);
    g.fillRect (getLocalBounds().removeFromBottom (Theme::barHeight + 16));
}

void LibraryView::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (72);

    auto bottom = bounds.removeFromBottom (Theme::barHeight + 16).reduced (12, 8);
    settingsButton.setBounds (bottom.removeFromLeft (Theme::touch + 8));
    importButton.setBounds (bottom.removeFromLeft (Theme::touch + 8));
    newButton.setBounds (bottom.removeFromRight (130));

    viewport.setBounds (bounds);

    const int width = bounds.getWidth() - 24;
    int y = 0;
    size_t rowIndex = 0;
    const auto sets = studio.getLibrary().listSets().size();

    for (size_t section = 0; section < sectionLabels.size(); ++section)
    {
        sectionLabels[section]->setBounds (12, y + 8, width, 28);
        y += 40;
        const size_t count = section == 0 ? sets : rows.size() - sets;
        for (size_t i = 0; i < count && rowIndex < rows.size(); ++i, ++rowIndex)
        {
            rows[rowIndex]->setBounds (12, y, width, 72);
            y += 72;
        }
        if (section == 0 && count == 0)
            y += 8;
    }

    content.setSize (bounds.getWidth(), y + 16);
}

void LibraryView::openSet (const SetInfo& info)
{
    studio.openSet (info);
    nav.showSession();
}

void LibraryView::showOptions (const SetInfo& info, juce::Component& anchor)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (info.name);
    menu.addItem (1, "Duplicate");
    menu.addItem (2, "Rename", ! info.isDemo);
    menu.addItem (3, "Share...");
    menu.addItem (4, "Delete", ! info.isDemo);

    juce::Component::SafePointer<LibraryView> safe (this);
    showMenu (menu, anchor, [safe, info] (int result)
    {
        if (safe == nullptr)
            return;
        auto& library = safe->studio.getLibrary();

        switch (result)
        {
            case 1: library.duplicate (info); safe->refresh(); break;
            case 2:
                askForText ("Rename Set", info.name, [safe, info] (juce::String name)
                {
                    if (safe != nullptr && name.isNotEmpty())
                    {
                        safe->studio.getLibrary().rename (info, name);
                        safe->refresh();
                    }
                });
                break;
            case 3:
            {
                if (safe->studio.getPreviewSetId() == info.id)
                    safe->studio.stopPreview();
                auto bundle = library.exportBundle (info);
                if (bundle.existsAsFile())
                    safe->shareBox = juce::ContentSharer::shareFilesScoped ({ juce::URL (bundle) }, nullptr, safe.getComponent());
                break;
            }
            case 4:
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete Set",
                                                    "Delete \"" + info.name + "\"? This cannot be undone.", "Delete", "Cancel",
                                                    nullptr, juce::ModalCallbackFunction::create ([safe, info] (int ok)
                {
                    if (safe == nullptr || ok == 0)
                        return;
                    if (safe->studio.getPreviewSetId() == info.id)
                        safe->studio.stopPreview();
                    safe->studio.getLibrary().remove (info);
                    safe->refresh();
                }));
                break;
            default: break;
        }
    });
}

void LibraryView::importFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a set or sample", juce::File(), "*.riffbook;*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<LibraryView> safe (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr || fc.getURLResults().isEmpty())
            return;

        const auto url = fc.getURLResults().getFirst();
        auto& library = safe->studio.getLibrary();

        // Copy the picked content into a local temp file first (works with Android content URIs).
        auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("import-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        {
            auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress));
            if (in == nullptr)
                return;
            juce::FileOutputStream out (temp);
            out.writeFromInputStream (*in, -1);
        }

        const auto name = url.getFileName();

        if (name.endsWithIgnoreCase (".riffbook"))
        {
            library.importBundle (temp);
        }
        else
        {
            // SMP-1: samples go to the user library.
            auto sample = SampleLibrary::loadFile (temp, safe->studio.getEngine().getSamples().getFormatManager());
            if (sample != nullptr)
            {
                auto target = library.getUserSamplesFolder().getNonexistentChildFile (
                    juce::File::createLegalFileName (juce::File (name).getFileNameWithoutExtension()), ".wav");
                SampleLibrary::writeWav (*sample, target);
            }
        }

        temp.deleteFile();
        safe->refresh();
    });
}

} // namespace rb
