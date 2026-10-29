namespace rb
{

//==============================================================================
IconButton::IconButton (Icon i, const juce::String& l) : juce::Button (l), icon (i), label (l) {}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::get();
    auto bounds = getLocalBounds().toFloat().reduced (2.0f);

    const bool on = highlight || getToggleState();
    if (filled || on || down)
    {
        auto bg = on ? t.accent : t.surfaceRaised;
        if (down) bg = bg.brighter (0.15f);
        g.setColour (bg);
        g.fillRoundedRectangle (bounds, Theme::corner);
    }
    else if (highlighted)
    {
        g.setColour (t.surfaceRaised.withAlpha (0.5f));
        g.fillRoundedRectangle (bounds, Theme::corner);
    }

    auto colour = iconColour.value_or (on ? juce::Colours::black : t.text);
    if (! isEnabled())
        colour = colour.withMultipliedAlpha (0.35f);

    if (label.isNotEmpty() && bounds.getHeight() > 40.0f)
    {
        auto textArea = bounds.removeFromBottom (14.0f);
        g.setColour (colour.withMultipliedAlpha (0.8f));
        g.setFont (Theme::font (11.0f));
        g.drawText (label, textArea, juce::Justification::centred);
    }

    const float size = juce::jmin (24.0f, bounds.getHeight() * 0.7f, bounds.getWidth() * 0.7f);
    drawIcon (g, icon, bounds.withSizeKeepingCentre (size, size), colour);
}

//==============================================================================
PillButton::PillButton (const juce::String& text, Style s) : juce::Button (text), style (s) {}

void PillButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::get();
    auto bounds = getLocalBounds().toFloat().reduced (1.5f);
    const float radius = juce::jmin (bounds.getHeight() * 0.5f, 22.0f);

    juce::Colour bg, fg;
    switch (style)
    {
        case Style::accent: bg = t.accent;        fg = juce::Colours::black; break;
        case Style::record: bg = t.record;        fg = juce::Colours::white; break;
        case Style::subtle: bg = t.surface;       fg = t.textDim; break;
        case Style::outline: bg = juce::Colours::transparentBlack; fg = t.text; break;
        case Style::normal:
        default:            bg = t.surfaceRaised; fg = t.text; break;
    }

    if (getToggleState()) { bg = t.accent; fg = juce::Colours::black; }
    if (down) bg = bg.brighter (0.15f);
    else if (highlighted) bg = bg.brighter (0.05f);
    if (! isEnabled()) { bg = bg.withMultipliedAlpha (0.4f); fg = fg.withMultipliedAlpha (0.4f); }

    g.setColour (bg);
    g.fillRoundedRectangle (bounds, radius);
    if (style == Style::outline)
    {
        g.setColour (t.outline);
        g.drawRoundedRectangle (bounds, radius, 1.2f);
    }

    g.setColour (fg);
    g.setFont (Theme::font (juce::jmin (16.0f, bounds.getHeight() * 0.42f), true));
    g.drawText (getButtonText(), bounds.reduced (6.0f, 0.0f), juce::Justification::centred);
}

//==============================================================================
void LongPress::mouseDown (const juce::MouseEvent& e)
{
    fired = false;
    downPosition = e.getPosition();
    startTimer (delayMs);
}

void LongPress::mouseDrag (const juce::MouseEvent& e)
{
    if (! fired && e.getPosition().getDistanceFrom (downPosition) > moveTolerance)
        stopTimer();
}

bool LongPress::mouseUp()
{
    stopTimer();
    return fired;
}

void LongPress::timerCallback()
{
    stopTimer();
    fired = true;
    if (onLongPress != nullptr)
        onLongPress (downPosition);
}

//==============================================================================
ParamKnob::ParamKnob (Studio& s, int id, ParamTarget t) : studio (s), trackId (id), target (t)
{
    startTimerHz (15);
}

void ParamKnob::setTarget (int id, ParamTarget t)
{
    trackId = id;
    target = t;
    repaint();
}

static std::vector<float>* valuesFor (Track& track, const ParamTarget& target)
{
    switch (target.device)
    {
        case ParamTarget::instrument: return &track.instrument.values;
        case ParamTarget::effect1:    return &track.effects[0].values;
        case ParamTarget::effect2:    return &track.effects[1].values;
        case ParamTarget::pad:        return &track.pads[(size_t) juce::jlimit (0, numDrumPads - 1, target.padIndex)].values;
        default:                      return nullptr;
    }
}

const ParamSpec* ParamKnob::specFor (const Set& set, int trackId, const ParamTarget& target)
{
    auto* track = set.findTrack (trackId);
    if (track == nullptr)
        return nullptr;
    auto* spec = specForTarget (*track, target);
    if (spec == nullptr || target.paramIndex < 0 || target.paramIndex >= (int) spec->params.size())
        return nullptr;
    return &spec->params[(size_t) target.paramIndex];
}

float ParamKnob::valueFor (const Set& set, int trackId, const ParamTarget& target)
{
    auto* spec = specFor (set, trackId, target);
    if (spec == nullptr)
        return 0.0f;

    auto* track = set.findTrack (trackId);
    auto* values = valuesFor (const_cast<Track&> (*track), target);
    if (values == nullptr || target.paramIndex >= (int) values->size())
        return spec->defaultValue;
    return (*values)[(size_t) target.paramIndex];
}

void ParamKnob::setValue (Set& set, int trackId, const ParamTarget& target, float value)
{
    auto* track = set.findTrack (trackId);
    if (track == nullptr)
        return;
    auto* deviceSpec = specForTarget (*track, target);
    auto* values = valuesFor (*track, target);
    if (deviceSpec == nullptr || values == nullptr || target.paramIndex >= (int) deviceSpec->params.size())
        return;

    *values = conformValues (*deviceSpec, *values);
    (*values)[(size_t) target.paramIndex] = deviceSpec->params[(size_t) target.paramIndex].clamp (value);
    track->presetModified = true;
}

static const Clip* clipForKnob (Studio& studio, int trackId)
{
    auto* track = studio.getSet().findTrack (trackId);
    if (track == nullptr)
        return nullptr;
    const int slot = studio.getPlayingSlot (trackId);
    return slot >= 0 ? track->slots[(size_t) slot].get() : nullptr;
}

bool ParamKnob::isAutomated() const
{
    auto* clip = clipForKnob (studio, trackId);
    return clip != nullptr && clip->findLane (target) != nullptr;
}

float ParamKnob::displayValue() const
{
    if (! dragging)
        if (auto* clip = clipForKnob (studio, trackId))
            if (auto* lane = clip->findLane (target))
                return lane->valueAt (studio.getClipPosition (trackId));

    return valueFor (studio.getSet(), trackId, target);
}

void ParamKnob::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    auto* spec = specFor (studio.getSet(), trackId, target);
    if (spec == nullptr)
        return;

    auto* track = studio.getSet().findTrack (trackId);
    const auto colour = Theme::trackColour (track != nullptr ? track->colour : 0);
    const float value = displayValue();
    const float normalised = spec->toNormalised (value);

    auto bounds = getLocalBounds().toFloat();
    auto nameArea = bounds.removeFromBottom (compact ? 16.0f : 18.0f);
    auto valueArea = bounds.removeFromBottom (compact ? 14.0f : 16.0f);

    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    auto knob = bounds.withSizeKeepingCentre (size, size);
    const float start = juce::MathConstants<float>::pi * 1.25f;
    const float end = juce::MathConstants<float>::pi * 2.75f;
    const float stroke = juce::jmax (3.0f, size * 0.09f);

    juce::Path trackArc;
    trackArc.addCentredArc (knob.getCentreX(), knob.getCentreY(), size * 0.42f, size * 0.42f, 0.0f, start, end, true);
    g.setColour (t.surfaceRaised);
    g.strokePath (trackArc, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Bipolar ranges fill from the centre.
    const bool bipolar = spec->minValue < 0.0f && spec->maxValue > 0.0f && ! spec->isDiscrete();
    const float from = bipolar ? start + (end - start) * spec->toNormalised (0.0f) : start;
    const float to = start + (end - start) * normalised;

    juce::Path arc;
    arc.addCentredArc (knob.getCentreX(), knob.getCentreY(), size * 0.42f, size * 0.42f, 0.0f,
                       juce::jmin (from, to), juce::jmax (from, to), true);
    g.setColour (colour);
    g.strokePath (arc, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Pointer dot.
    const float px = knob.getCentreX() + std::sin (to) * size * 0.42f;
    const float py = knob.getCentreY() - std::cos (to) * size * 0.42f;
    g.setColour (t.text);
    g.fillEllipse (px - stroke * 0.7f, py - stroke * 0.7f, stroke * 1.4f, stroke * 1.4f);

    if (macroLock)
        drawIcon (g, Icon::star, knob.withSizeKeepingCentre (size * 0.26f, size * 0.26f), t.textDim);

    g.setColour (dragging ? t.text : t.textDim);
    g.setFont (Theme::font (compact ? 11.0f : 12.5f));
    g.drawText (spec->format (value), valueArea, juce::Justification::centred);

    g.setColour (t.text);
    g.setFont (Theme::font (compact ? 11.5f : 13.0f, true));
    g.drawFittedText (spec->name, nameArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);

    if (isAutomated())
    {
        g.setColour (colour);
        g.fillEllipse (nameArea.getCentreX() - 2.5f, nameArea.getBottom() - 3.0f, 5.0f, 5.0f);
    }
}

void ParamKnob::mouseDown (const juce::MouseEvent& e)
{
    downPosition = e.getPosition();
    dragging = false;
    if (auto* spec = specFor (studio.getSet(), trackId, target))
        dragStartNormalised = spec->toNormalised (valueFor (studio.getSet(), trackId, target));
}

void ParamKnob::mouseDrag (const juce::MouseEvent& e)
{
    auto* spec = specFor (studio.getSet(), trackId, target);
    if (spec == nullptr)
        return;

    const auto delta = e.getPosition() - downPosition;
    if (! dragging)
    {
        if (std::abs (delta.x) + std::abs (delta.y) < 5)
            return;
        dragging = true;
        pendingTaps = 0;
        stopTimer();
        startTimerHz (15);
        studio.getDocument().beginGesture();
    }

    // Up/right increases; a full sweep is ~220 px (DEV-10).
    const float sensitivity = spec->isDiscrete() ? 1.0f / juce::jmax (1.0f, (spec->maxValue - spec->minValue) * 28.0f / 220.0f)
                                                 : 1.0f;
    const float normalised = juce::jlimit (0.0f, 1.0f, dragStartNormalised + (delta.x - delta.y) / 220.0f * sensitivity);
    const float value = spec->fromNormalised (normalised);

    if (value != valueFor (studio.getSet(), trackId, target))
    {
        const auto t = target;
        const int id = trackId;
        studio.getDocument().edit ([=] (Set& s) { setValue (s, id, t, value); });
        studio.recordParameterChange (trackId, target, value);
    }
    repaint();
}

void ParamKnob::mouseUp (const juce::MouseEvent& e)
{
    if (dragging)
    {
        dragging = false;
        studio.getDocument().endGesture();
        repaint();
        return;
    }

    if (e.getNumberOfClicks() >= 2)
    {
        // Double tap: reset (unless automated, like Reset in the menu).
        pendingTaps = 0;
        stopTimer();
        startTimerHz (15);
        if (auto* spec = specFor (studio.getSet(), trackId, target); spec != nullptr && ! isAutomated())
        {
            const auto t = target;
            const int id = trackId;
            const float def = spec->defaultValue;
            studio.getDocument().edit ([=] (Set& s) { setValue (s, id, t, def); });
        }
        return;
    }

    // Single tap: wait briefly for a second tap before opening the options.
    pendingTaps = 1;
    stopTimer();
    startTimer (280);
}

void ParamKnob::timerCallback()
{
    if (pendingTaps > 0)
    {
        pendingTaps = 0;
        stopTimer();
        startTimerHz (15);
        showOptions();
        return;
    }

    if (isAutomated() && studio.isPlaying())
        repaint();
}

void ParamKnob::showOptions()
{
    auto* spec = specFor (studio.getSet(), trackId, target);
    if (spec == nullptr)
        return;

    auto* track = studio.getSet().findTrack (trackId);
    const int slot = studio.getPlayingSlot (trackId);
    const bool hasClip = track != nullptr && slot >= 0 && track->slots[(size_t) slot] != nullptr;
    const bool automated = isAutomated();

    juce::PopupMenu menu;
    menu.addSectionHeader (spec->name);
    menu.addItem (1, "Reset to Default", ! automated);
    menu.addItem (2, "Insert Automation", hasClip);
    menu.addItem (3, "Delete Automation", automated);

    juce::Component::SafePointer<ParamKnob> safe (this);
    showMenu (menu, *this, [safe, slot] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        auto& studio = safe->studio;
        const auto t = safe->target;
        const int id = safe->trackId;
        auto* sp = specFor (studio.getSet(), id, t);
        if (sp == nullptr)
            return;

        if (result == 1)
        {
            const float def = sp->defaultValue;
            studio.getDocument().edit ([=] (Set& s) { setValue (s, id, t, def); });
        }
        else if (result == 2 || result == 3)
        {
            const float current = valueFor (studio.getSet(), id, t);
            studio.getDocument().edit ([=] (Set& s)
            {
                auto* tr = s.findTrack (id);
                if (tr == nullptr || slot < 0 || tr->slots[(size_t) slot] == nullptr)
                    return;
                auto clip = std::make_shared<Clip> (*tr->slots[(size_t) slot]);
                auto& lanes = clip->automation;
                lanes.erase (std::remove_if (lanes.begin(), lanes.end(), [&] (auto& l) { return l.target == t; }), lanes.end());
                if (result == 2)
                {
                    AutomationLane lane;
                    lane.target = t;
                    lane.points = { { clip->loopStart, current }, { clip->loopEnd(), current } };
                    lanes.push_back (lane);
                }
                tr->slots[(size_t) slot] = clip;
            });
        }
        safe->repaint();
    });
}

//==============================================================================
Sheet::Sheet (const juce::String& t) : title (t)
{
    addAndMakeVisible (closeButton);
    closeButton.onClick = [this] { close(); };
}

void Sheet::close()
{
    if (onClose != nullptr)
        onClose();
}

void Sheet::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    g.setColour (t.surface);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 16.0f);

    g.setColour (t.outline);
    g.fillRoundedRectangle (juce::Rectangle<float> (40.0f, 4.0f).withCentre ({ getWidth() * 0.5f, 8.0f }), 2.0f);

    g.setColour (t.text);
    g.setFont (Theme::font (19.0f, true));
    g.drawText (title, getLocalBounds().removeFromTop (Theme::barHeight).reduced (20, 0), juce::Justification::centredLeft);
}

void Sheet::resized()
{
    closeButton.setBounds (getWidth() - Theme::touch - 8, (Theme::barHeight - Theme::touch) / 2, Theme::touch, Theme::touch);
}

juce::Rectangle<int> Sheet::getContentArea() const
{
    return getLocalBounds().withTrimmedTop (Theme::barHeight).reduced (16, 0).withTrimmedBottom (12);
}

//==============================================================================
HeaderBar::HeaderBar()
{
    addAndMakeVisible (backButton);
    backButton.onClick = [this] { if (onBack != nullptr) onBack(); };
}

void HeaderBar::setTitle (const juce::String& t, bool m)
{
    title = t;
    modified = m;
    repaint();
}

void HeaderBar::setShowBack (bool b)
{
    backButton.setVisible (b);
    resized();
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto& t = Theme::get();
    g.setColour (t.text);
    g.setFont (Theme::font (19.0f, true));
    g.drawFittedText (title + (modified ? " *" : ""), titleArea, juce::Justification::centredLeft, 1, 0.8f);

    if (onTitleClicked != nullptr)
    {
        const float textWidth = juce::jmin ((float) titleArea.getWidth() - 20.0f,
                                            juce::GlyphArrangement::getStringWidth (Theme::font (19.0f, true), title + (modified ? " *" : "")));
        drawIcon (g, Icon::chevronDown,
                  juce::Rectangle<float> (titleArea.getX() + textWidth + 4.0f, (float) titleArea.getCentreY() - 8.0f, 16.0f, 16.0f),
                  t.textDim);
    }
}

void HeaderBar::resized()
{
    auto bounds = getLocalBounds();
    if (backButton.isVisible())
        backButton.setBounds (bounds.removeFromLeft (Theme::touch + 4).withSizeKeepingCentre (Theme::touch, Theme::touch));
    else
        bounds.removeFromLeft (16);

    for (auto it = rightComponents.rbegin(); it != rightComponents.rend(); ++it)
        (*it)->setBounds (bounds.removeFromRight (Theme::touch + 4).withSizeKeepingCentre (Theme::touch, Theme::touch));

    titleArea = bounds.reduced (4, 0);
}

void HeaderBar::mouseUp (const juce::MouseEvent& e)
{
    if (onTitleClicked != nullptr && titleArea.contains (e.getPosition()))
        onTitleClicked();
}

//==============================================================================
void showMenu (juce::PopupMenu& menu, juce::Component& target, std::function<void (int)> onResult)
{
    menu.setLookAndFeel (&target.getLookAndFeel());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withMinimumWidth (200)
                                                  .withStandardItemHeight (Theme::touch),
                        [onResult] (int result) { if (onResult != nullptr) onResult (result); });
}

void askForText (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> onDone)
{
    auto* window = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("text", initial);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([window, onDone] (int result)
    {
        if (result == 1 && onDone != nullptr)
            onDone (window->getTextEditorContents ("text").trim());
    }), true);
}

} // namespace rb
