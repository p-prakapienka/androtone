namespace rb
{

static juce::File appDataRoot()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Riffbook");
}

MainComponent::MainComponent()
{
    studio = std::make_unique<Studio> (appDataRoot());
    Theme::use (studio->getSettings().getTheme());
    lookAndFeel.refreshColours();
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    studio->getSettings().addChangeListener (this);

    addChildComponent (dimmer);
    dimmer.onTap = [this] { closeSheet(); };

    showLibrary();
    changeListenerCallback (nullptr);
    setSize (412, 892);
}

MainComponent::~MainComponent()
{
    studio->getSettings().removeChangeListener (this);
    sheet = nullptr;
    view = nullptr;
    touchIndicator = nullptr;
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
    studio = nullptr;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Theme::get().background);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    if (view != nullptr)
        view->setBounds (bounds);

    dimmer.setBounds (bounds);

    if (sheet != nullptr)
    {
        const int height = juce::roundToInt (bounds.getHeight() * sheet->heightFraction);
        const int width = juce::jmin (bounds.getWidth(), 640);
        sheet->setBounds (juce::Rectangle<int> (width, height).withCentre (bounds.getCentre())
                              .withBottomY (bounds.getBottom()));
    }

    if (touchIndicator != nullptr)
        touchIndicator->setBounds (bounds);
}

void MainComponent::setView (std::unique_ptr<juce::Component> newView)
{
    closeSheet();
    view = std::move (newView);
    addAndMakeVisible (*view, 0);
    resized();
}

void MainComponent::showLibrary()
{
    if (studio->isSetOpen())
        studio->closeSet();
    setView (std::make_unique<LibraryView> (*studio, *this));
}

void MainComponent::showSession()
{
    setView (std::make_unique<SessionView> (*studio, *this));
}

void MainComponent::showInstrument (int trackId, int slot)
{
    setView (std::make_unique<InstrumentView> (*studio, *this, trackId, slot));
}

void MainComponent::showSheet (std::unique_ptr<Sheet> newSheet)
{
    closeSheet();
    sheet = std::move (newSheet);
    sheet->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
                                                                { if (safe != nullptr) safe->closeSheet(); }); };
    dimmer.setVisible (true);
    dimmer.toFront (false);
    addAndMakeVisible (*sheet);
    sheet->toFront (true);
    if (touchIndicator != nullptr)
        touchIndicator->toFront (false);
    resized();
}

void MainComponent::closeSheet()
{
    if (sheet == nullptr)
        return;
    sheet = nullptr;
    dimmer.setVisible (false);
    if (view != nullptr)
        view->repaint();
}

bool MainComponent::goBack()
{
    if (sheet != nullptr)
    {
        closeSheet();
        return true;
    }
    if (dynamic_cast<InstrumentView*> (view.get()) != nullptr)
    {
        showSession();
        return true;
    }
    if (dynamic_cast<SessionView*> (view.get()) != nullptr)
    {
        showLibrary();
        return true;
    }
    return false;
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    auto& settings = studio->getSettings();

    Theme::use (settings.getTheme());
    lookAndFeel.refreshColours();
    studio->getSession().setMonitorInput (settings.getMonitorInput());

    if (settings.getShowTouches() && touchIndicator == nullptr)
    {
        touchIndicator = std::make_unique<TouchIndicator>();
        addAndMakeVisible (*touchIndicator);
        resized();
    }
    else if (! settings.getShowTouches())
    {
        touchIndicator = nullptr;
    }

    sendLookAndFeelChange();
    repaint();
}

void MainComponent::writeSnapshots (const juce::File& folder)
{
    folder.createDirectory();

    auto save = [&] (const juce::String& name)
    {
        auto image = createComponentSnapshot (getLocalBounds(), true, 2.0f);
        juce::PNGImageFormat png;
        auto file = folder.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        png.writeImageToStream (image, out);
    };

    showLibrary();
    save ("1-library");

    auto demos = studio->getLibrary().listDemos();
    if (! demos.empty())
    {
        studio->openSet (demos.front());
        showSession();
        save ("2-session");

        auto& set = studio->getSet();
        if (! set.tracks.empty())
        {
            showInstrument (set.tracks[0].id, 0);
            save ("3-instrument-drums");
        }
        if (set.tracks.size() > 2)
        {
            showInstrument (set.tracks[2].id, 0);
            save ("4-instrument-melodic");
        }

        // The expanded MIDI editor; the drum shot has a selection to show the note-edit menu.
        auto findEditor = [this]() -> ClipEditor*
        {
            for (auto* child : view->getChildren())
                if (auto* editor = dynamic_cast<ClipEditor*> (child))
                    return editor;
            return nullptr;
        };
        if (set.tracks.size() > 2)
        {
            showInstrument (set.tracks[2].id, 0);
            if (auto* editor = findEditor()) editor->setExpanded (true);
            save ("5-editor-melodic");
        }
        if (! set.tracks.empty())
        {
            showInstrument (set.tracks[0].id, 0);
            if (auto* editor = findEditor(); editor != nullptr && editor->clip() != nullptr)
            {
                editor->setExpanded (true);
                Selection selection;
                for (size_t i = 0; i < editor->clip()->notes.size(); ++i)
                    if (editor->clip()->notes[i].start < 2.0)
                        selection.push_back ((int) i);
                editor->setSelection (selection);
            }
            save ("6-editor-drums");
        }
    }
}

//==============================================================================
MainComponent::TouchIndicator::TouchIndicator()
{
    setInterceptsMouseClicks (false, false);
    juce::Desktop::getInstance().addGlobalMouseListener (this);
    startTimerHz (30);
}

MainComponent::TouchIndicator::~TouchIndicator()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
}

void MainComponent::TouchIndicator::paint (juce::Graphics& g)
{
    for (auto& [index, point] : touches)
    {
        const float alpha = fades.count (index) ? fades[index] : 1.0f;
        g.setColour (juce::Colours::white.withAlpha (0.35f * alpha));
        g.fillEllipse (juce::Rectangle<float> (36.0f, 36.0f).withCentre (point));
        g.setColour (juce::Colours::white.withAlpha (0.8f * alpha));
        g.drawEllipse (juce::Rectangle<float> (36.0f, 36.0f).withCentre (point), 1.5f);
    }
}

void MainComponent::TouchIndicator::mouseDown (const juce::MouseEvent& e)
{
    touches[e.source.getIndex()] = getLocalPoint (nullptr, e.getScreenPosition()).toFloat();
    fades.erase (e.source.getIndex());
    repaint();
}

void MainComponent::TouchIndicator::mouseDrag (const juce::MouseEvent& e) { mouseDown (e); }

void MainComponent::TouchIndicator::mouseUp (const juce::MouseEvent& e)
{
    fades[e.source.getIndex()] = 1.0f;
}

void MainComponent::TouchIndicator::timerCallback()
{
    for (auto it = fades.begin(); it != fades.end();)
    {
        it->second -= 0.12f;
        if (it->second <= 0.0f)
        {
            touches.erase (it->first);
            it = fades.erase (it);
        }
        else
        {
            ++it;
        }
    }
    repaint();
}

} // namespace rb
