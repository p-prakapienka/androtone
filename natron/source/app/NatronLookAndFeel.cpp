#include "NatronLookAndFeel.h"

namespace {
    const juce::Colour buttonTop { 0xffeceef0 };
    const juce::Colour buttonBottom { 0xffb9bcc1 };
    const juce::Colour edge { 0xff4a4d52 };
    const juce::Colour ledOn { 0xffffb238 };
}

NatronLookAndFeel::NatronLookAndFeel() {
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Label::textColourId, juce::Colour(0xffd5d8dc)); // on the charcoal; panels set ink
    setColour(juce::TextButton::textColourOffId, ink);
    setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    setColour(juce::TextButton::buttonColourId, buttonTop);
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff5d636b));
    setColour(juce::ComboBox::textColourId, lcdText);
    setColour(juce::ComboBox::backgroundColourId, lcd);
    setColour(juce::ComboBox::arrowColourId, lcdText);
    setColour(juce::ComboBox::outlineColourId, edge);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff26292e));
    setColour(juce::PopupMenu::textColourId, juce::Colour(0xffe6e8eb));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff4b6e8f));
    setColour(juce::Slider::textBoxTextColourId, lcdText);
    setColour(juce::Slider::textBoxBackgroundColourId, lcd);
    setColour(juce::Slider::textBoxOutlineColourId, edge);
    setColour(juce::Slider::rotarySliderFillColourId, ledOn);
    setColour(juce::ToggleButton::textColourId, ink);
    setColour(juce::ToggleButton::tickColourId, ledOn);
    setColour(juce::TextEditor::backgroundColourId, lcd);
    setColour(juce::TextEditor::textColourId, lcdText);
    setColour(juce::TextEditor::outlineColourId, edge);
    setColour(juce::ScrollBar::thumbColourId, juce::Colour(0xff8a9099));
    setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff2f3237));
    setColour(juce::AlertWindow::textColourId, juce::Colour(0xffe6e8eb));
}

juce::Font NatronLookAndFeel::labelFont(float height) {
    return juce::Font(juce::FontOptions(height, juce::Font::bold)).withExtraKerningFactor(0.06f);
}

void NatronLookAndFeel::drawPanel(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float corner) {
    g.setGradientFill(juce::ColourGradient(colour.brighter(0.12f), area.getX(), area.getY(), colour.darker(0.12f),
                                           area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle(area, corner);
    g.setColour(colour.darker(0.6f));
    g.drawRoundedRectangle(area.reduced(0.5f), corner, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.25f)); // top highlight
    g.drawHorizontalLine(juce::roundToInt(area.getY() + 1.5f), area.getX() + corner, area.getRight() - corner);
}

void NatronLookAndFeel::drawSection(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour panelColour,
                                    const juce::String& title) {
    // A recessed area, a shade lighter than the panel.
    g.setColour(panelColour.brighter(0.08f));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(panelColour.darker(0.35f));
    g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);

    if (title.isEmpty()) {
        return;
    }
    // The title tab: square on the left, a 45-degree cut on the right.
    const auto font = labelFont(12.0f);
    const float textWidth = juce::GlyphArrangement::getStringWidth(font, title.toUpperCase());
    const float height = static_cast<float>(sectionTabHeight) - 4.0f;
    const auto tab = juce::Rectangle<float>(area.getX(), area.getY(), textWidth + 16.0f + height, height);
    juce::Path shape;
    shape.startNewSubPath(tab.getX() + 4.0f, tab.getY());
    shape.lineTo(tab.getRight(), tab.getY());
    shape.lineTo(tab.getRight() - height, tab.getBottom());
    shape.lineTo(tab.getX(), tab.getBottom());
    shape.lineTo(tab.getX(), tab.getY() + 4.0f);
    shape.quadraticTo(tab.getX(), tab.getY(), tab.getX() + 4.0f, tab.getY());
    shape.closeSubPath();
    g.setColour(tabFill);
    g.fillPath(shape);
    g.setColour(juce::Colours::white);
    g.setFont(font);
    g.drawText(title.toUpperCase(), tab.withTrimmedLeft(8.0f).withTrimmedRight(height), juce::Justification::centredLeft);
}

void NatronLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                                         float startAngle, float endAngle, juce::Slider& slider) {
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
    const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const auto knob = bounds.withSizeKeepingCentre(diameter, diameter);
    const auto centre = knob.getCentre();
    const float angle = startAngle + position * (endAngle - startAngle);

    // Scale ticks around the knob, and the value arc.
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, diameter * 0.5f, diameter * 0.5f, 0.0f, startAngle, endAngle, true);
    g.setColour(ink.withAlpha(0.25f));
    g.strokePath(track, juce::PathStrokeType(2.0f));
    juce::Path value;
    value.addCentredArc(centre.x, centre.y, diameter * 0.5f, diameter * 0.5f, 0.0f, startAngle, angle, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId).withAlpha(slider.isEnabled() ? 0.9f : 0.3f));
    g.strokePath(value, juce::PathStrokeType(2.0f));

    // Drop shadow, dark rim, then the cap.
    const auto body = knob.reduced(diameter * 0.1f);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillEllipse(body.translated(0.0f, diameter * 0.05f).expanded(1.0f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5a5e64), body.getX(), body.getY(), juce::Colour(0xff15171a),
                                           body.getX(), body.getBottom(), false));
    g.fillEllipse(body);

    const auto cap = body.reduced(diameter * 0.07f);
    const auto capProperty = slider.getProperties()[capColourProperty];
    const auto capColour = capProperty.isVoid() ? juce::Colour(0xffd9dcdf) : juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(capProperty)));
    juce::ColourGradient face(capColour.brighter(0.5f), cap.getX() + cap.getWidth() * 0.3f, cap.getY() + cap.getHeight() * 0.2f,
                              capColour.darker(0.45f), cap.getRight(), cap.getBottom(), true);
    g.setGradientFill(face);
    g.fillEllipse(cap);
    g.setColour(juce::Colours::white.withAlpha(0.35f)); // a ring of light at the top edge
    g.drawEllipse(cap.reduced(0.5f), 1.0f);

    // Pointer: a short line from near the centre towards the rim.
    const float inner = cap.getWidth() * 0.18f;
    const float outer = cap.getWidth() * 0.44f;
    const juce::Point<float> from(centre.x + inner * std::sin(angle), centre.y - inner * std::cos(angle));
    const juce::Point<float> to(centre.x + outer * std::sin(angle), centre.y - outer * std::cos(angle));
    g.setColour(ink);
    g.drawLine({ from, to }, juce::jmax(2.0f, diameter * 0.06f));
}

juce::Label* NatronLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(juce::FontOptions(12.0f));
    label->setColour(juce::Label::backgroundColourId, lcd);
    label->setColour(juce::Label::outlineColourId, edge);
    label->setColour(juce::Label::textColourId, lcdText);
    return label;
}

void NatronLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                                         juce::Slider& slider) {
    if (style != juce::Slider::LinearBar) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }
    // An LCD with a lit bar for the value (the tempo control).
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
    g.setColour(lcd);
    g.fillRoundedRectangle(area, 3.0f);
    g.setColour(lcdText.withAlpha(0.18f));
    g.fillRoundedRectangle(area.withRight(sliderPos), 3.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(area.reduced(0.5f), 3.0f, 1.0f);
}

void NatronLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) {
    const auto area = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(lcd);
    g.fillRoundedRectangle(area, 3.0f);
    g.setColour(juce::Colours::black.withAlpha(0.5f)); // inner shadow along the top
    g.drawHorizontalLine(1, 3.0f, static_cast<float>(width) - 3.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(area.reduced(0.5f), 3.0f, 1.0f);

    juce::Path arrow;
    const float cx = static_cast<float>(width) - 10.0f;
    const float cy = static_cast<float>(height) * 0.5f;
    arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.3f));
    g.fillPath(arrow);
}

juce::Font NatronLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    return juce::Font(juce::FontOptions(juce::jmin(15.0f, static_cast<float>(box.getHeight()) * 0.55f)));
}

void NatronLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(4, 1, box.getWidth() - 20, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

void NatronLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& colour,
                                             bool isMouseOver, bool isButtonDown) {
    const auto area = button.getLocalBounds().toFloat().reduced(1.0f);
    const bool on = button.getToggleState();
    auto top = on ? button.findColour(juce::TextButton::buttonOnColourId) : colour;
    auto bottom = on ? top.darker(0.3f) : buttonBottom.interpolatedWith(colour, 0.15f);
    if (isButtonDown) {
        std::swap(top, bottom); // pressed in
    } else if (isMouseOver) {
        top = top.brighter(0.05f);
    }
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(area.translated(0.0f, 1.0f), 4.0f);
    g.setGradientFill(juce::ColourGradient(top, 0.0f, area.getY(), bottom, 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
    if (!button.isEnabled()) {
        g.setColour(NatronLookAndFeel::background.withAlpha(0.45f));
        g.fillRoundedRectangle(area, 4.0f);
    }
}

juce::Font NatronLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return labelFont(juce::jmin(15.0f, static_cast<float>(buttonHeight) * 0.42f));
}

void NatronLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool, bool isButtonDown) {
    // A small bevelled switch with an LED, the label beside it.
    const auto area = button.getLocalBounds().toFloat();
    const float size = juce::jmin(22.0f, area.getHeight() - 4.0f);
    const auto key = juce::Rectangle<float>(area.getX() + 2.0f, area.getCentreY() - size * 0.5f, size, size);
    g.setGradientFill(juce::ColourGradient(isButtonDown ? buttonBottom : buttonTop, 0.0f, key.getY(),
                                           isButtonDown ? buttonTop : buttonBottom, 0.0f, key.getBottom(), false));
    g.fillRoundedRectangle(key, 3.0f);
    g.setColour(edge);
    g.drawRoundedRectangle(key.reduced(0.5f), 3.0f, 1.0f);

    const auto led = key.reduced(size * 0.3f);
    const bool on = button.getToggleState();
    if (on) {
        g.setColour(ledOn.withAlpha(0.35f));
        g.fillEllipse(led.expanded(3.0f));
    }
    g.setColour(on ? ledOn : juce::Colour(0xff5b4a2c));
    g.fillEllipse(led);

    g.setColour(button.findColour(juce::ToggleButton::textColourId));
    g.setFont(labelFont(11.0f));
    g.drawFittedText(button.getButtonText().toUpperCase(),
                     area.withTrimmedLeft(size + 8.0f).toNearestInt(), juce::Justification::centredLeft, 2);
}

void NatronLookAndFeel::drawTabButton(juce::TabBarButton& button, juce::Graphics& g, bool isMouseOver, bool) {
    const auto area = button.getActiveArea().toFloat().reduced(2.0f, 3.0f);
    const bool front = button.isFrontTab();
    const auto fill = front ? juce::Colour(0xffd4d7db) : juce::Colour(0xff3a3d42).brighter(isMouseOver ? 0.1f : 0.0f);
    g.setGradientFill(juce::ColourGradient(fill.brighter(0.1f), 0.0f, area.getY(), fill.darker(0.15f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(front ? edge : juce::Colour(0xff17181b));
    g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
    g.setColour(front ? ink : juce::Colour(0xffc9ccd1));
    g.setFont(labelFont(14.0f));
    g.drawText(button.getButtonText().toUpperCase(), area, juce::Justification::centred);
}

int NatronLookAndFeel::getTabButtonBestWidth(juce::TabBarButton& button, int) {
    return juce::roundToInt(juce::GlyphArrangement::getStringWidth(labelFont(14.0f), button.getButtonText().toUpperCase())) + 36;
}
