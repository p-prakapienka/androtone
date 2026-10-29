namespace rb
{

static Theme makeDark()
{
    Theme t;
    t.background    = juce::Colour (0xff111214);
    t.surface       = juce::Colour (0xff1c1d21);
    t.surfaceRaised = juce::Colour (0xff2a2c31);
    t.outline       = juce::Colour (0xff3a3d44);
    t.text          = juce::Colour (0xffeef0f3);
    t.textDim       = juce::Colour (0xff8c919b);
    t.accent        = juce::Colour (0xffffa53d);
    t.playing       = juce::Colour (0xff4fe08c);
    t.record        = juce::Colour (0xffff4d5e);
    t.danger        = juce::Colour (0xffff5c5c);
    return t;
}

static Theme makeHighContrast()
{
    Theme t;
    t.background    = juce::Colours::black;
    t.surface       = juce::Colour (0xff101010);
    t.surfaceRaised = juce::Colour (0xff262626);
    t.outline       = juce::Colour (0xffbdbdbd);
    t.text          = juce::Colours::white;
    t.textDim       = juce::Colour (0xffd8d8d8);
    t.accent        = juce::Colour (0xffffd400);
    t.playing       = juce::Colour (0xff00ff7f);
    t.record        = juce::Colour (0xffff3040);
    t.danger        = juce::Colour (0xffff3040);
    return t;
}

static Theme currentTheme = makeDark();

const Theme& Theme::get() { return currentTheme; }

void Theme::use (AppSettings::Theme which)
{
    currentTheme = which == AppSettings::Theme::highContrast ? makeHighContrast() : makeDark();
}

juce::Colour Theme::trackColour (int index)
{
    static const juce::uint32 colours[] = {
        0xffff7a59, 0xffffb547, 0xfff4e04d, 0xff9be15d, 0xff43d18f, 0xff3cc9d6, 0xff4f9dff, 0xff8a7dff,
        0xffc879ff, 0xffff6fb5, 0xffe0a07a, 0xffa3b8c9, 0xff6fd3b8, 0xffd9d36b, 0xffff9a8c, 0xff9ad0ff,
    };
    return juce::Colour (colours[(size_t) (index & 15)]);
}

juce::Font Theme::font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
juce::Path makeIcon (Icon icon)
{
    juce::Path p;
    using juce::MathConstants;

    switch (icon)
    {
        case Icon::play:
            p.addTriangle (0.28f, 0.18f, 0.28f, 0.82f, 0.82f, 0.5f);
            break;
        case Icon::stop:
            p.addRoundedRectangle (0.24f, 0.24f, 0.52f, 0.52f, 0.06f);
            break;
        case Icon::record:
            p.addEllipse (0.22f, 0.22f, 0.56f, 0.56f);
            break;
        case Icon::back:
        case Icon::chevronLeft:
            p.startNewSubPath (0.62f, 0.2f); p.lineTo (0.32f, 0.5f); p.lineTo (0.62f, 0.8f);
            break;
        case Icon::chevronRight:
            p.startNewSubPath (0.38f, 0.2f); p.lineTo (0.68f, 0.5f); p.lineTo (0.38f, 0.8f);
            break;
        case Icon::chevronDown:
            p.startNewSubPath (0.2f, 0.38f); p.lineTo (0.5f, 0.68f); p.lineTo (0.8f, 0.38f);
            break;
        case Icon::chevronUp:
            p.startNewSubPath (0.2f, 0.62f); p.lineTo (0.5f, 0.32f); p.lineTo (0.8f, 0.62f);
            break;
        case Icon::gear:
        {
            p.addEllipse (0.37f, 0.37f, 0.26f, 0.26f);
            for (int i = 0; i < 8; ++i)
            {
                const float a = i * MathConstants<float>::twoPi / 8.0f;
                p.startNewSubPath (0.5f + 0.22f * std::cos (a), 0.5f + 0.22f * std::sin (a));
                p.lineTo (0.5f + 0.36f * std::cos (a), 0.5f + 0.36f * std::sin (a));
            }
            p.addEllipse (0.24f, 0.24f, 0.52f, 0.52f);
            break;
        }
        case Icon::dots:
            for (int i = 0; i < 3; ++i)
                p.addEllipse (0.17f + i * 0.27f, 0.44f, 0.12f, 0.12f);
            break;
        case Icon::plus:
            p.startNewSubPath (0.5f, 0.2f); p.lineTo (0.5f, 0.8f);
            p.startNewSubPath (0.2f, 0.5f); p.lineTo (0.8f, 0.5f);
            break;
        case Icon::minus:
            p.startNewSubPath (0.2f, 0.5f); p.lineTo (0.8f, 0.5f);
            break;
        case Icon::close:
            p.startNewSubPath (0.26f, 0.26f); p.lineTo (0.74f, 0.74f);
            p.startNewSubPath (0.74f, 0.26f); p.lineTo (0.26f, 0.74f);
            break;
        case Icon::undo:
            p.startNewSubPath (0.3f, 0.36f); p.lineTo (0.62f, 0.36f);
            p.cubicTo (0.86f, 0.36f, 0.86f, 0.74f, 0.62f, 0.74f); p.lineTo (0.4f, 0.74f);
            p.startNewSubPath (0.42f, 0.22f); p.lineTo (0.28f, 0.36f); p.lineTo (0.42f, 0.5f);
            break;
        case Icon::redo:
            p.startNewSubPath (0.7f, 0.36f); p.lineTo (0.38f, 0.36f);
            p.cubicTo (0.14f, 0.36f, 0.14f, 0.74f, 0.38f, 0.74f); p.lineTo (0.6f, 0.74f);
            p.startNewSubPath (0.58f, 0.22f); p.lineTo (0.72f, 0.36f); p.lineTo (0.58f, 0.5f);
            break;
        case Icon::mixer:
            for (int i = 0; i < 3; ++i)
            {
                const float x = 0.27f + i * 0.23f;
                p.startNewSubPath (x, 0.18f); p.lineTo (x, 0.82f);
                const float y = i == 1 ? 0.36f : (i == 0 ? 0.62f : 0.52f);
                p.startNewSubPath (x - 0.08f, y); p.lineTo (x + 0.08f, y);
            }
            break;
        case Icon::scenes:
            for (int i = 0; i < 3; ++i)
            {
                p.addEllipse (0.18f, 0.24f + i * 0.22f, 0.08f, 0.08f);
                p.startNewSubPath (0.36f, 0.28f + i * 0.22f); p.lineTo (0.82f, 0.28f + i * 0.22f);
            }
            break;
        case Icon::duplicate:
            p.addRoundedRectangle (0.32f, 0.32f, 0.46f, 0.46f, 0.06f);
            p.startNewSubPath (0.22f, 0.66f); p.lineTo (0.22f, 0.22f); p.lineTo (0.66f, 0.22f);
            break;
        case Icon::quantize:
            for (int i = 0; i < 4; ++i)
            {
                const float x = 0.2f + i * 0.2f;
                p.startNewSubPath (x, 0.22f); p.lineTo (x, 0.78f);
            }
            p.addRectangle (0.2f, 0.42f, 0.2f, 0.16f);
            p.addRectangle (0.6f, 0.42f, 0.2f, 0.16f);
            break;
        case Icon::expand:
            p.startNewSubPath (0.2f, 0.42f); p.lineTo (0.2f, 0.2f); p.lineTo (0.42f, 0.2f);
            p.startNewSubPath (0.58f, 0.8f); p.lineTo (0.8f, 0.8f); p.lineTo (0.8f, 0.58f);
            p.startNewSubPath (0.2f, 0.2f); p.lineTo (0.42f, 0.42f);
            p.startNewSubPath (0.8f, 0.8f); p.lineTo (0.58f, 0.58f);
            break;
        case Icon::collapse:
            p.startNewSubPath (0.42f, 0.2f); p.lineTo (0.42f, 0.42f); p.lineTo (0.2f, 0.42f);
            p.startNewSubPath (0.58f, 0.8f); p.lineTo (0.58f, 0.58f); p.lineTo (0.8f, 0.58f);
            break;
        case Icon::heart:
        case Icon::heartFilled:
            p.startNewSubPath (0.5f, 0.82f);
            p.cubicTo (0.1f, 0.55f, 0.12f, 0.2f, 0.34f, 0.22f);
            p.cubicTo (0.44f, 0.22f, 0.5f, 0.3f, 0.5f, 0.36f);
            p.cubicTo (0.5f, 0.3f, 0.56f, 0.22f, 0.66f, 0.22f);
            p.cubicTo (0.88f, 0.2f, 0.9f, 0.55f, 0.5f, 0.82f);
            p.closeSubPath();
            break;
        case Icon::search:
            p.addEllipse (0.2f, 0.2f, 0.42f, 0.42f);
            p.startNewSubPath (0.56f, 0.56f); p.lineTo (0.8f, 0.8f);
            break;
        case Icon::mic:
            p.addRoundedRectangle (0.38f, 0.14f, 0.24f, 0.44f, 0.12f);
            p.startNewSubPath (0.26f, 0.46f);
            p.cubicTo (0.26f, 0.74f, 0.74f, 0.74f, 0.74f, 0.46f);
            p.startNewSubPath (0.5f, 0.68f); p.lineTo (0.5f, 0.84f);
            break;
        case Icon::folder:
            p.startNewSubPath (0.16f, 0.3f); p.lineTo (0.4f, 0.3f); p.lineTo (0.46f, 0.38f); p.lineTo (0.84f, 0.38f);
            p.lineTo (0.84f, 0.76f); p.lineTo (0.16f, 0.76f); p.closeSubPath();
            break;
        case Icon::trash:
            p.startNewSubPath (0.22f, 0.3f); p.lineTo (0.78f, 0.3f);
            p.startNewSubPath (0.4f, 0.3f); p.lineTo (0.42f, 0.2f); p.lineTo (0.58f, 0.2f); p.lineTo (0.6f, 0.3f);
            p.startNewSubPath (0.3f, 0.3f); p.lineTo (0.34f, 0.82f); p.lineTo (0.66f, 0.82f); p.lineTo (0.7f, 0.3f);
            break;
        case Icon::pads:
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 2; ++c)
                    p.addRoundedRectangle (0.18f + c * 0.34f, 0.18f + r * 0.34f, 0.28f, 0.28f, 0.05f);
            break;
        case Icon::keys:
            p.addRectangle (0.16f, 0.24f, 0.68f, 0.52f);
            for (int i = 1; i < 4; ++i)
            {
                p.startNewSubPath (0.16f + i * 0.17f, 0.5f); p.lineTo (0.16f + i * 0.17f, 0.76f);
            }
            for (int i = 0; i < 3; ++i)
                p.addRectangle (0.27f + i * 0.17f, 0.24f, 0.08f, 0.26f);
            break;
        case Icon::single:
            p.addRoundedRectangle (0.2f, 0.2f, 0.6f, 0.6f, 0.08f);
            break;
        case Icon::repeat:
            p.startNewSubPath (0.24f, 0.58f); p.lineTo (0.24f, 0.36f); p.lineTo (0.76f, 0.36f);
            p.startNewSubPath (0.64f, 0.24f); p.lineTo (0.76f, 0.36f); p.lineTo (0.64f, 0.48f);
            p.startNewSubPath (0.76f, 0.42f); p.lineTo (0.76f, 0.64f); p.lineTo (0.24f, 0.64f);
            p.startNewSubPath (0.36f, 0.52f); p.lineTo (0.24f, 0.64f); p.lineTo (0.36f, 0.76f);
            break;
        case Icon::octave:
            p.startNewSubPath (0.3f, 0.42f); p.lineTo (0.5f, 0.22f); p.lineTo (0.7f, 0.42f);
            p.startNewSubPath (0.3f, 0.58f); p.lineTo (0.5f, 0.78f); p.lineTo (0.7f, 0.58f);
            break;
        case Icon::save:
            p.startNewSubPath (0.5f, 0.18f); p.lineTo (0.5f, 0.62f);
            p.startNewSubPath (0.32f, 0.46f); p.lineTo (0.5f, 0.64f); p.lineTo (0.68f, 0.46f);
            p.startNewSubPath (0.2f, 0.66f); p.lineTo (0.2f, 0.82f); p.lineTo (0.8f, 0.82f); p.lineTo (0.8f, 0.66f);
            break;
        case Icon::share:
            p.startNewSubPath (0.5f, 0.62f); p.lineTo (0.5f, 0.16f);
            p.startNewSubPath (0.32f, 0.32f); p.lineTo (0.5f, 0.14f); p.lineTo (0.68f, 0.32f);
            p.startNewSubPath (0.3f, 0.46f); p.lineTo (0.2f, 0.46f); p.lineTo (0.2f, 0.84f); p.lineTo (0.8f, 0.84f);
            p.lineTo (0.8f, 0.46f); p.lineTo (0.7f, 0.46f);
            break;
        case Icon::check:
            p.startNewSubPath (0.2f, 0.52f); p.lineTo (0.42f, 0.74f); p.lineTo (0.8f, 0.28f);
            break;
        case Icon::triplet:
            for (int i = 0; i < 3; ++i)
                p.addEllipse (0.16f + i * 0.25f, 0.56f, 0.16f, 0.14f);
            p.startNewSubPath (0.18f, 0.4f); p.lineTo (0.18f, 0.3f); p.lineTo (0.82f, 0.3f); p.lineTo (0.82f, 0.4f);
            break;
        case Icon::metronome:
            p.startNewSubPath (0.38f, 0.16f); p.lineTo (0.62f, 0.16f); p.lineTo (0.78f, 0.84f); p.lineTo (0.22f, 0.84f);
            p.closeSubPath();
            p.startNewSubPath (0.5f, 0.68f); p.lineTo (0.72f, 0.3f);
            break;
        case Icon::waveform:
        {
            const float heights[] = { 0.2f, 0.5f, 0.3f, 0.7f, 0.4f, 0.6f, 0.25f };
            for (int i = 0; i < 7; ++i)
            {
                const float x = 0.17f + i * 0.11f;
                p.startNewSubPath (x, 0.5f - heights[i] * 0.4f); p.lineTo (x, 0.5f + heights[i] * 0.4f);
            }
            break;
        }
        case Icon::filter:
            p.startNewSubPath (0.14f, 0.4f); p.lineTo (0.5f, 0.4f);
            p.cubicTo (0.62f, 0.4f, 0.6f, 0.26f, 0.68f, 0.3f); p.lineTo (0.86f, 0.8f);
            break;
        case Icon::edit:
            p.startNewSubPath (0.22f, 0.78f); p.lineTo (0.26f, 0.62f); p.lineTo (0.66f, 0.22f); p.lineTo (0.78f, 0.34f);
            p.lineTo (0.38f, 0.74f); p.closeSubPath();
            break;
        case Icon::crop:
            p.startNewSubPath (0.3f, 0.14f); p.lineTo (0.3f, 0.7f); p.lineTo (0.86f, 0.7f);
            p.startNewSubPath (0.14f, 0.3f); p.lineTo (0.7f, 0.3f); p.lineTo (0.7f, 0.86f);
            break;
        case Icon::loop:
            p.addRoundedRectangle (0.18f, 0.3f, 0.64f, 0.4f, 0.2f);
            p.startNewSubPath (0.56f, 0.22f); p.lineTo (0.66f, 0.3f); p.lineTo (0.56f, 0.38f);
            break;
        case Icon::drum:
            p.addEllipse (0.18f, 0.26f, 0.64f, 0.22f);
            p.startNewSubPath (0.18f, 0.37f); p.lineTo (0.18f, 0.66f);
            p.cubicTo (0.18f, 0.84f, 0.82f, 0.84f, 0.82f, 0.66f); p.lineTo (0.82f, 0.37f);
            break;
        case Icon::synth:
            p.startNewSubPath (0.12f, 0.6f); p.lineTo (0.3f, 0.6f); p.lineTo (0.3f, 0.3f); p.lineTo (0.5f, 0.3f);
            p.lineTo (0.5f, 0.6f); p.lineTo (0.7f, 0.6f); p.lineTo (0.7f, 0.3f); p.lineTo (0.88f, 0.3f);
            break;
        case Icon::sampler:
            p.addEllipse (0.2f, 0.2f, 0.6f, 0.6f);
            p.startNewSubPath (0.32f, 0.5f); p.lineTo (0.42f, 0.36f); p.lineTo (0.5f, 0.62f); p.lineTo (0.58f, 0.42f);
            p.lineTo (0.68f, 0.5f);
            break;
        case Icon::wave:
            p.startNewSubPath (0.12f, 0.5f);
            p.cubicTo (0.24f, 0.1f, 0.36f, 0.1f, 0.5f, 0.5f);
            p.cubicTo (0.64f, 0.9f, 0.76f, 0.9f, 0.88f, 0.5f);
            break;
        case Icon::star:
            for (int i = 0; i < 10; ++i)
            {
                const float a = -MathConstants<float>::halfPi + i * MathConstants<float>::pi / 5.0f;
                const float r = i % 2 == 0 ? 0.36f : 0.15f;
                if (i == 0) p.startNewSubPath (0.5f + r * std::cos (a), 0.52f + r * std::sin (a));
                else        p.lineTo (0.5f + r * std::cos (a), 0.52f + r * std::sin (a));
            }
            p.closeSubPath();
            break;
    }
    return p;
}

void drawIcon (juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour, float strokeWidth)
{
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    auto box = area.withSizeKeepingCentre (size, size);
    auto path = makeIcon (icon);
    path.applyTransform (juce::AffineTransform::scale (size).translated (box.getX(), box.getY()));

    g.setColour (colour);
    const bool filled = icon == Icon::play || icon == Icon::stop || icon == Icon::record || icon == Icon::heartFilled
                     || icon == Icon::star || icon == Icon::dots;
    if (filled)
        g.fillPath (path);
    else
        g.strokePath (path, juce::PathStrokeType (strokeWidth * size / 24.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
}

//==============================================================================
RbLookAndFeel::RbLookAndFeel()
{
    refreshColours();
}

void RbLookAndFeel::refreshColours()
{
    const auto& t = Theme::get();
    setColour (juce::ResizableWindow::backgroundColourId, t.background);
    setColour (juce::DocumentWindow::backgroundColourId, t.background);
    setColour (juce::TextButton::buttonColourId, t.surfaceRaised);
    setColour (juce::TextButton::buttonOnColourId, t.accent);
    setColour (juce::TextButton::textColourOffId, t.text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::Label::textColourId, t.text);
    setColour (juce::PopupMenu::backgroundColourId, t.surfaceRaised);
    setColour (juce::PopupMenu::textColourId, t.text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.outline);
    setColour (juce::PopupMenu::highlightedTextColourId, t.text);
    setColour (juce::TextEditor::backgroundColourId, t.surfaceRaised);
    setColour (juce::TextEditor::textColourId, t.text);
    setColour (juce::TextEditor::outlineColourId, t.outline);
    setColour (juce::TextEditor::focusedOutlineColourId, t.accent);
    setColour (juce::TextEditor::highlightColourId, t.accent.withAlpha (0.4f));
    setColour (juce::CaretComponent::caretColourId, t.accent);
    setColour (juce::ListBox::backgroundColourId, t.background);
    setColour (juce::ScrollBar::thumbColourId, t.outline);
    setColour (juce::Slider::thumbColourId, t.text);
    setColour (juce::Slider::trackColourId, t.accent);
    setColour (juce::Slider::backgroundColourId, t.surfaceRaised);
    setColour (juce::ToggleButton::textColourId, t.text);
    setColour (juce::ToggleButton::tickColourId, t.accent);
    setColour (juce::AlertWindow::backgroundColourId, t.surfaceRaised);
    setColour (juce::AlertWindow::textColourId, t.text);
    setColour (juce::AlertWindow::outlineColourId, t.outline);
    setColour (juce::ComboBox::backgroundColourId, t.surfaceRaised);
    setColour (juce::ComboBox::textColourId, t.text);
    setColour (juce::ComboBox::outlineColourId, t.outline);
}

juce::Font RbLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return Theme::font (juce::jmin (16.0f, buttonHeight * 0.42f), true);
}

void RbLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                          bool isMouseOverButton, bool isButtonDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = button.getToggleState() ? button.findColour (juce::TextButton::buttonOnColourId) : backgroundColour;
    if (isButtonDown)
        colour = colour.brighter (0.15f);
    else if (isMouseOverButton)
        colour = colour.brighter (0.05f);
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.4f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, Theme::corner);
}

juce::Font RbLookAndFeel::getPopupMenuFont()
{
    return Theme::font (17.0f);
}

void RbLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                               int& idealWidth, int& idealHeight)
{
    LookAndFeel_V4::getIdealPopupMenuItemSize (text, isSeparator, standardMenuItemHeight, idealWidth, idealHeight);
    if (! isSeparator)
        idealHeight = juce::jmax (idealHeight, Theme::touch);
    idealWidth = juce::jmax (idealWidth + 24, 180);
}

void RbLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (Theme::get().surfaceRaised);
    g.setColour (Theme::get().outline);
    g.drawRect (0, 0, width, height);
}

juce::Font RbLookAndFeel::getLabelFont (juce::Label& label)
{
    return Theme::font (juce::jmin (17.0f, label.getHeight() * 0.6f));
}

void RbLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.hasKeyboardFocus (true) ? Theme::get().accent : Theme::get().outline);
    g.drawRoundedRectangle (juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f), Theme::corner, 1.0f);
}

void RbLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (Theme::get().surfaceRaised);
    g.fillRoundedRectangle (juce::Rectangle<float> (0, 0, (float) width, (float) height), Theme::corner);
}

void RbLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    // A switch: text on the left, pill on the right.
    const auto& t = Theme::get();
    auto bounds = button.getLocalBounds().toFloat();
    auto pill = bounds.removeFromRight (52.0f).withSizeKeepingCentre (44.0f, 26.0f);

    g.setColour (button.getToggleState() ? t.accent : t.outline);
    g.fillRoundedRectangle (pill, 13.0f);
    g.setColour (t.text);
    const float knobX = button.getToggleState() ? pill.getRight() - 23.0f : pill.getX() + 3.0f;
    g.fillEllipse (knobX, pill.getY() + 3.0f, 20.0f, 20.0f);

    g.setColour (button.isEnabled() ? t.text : t.textDim);
    g.setFont (Theme::font (17.0f));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centredLeft);
}

void RbLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto& t = Theme::get();
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto fill = slider.findColour (juce::Slider::trackColourId);

    if (style == juce::Slider::LinearVertical)
    {
        auto track = bounds.withSizeKeepingCentre (juce::jmin (28.0f, bounds.getWidth()), bounds.getHeight());
        g.setColour (t.surfaceRaised);
        g.fillRoundedRectangle (track, 6.0f);
        g.setColour (fill);
        g.fillRoundedRectangle (track.withTop (sliderPos), 6.0f);
    }
    else
    {
        auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), juce::jmin (28.0f, bounds.getHeight()));
        g.setColour (t.surfaceRaised);
        g.fillRoundedRectangle (track, 6.0f);
        g.setColour (fill);
        g.fillRoundedRectangle (track.withRight (sliderPos), 6.0f);
    }
}

} // namespace rb
