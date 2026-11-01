#include "NatronLookAndFeel.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace {
    const juce::Colour keyTop { 0xff2c2f35 };
    const juce::Colour keyBottom { 0xff1f2125 };
    const juce::Colour ledOff { 0xff3a3226 };
}

NatronLookAndFeel::NatronLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Label::textColourId, text);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::TextButton::textColourOnId, ink);
    setColour(juce::TextButton::buttonColourId, keyTop);
    setColour(juce::TextButton::buttonOnColourId, accent);
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::backgroundColourId, lcd);
    setColour(juce::ComboBox::arrowColourId, textDim);
    setColour(juce::ComboBox::outlineColourId, line);
    setColour(juce::PopupMenu::backgroundColourId, surface);
    setColour(juce::PopupMenu::textColourId, text);
    setColour(juce::PopupMenu::headerTextColourId, textDim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour(juce::PopupMenu::highlightedTextColourId, ink);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, lcd);
    setColour(juce::Slider::textBoxOutlineColourId, line);
    setColour(juce::Slider::rotarySliderFillColourId, accent);
    setColour(juce::Slider::backgroundColourId, lcd);
    setColour(juce::Slider::trackColourId, accent);
    setColour(juce::ToggleButton::textColourId, text);
    setColour(juce::ToggleButton::tickColourId, accent);
    setColour(juce::TextEditor::backgroundColourId, lcd);
    setColour(juce::TextEditor::textColourId, text);
    setColour(juce::TextEditor::outlineColourId, line);
    setColour(juce::TextEditor::focusedOutlineColourId, accent);
    setColour(juce::ScrollBar::thumbColourId, line.brighter(0.3f));
    setColour(juce::AlertWindow::backgroundColourId, surface);
    setColour(juce::AlertWindow::textColourId, text);
    setColour(juce::AlertWindow::outlineColourId, line);
    setColour(juce::CaretComponent::caretColourId, accent);
    setColour(juce::ListBox::backgroundColourId, lcd);
    setColour(juce::ListBox::textColourId, text);
    setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffd9dbde));
    setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff17181b));
    setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff6c7076));
    setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, accent.withAlpha(0.25f));
    setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, accent.withAlpha(0.7f));
    setColour(juce::MidiKeyboardComponent::textLabelColourId, juce::Colour(0xff6c7076));
    setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha(0.35f));
}

juce::Font NatronLookAndFeel::labelFont(float height) {
    return juce::Font(juce::FontOptions(height, juce::Font::bold)).withExtraKerningFactor(0.08f);
}

juce::Font NatronLookAndFeel::displayFont(float height) {
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::bold));
}

void NatronLookAndFeel::drawPanel(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float corner) {
    g.setGradientFill(juce::ColourGradient(colour.brighter(0.04f), area.getX(), area.getY(), colour.darker(0.08f),
                                           area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle(area, corner);
    g.setColour(line);
    g.drawRoundedRectangle(area.reduced(0.5f), corner, 1.0f);
}

void NatronLookAndFeel::drawKey(juce::Graphics& g, juce::Rectangle<float> key, juce::Colour fill, bool pressed) {
    const auto top = pressed ? fill.darker(0.25f) : fill.brighter(0.06f);
    const auto bottom = pressed ? fill.darker(0.1f) : fill.darker(0.18f);
    g.setColour(juce::Colours::black.withAlpha(pressed ? 0.15f : 0.4f));
    g.fillRoundedRectangle(key.translated(0.0f, 1.0f), 5.0f);
    g.setGradientFill(juce::ColourGradient(top, 0.0f, key.getY(), bottom, 0.0f, key.getBottom(), false));
    g.fillRoundedRectangle(key, 5.0f);
    g.setColour(fill.getPerceivedBrightness() > 0.5f ? fill.darker(0.4f) : line.brighter(0.08f));
    g.drawRoundedRectangle(key.reduced(0.5f), 5.0f, 1.0f);
    if (!pressed) { // a hairline of light along the top edge
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawHorizontalLine(juce::roundToInt(key.getY() + 1.5f), key.getX() + 5.0f, key.getRight() - 5.0f);
    }
}

void NatronLookAndFeel::drawLed(juce::Graphics& g, juce::Rectangle<float> led, bool on) {
    if (on) {
        g.setColour(accent.withAlpha(0.3f));
        g.fillEllipse(led.expanded(2.5f));
    }
    g.setColour(on ? accent : ledOff);
    g.fillEllipse(led);
}

void NatronLookAndFeel::drawSection(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour panelColour,
                                    const juce::String& title, juce::Colour tabColour) {
    g.setColour(panelColour.brighter(0.06f));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour(line.withAlpha(0.7f));
    g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);
    if (title.isEmpty()) {
        return;
    }
    auto header = area.withHeight(static_cast<float>(sectionTabHeight)).reduced(8.0f, 0.0f).translated(0.0f, 3.0f);
    g.setColour(tabColour);
    g.fillRoundedRectangle(header.removeFromLeft(3.0f).reduced(0.0f, 3.0f), 1.5f);
    header.removeFromLeft(6.0f);
    g.setColour(textDim);
    g.setFont(labelFont(10.5f));
    g.drawText(title.toUpperCase(), header, juce::Justification::centredLeft);
}

void NatronLookAndFeel::drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> a, juce::Colour colour) {
    g.setColour(colour);
    juce::Path p;
    const auto stroke = juce::PathStrokeType(juce::jmax(1.5f, a.getWidth() * 0.1f), juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded);
    switch (icon) {
        case Icon::play:
            p.addTriangle(a.getX() + a.getWidth() * 0.15f, a.getY(), a.getX() + a.getWidth() * 0.15f, a.getBottom(), a.getRight(),
                          a.getCentreY());
            g.fillPath(p);
            break;
        case Icon::stop:
            g.fillRoundedRectangle(a.reduced(a.getWidth() * 0.08f), 2.0f);
            break;
        case Icon::record:
            g.fillEllipse(a.reduced(a.getWidth() * 0.04f));
            break;
        case Icon::menu:
            for (int i = 0; i < 3; i++) {
                const float y = a.getY() + a.getHeight() * (0.2f + 0.3f * static_cast<float>(i));
                g.fillRoundedRectangle(a.getX(), y - a.getHeight() * 0.06f, a.getWidth(), a.getHeight() * 0.12f, 1.5f);
            }
            break;
        case Icon::timeline: { // blocks on rows
            const float h = a.getHeight() / 3.0f;
            g.fillRoundedRectangle(a.getX(), a.getY(), a.getWidth() * 0.55f, h * 0.7f, 1.5f);
            g.fillRoundedRectangle(a.getX() + a.getWidth() * 0.35f, a.getY() + h, a.getWidth() * 0.65f, h * 0.7f, 1.5f);
            g.fillRoundedRectangle(a.getX() + a.getWidth() * 0.15f, a.getY() + 2.0f * h, a.getWidth() * 0.5f, h * 0.7f, 1.5f);
            break;
        }
        case Icon::save: { // a tray with an arrow down into it
            p.startNewSubPath(a.getCentreX(), a.getY());
            p.lineTo(a.getCentreX(), a.getY() + a.getHeight() * 0.62f);
            p.startNewSubPath(a.getCentreX() - a.getWidth() * 0.25f, a.getY() + a.getHeight() * 0.38f);
            p.lineTo(a.getCentreX(), a.getY() + a.getHeight() * 0.64f);
            p.lineTo(a.getCentreX() + a.getWidth() * 0.25f, a.getY() + a.getHeight() * 0.38f);
            p.startNewSubPath(a.getX(), a.getY() + a.getHeight() * 0.7f);
            p.lineTo(a.getX(), a.getBottom());
            p.lineTo(a.getRight(), a.getBottom());
            p.lineTo(a.getRight(), a.getY() + a.getHeight() * 0.7f);
            g.strokePath(p, stroke);
            break;
        }
        case Icon::metronome: { // a pyramid with a swinging arm
            p.startNewSubPath(a.getX() + a.getWidth() * 0.3f, a.getY());
            p.lineTo(a.getX() + a.getWidth() * 0.7f, a.getY());
            p.lineTo(a.getRight(), a.getBottom());
            p.lineTo(a.getX(), a.getBottom());
            p.closeSubPath();
            g.strokePath(p, stroke);
            juce::Path arm;
            arm.startNewSubPath(a.getCentreX(), a.getY() + a.getHeight() * 0.8f);
            arm.lineTo(a.getRight() - a.getWidth() * 0.05f, a.getY() + a.getHeight() * 0.1f);
            g.strokePath(arm, stroke);
            break;
        }
        case Icon::plus:
            g.fillRoundedRectangle(a.withSizeKeepingCentre(a.getWidth(), a.getHeight() * 0.16f), 1.5f);
            g.fillRoundedRectangle(a.withSizeKeepingCentre(a.getWidth() * 0.16f, a.getHeight()), 1.5f);
            break;
        case Icon::cross:
            p.startNewSubPath(a.getTopLeft());
            p.lineTo(a.getBottomRight());
            p.startNewSubPath(a.getTopRight());
            p.lineTo(a.getBottomLeft());
            g.strokePath(p, stroke);
            break;
        case Icon::octaveUp:
        case Icon::octaveDown: {
            const bool up = icon == Icon::octaveUp;
            const float tip = up ? a.getY() + a.getHeight() * 0.2f : a.getBottom() - a.getHeight() * 0.2f;
            const float base = up ? a.getBottom() - a.getHeight() * 0.2f : a.getY() + a.getHeight() * 0.2f;
            p.startNewSubPath(a.getX(), base);
            p.lineTo(a.getCentreX(), tip);
            p.lineTo(a.getRight(), base);
            g.strokePath(p, stroke);
            break;
        }
    }
}

void NatronLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                         float startAngle, float endAngle, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f);
    const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto ring = bounds.withSizeKeepingCentre(diameter, diameter);
    const auto centre = ring.getCentre();
    const float angle = startAngle + position * (endAngle - startAngle);
    const float ringWidth = juce::jlimit(2.0f, 4.0f, diameter * 0.06f);
    const float radius = (diameter - ringWidth) * 0.5f;

    // The value ring: a dim track, and the value in the slider's colour.
    const auto capProperty = slider.getProperties()[capColourProperty];
    const auto ringColour = capProperty.isVoid() ? slider.findColour(juce::Slider::rotarySliderFillColourId)
                                                 : juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(capProperty)));
    const juce::PathStrokeType ringStroke(ringWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour(line);
    g.strokePath(track, ringStroke);
    // Bipolar ranges (pan, EQ gain) fill from the middle.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float from = bipolar ? startAngle + static_cast<float>(slider.valueToProportionOfLength(0.0)) * (endAngle - startAngle)
                               : startAngle;
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, radius, radius, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
    g.setColour(ringColour.withAlpha(slider.isEnabled() ? 1.0f : 0.35f));
    g.strokePath(value, ringStroke);

    // The knob: matte, a soft shadow below, a faint highlight on top.
    const auto body = ring.reduced(ringWidth + diameter * 0.08f);
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(body.translated(0.0f, diameter * 0.03f).expanded(1.0f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3b3f46), body.getCentreX(), body.getY(), juce::Colour(0xff1d1f23),
                                           body.getCentreX(), body.getBottom(), false));
    g.fillEllipse(body);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawEllipse(body.reduced(0.5f), 1.0f);

    // Pointer: from inside the knob to its edge.
    const float r = body.getWidth() * 0.5f;
    const juce::Point<float> a(centre.x + r * 0.25f * std::sin(angle), centre.y - r * 0.25f * std::cos(angle));
    const juce::Point<float> b(centre.x + r * 0.82f * std::sin(angle), centre.y - r * 0.82f * std::cos(angle));
    juce::Path pointer;
    pointer.startNewSubPath(a);
    pointer.lineTo(b);
    g.setColour(text);
    g.strokePath(pointer, juce::PathStrokeType(juce::jmax(1.6f, diameter * 0.05f), juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

juce::Label* NatronLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(displayFont(11.0f));
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::textColourId, textDim);
    return label;
}

void NatronLookAndFeel::drawBubble(juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&,
                                   const juce::Rectangle<float>& body) {
    g.setColour(lcd);
    g.fillRoundedRectangle(body, 4.0f);
    g.setColour(accent.withAlpha(0.6f));
    g.drawRoundedRectangle(body.reduced(0.5f), 4.0f, 1.0f);
}

void NatronLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                         juce::Slider& slider) {
    if (style != juce::Slider::LinearBar) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }
    // A display with a dim bar for the value (tempo, swing).
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
    g.setColour(lcd);
    g.fillRoundedRectangle(area, 5.0f);
    g.setColour(accent.withAlpha(0.16f));
    g.fillRoundedRectangle(area.withRight(sliderPos), 5.0f);
    g.setColour(line);
    g.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);
}

void NatronLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) {
    const auto area = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(lcd);
    g.fillRoundedRectangle(area, 5.0f);
    g.setColour(box.hasKeyboardFocus(false) ? accent.withAlpha(0.6f) : line);
    g.drawRoundedRectangle(area.reduced(0.5f), 5.0f, 1.0f);

    juce::Path chevron;
    const float cx = static_cast<float>(width) - 11.0f;
    const float cy = static_cast<float>(height) * 0.5f;
    chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
    chevron.lineTo(cx, cy + 2.0f);
    chevron.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 1.0f : 0.3f));
    g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font NatronLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    return juce::Font(juce::FontOptions(juce::jmin(14.0f, static_cast<float>(box.getHeight()) * 0.5f)));
}

void NatronLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(6, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void NatronLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                             bool isMouseOver, bool isButtonDown) {
    const auto area = button.getLocalBounds().toFloat().reduced(1.0f, 1.5f);
    const bool on = button.getToggleState();
    auto fill = on ? button.findColour(juce::TextButton::buttonOnColourId) : colour;
    if (isMouseOver && !isButtonDown) {
        fill = fill.brighter(0.06f);
    }
    drawKey(g, area, fill, isButtonDown);
    if (!button.isEnabled()) {
        g.setColour(background.withAlpha(0.5f));
        g.fillRoundedRectangle(area, 5.0f);
    }
}

void NatronLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) {
    const bool on = button.getToggleState();
    const auto fill = on ? button.findColour(juce::TextButton::buttonOnColourId) : button.findColour(juce::TextButton::buttonColourId);
    g.setColour((fill.getPerceivedBrightness() > 0.55f ? ink : text).withAlpha(button.isEnabled() ? 1.0f : 0.4f));
    g.setFont(getTextButtonFont(button, button.getHeight()));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 2), juce::Justification::centred, 2);
}

juce::Font NatronLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return labelFont(juce::jlimit(10.0f, 14.0f, static_cast<float>(buttonHeight) * 0.36f));
}

void NatronLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool, bool isButtonDown) {
    // A small dark key with an LED; a caption beside it if it has one.
    const auto area = button.getLocalBounds().toFloat();
    const float size = juce::jmin(26.0f, area.getHeight() - 4.0f, area.getWidth() - 4.0f);
    const bool captioned = button.getButtonText().isNotEmpty();
    const auto key = captioned ? juce::Rectangle<float>(area.getX() + 2.0f, area.getCentreY() - size * 0.5f, size, size)
                               : area.withSizeKeepingCentre(size, size);
    drawKey(g, key, keyTop, isButtonDown);
    drawLed(g, key.withSizeKeepingCentre(size * 0.32f, size * 0.32f), button.getToggleState());
    if (!captioned) {
        return;
    }
    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.setFont(labelFont(11.0f));
    g.drawFittedText(button.getButtonText().toUpperCase(), area.withTrimmedLeft(size + 8.0f).toNearestInt(),
                     juce::Justification::centredLeft, 2);
}

void NatronLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(surface);
    g.setColour(line);
    g.drawRect(0, 0, width, height);
}

void NatronLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) {
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillPath(path, juce::AffineTransform::translation(0.0f, 2.0f));
    g.setColour(surface);
    g.fillPath(path);
    g.setColour(line);
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void NatronLookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool) {
    const auto area = button.getActiveArea().toFloat().reduced(2.0f, 3.0f);
    drawKey(g, area, button.isFrontTab() ? accent : keyTop.brighter(isMouseOver ? 0.05f : 0.0f), false);
    g.setColour(button.isFrontTab() ? ink : text);
    g.setFont(labelFont(13.0f));
    g.drawText(button.getButtonText().toUpperCase(), area, juce::Justification::centred);
}

int NatronLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& button, int) {
    return juce::roundToInt(juce::GlyphArrangement::getStringWidth(labelFont(13.0f), button.getButtonText().toUpperCase())) + 36;
}
