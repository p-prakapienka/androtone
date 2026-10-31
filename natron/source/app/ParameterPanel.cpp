#include "ParameterPanel.h"

#include "NatronLookAndFeel.h"

ParameterPanel::ParameterPanel(const ParameterSet& parametersToShow, Setter onChange, juce::String titleText) :
    parameters(parametersToShow), setter(std::move(onChange)), title(std::move(titleText)) {
    for (int i = 0; i < parameters.size(); i++) {
        const auto& spec = parameters.getSpec(i);
        Control control;
        control.id = spec.id;

        const auto makeLabel = [&] {
            control.label = std::make_unique<juce::Label>(spec.id, spec.name.toUpperCase());
            control.label->setJustificationType(juce::Justification::centred);
            control.label->setFont(NatronLookAndFeel::labelFont(10.5f));
            control.label->setMinimumHorizontalScale(0.7f);
            control.label->setColour(juce::Label::textColourId, NatronLookAndFeel::ink);
            addAndMakeVisible(*control.label);
        };

        if (!spec.choices.isEmpty()) {
            control.menu = std::make_unique<juce::ComboBox>(spec.name);
            for (int c = 0; c < spec.choices.size(); c++) {
                control.menu->addItem(spec.choices[c], c + 1);
            }
            control.menu->onChange = [this, id = spec.id, min = spec.minValue, menu = control.menu.get()] {
                setter(id, min + static_cast<float>(menu->getSelectedId() - 1));
            };
            addAndMakeVisible(*control.menu);
            makeLabel();
        } else if (spec.isToggle()) {
            control.toggle = std::make_unique<juce::ToggleButton>(); // its caption is a label above, like a knob's
            control.toggle->onClick = [this, id = spec.id, button = control.toggle.get()] {
                setter(id, button->getToggleState() ? 1.0f : 0.0f);
            };
            addAndMakeVisible(*control.toggle);
            makeLabel();
        } else {
            control.slider = std::make_unique<TouchKnob>();
            control.slider->setRange(spec.minValue, spec.maxValue, spec.interval);
            control.slider->setDoubleClickReturnValue(true, spec.defaultValue);
            control.slider->setScrollWheelEnabled(false); // wheel scrolls the page, never turns a knob
            control.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, cellWidth() - 14, 16);
            control.slider->setNumDecimalPlacesToDisplay(spec.interval > 0.0f ? 0 : 2);
            // Wide ranges (Hz, seconds) are easier to set logarithmically.
            if (spec.minValue > 0.0f && spec.maxValue / spec.minValue >= 100.0f) {
                control.slider->setSkewFactorFromMidPoint(std::sqrt(spec.minValue * spec.maxValue));
            }
            control.slider->onValueChange = [this, id = spec.id, slider = control.slider.get()] {
                setter(id, static_cast<float>(slider->getValue()));
            };
            addAndMakeVisible(*control.slider);
            makeLabel();
        }
        controls.push_back(std::move(control));
    }
    sections = PanelLayouts::sectionsFor({}, parameters);
    refresh();
}

void ParameterPanel::setLayout(const juce::String& type, juce::Colour colour) {
    sections = PanelLayouts::sectionsFor(type, parameters);
    panelColour = colour;
    for (auto& control : controls) {
        if (control.label != nullptr) {
            control.label->setColour(juce::Label::textColourId, textColour());
        }
        if (control.toggle != nullptr) {
            control.toggle->setColour(juce::ToggleButton::textColourId, textColour());
        }
    }
    // Inside a section, labels drop a leading repeat of its title ("KICK TUNE" under KICK: "TUNE").
    for (const auto& section : sections) {
        const auto prefix = section.title.toUpperCase() + " ";
        for (const int index : section.parameters) {
            auto& control = controls[static_cast<size_t>(index)];
            const auto name = parameters.getSpec(index).name.toUpperCase();
            if (section.title.isNotEmpty() && name.startsWith(prefix)) {
                const auto shortName = name.substring(prefix.length());
                if (control.label != nullptr) {
                    control.label->setText(shortName, juce::dontSendNotification);
                }
            }
        }
    }
    resized();
    repaint();
}

void ParameterPanel::refresh() {
    for (size_t i = 0; i < controls.size(); i++) {
        const float value = parameters.get(static_cast<int>(i));
        if (controls[i].menu != nullptr) {
            controls[i].menu->setSelectedId(juce::roundToInt(value - parameters.getSpec(static_cast<int>(i)).minValue) + 1,
                                            juce::dontSendNotification);
        } else if (controls[i].slider != nullptr) {
            controls[i].slider->setValue(value, juce::dontSendNotification);
        } else {
            controls[i].toggle->setToggleState(value >= 0.5f, juce::dontSendNotification);
        }
    }
}

std::vector<ParameterPanel::Placed> ParameterPanel::place(int width, int& height) const {
    // Sections flow left to right and wrap. A section as wide as the panel wraps its controls.
    std::vector<Placed> placed;
    const int top = title.isNotEmpty() ? titleHeight : padding;
    const int usable = juce::jmax(cellWidth() + 2 * padding, width - 2 * padding);
    int x = padding;
    int y = top;
    int rowHeight = 0;
    for (const auto& section : sections) {
        const int count = static_cast<int>(section.parameters.size());
        const int maxColumns = juce::jmax(1, (usable - 2 * padding) / cellWidth());
        const int columns = juce::jlimit(1, maxColumns, count);
        const int rows = (count + columns - 1) / columns;
        const int sectionWidth = columns * cellWidth() + 2 * padding;
        const int sectionHeight = (section.title.isNotEmpty() ? NatronLookAndFeel::sectionTabHeight : padding)
                                  + rows * cellHeight() + padding;
        if (x > padding && x + sectionWidth > padding + usable) {
            x = padding;
            y += rowHeight + padding;
            rowHeight = 0;
        }
        placed.push_back({ { x, y, sectionWidth, sectionHeight }, columns });
        x += sectionWidth + padding;
        rowHeight = juce::jmax(rowHeight, sectionHeight);
    }
    height = y + rowHeight + padding;
    return placed;
}

void ParameterPanel::setCompact(bool shouldBeCompact) {
    compact = shouldBeCompact;
    for (auto& control : controls) {
        if (control.slider != nullptr) {
            control.slider->setTextBoxStyle(compact ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, false,
                                            cellWidth() - 14, 16);
            control.slider->setPopupDisplayEnabled(compact, true, nullptr);
        }
        if (control.label != nullptr) {
            control.label->setFont(NatronLookAndFeel::labelFont(compact ? 9.0f : 10.5f));
        }
    }
    resized();
    repaint();
}

int ParameterPanel::headerHeight(const PanelLayouts::Section& section) const {
    return section.title.isNotEmpty() ? NatronLookAndFeel::sectionTabHeight : padding;
}

std::vector<ParameterPanel::Placed> ParameterPanel::placeInColumns(int height, int& width) const {
    // Each section is as many control rows tall as fit (wrapping its controls into more columns);
    // short sections stack under each other while they fit, so the panel fills the height.
    std::vector<Placed> placed;
    const int top = title.isNotEmpty() ? titleHeight : padding;
    const int available = juce::jmax(cellHeight() + NatronLookAndFeel::sectionTabHeight + padding, height - top - padding);
    int x = padding;
    int y = top;
    int columnWidth = 0;
    for (const auto& section : sections) {
        const int count = static_cast<int>(section.parameters.size());
        const int rowsThatFit = juce::jmax(1, (available - headerHeight(section) - padding) / cellHeight());
        const int rows = juce::jmin(count, rowsThatFit);
        const int columns = (count + rows - 1) / rows;
        const int sectionWidth = columns * cellWidth() + 2 * padding;
        const int sectionHeight = headerHeight(section) + rows * cellHeight() + padding;
        const bool fitsBelow = y > top && y + sectionHeight <= top + available && sectionWidth <= columnWidth;
        if (!fitsBelow && y > top) {
            x += columnWidth + padding;
            y = top;
            columnWidth = 0;
        }
        placed.push_back({ { x, y, sectionWidth, sectionHeight }, columns });
        y += sectionHeight + padding;
        columnWidth = juce::jmax(columnWidth, sectionWidth);
    }
    width = x + columnWidth + padding;
    return placed;
}

int ParameterPanel::getWidthForHeight(int height) const {
    int width = 0;
    placeInColumns(height, width);
    return width;
}

std::vector<ParameterPanel::Placed> ParameterPanel::currentPlacement() const {
    int size = 0;
    return fitToHeight ? placeInColumns(getHeight(), size) : place(getWidth(), size);
}

int ParameterPanel::getHeightForWidth(int width) const {
    int height = 0;
    place(width, height);
    return height;
}

void ParameterPanel::paint(juce::Graphics& g) {
    NatronLookAndFeel::drawPanel(g, getLocalBounds().toFloat(), panelColour);

    if (title.isNotEmpty()) {
        auto bar = getLocalBounds().removeFromTop(titleHeight).reduced(padding, 4);
        if (!accentColour.isTransparent()) {
            g.setColour(accentColour);
            g.fillRoundedRectangle(bar.removeFromLeft(8).toFloat(), 2.0f);
            bar.removeFromLeft(6);
        }
        g.setColour(textColour());
        g.setFont(NatronLookAndFeel::labelFont(16.0f));
        g.drawText(title.toUpperCase(), bar, juce::Justification::centredLeft);
    }

    const auto placed = currentPlacement();
    for (size_t s = 0; s < sections.size(); s++) {
        NatronLookAndFeel::drawSection(g, placed[s].area.toFloat(), panelColour, sections[s].title,
                                       accentColour.isTransparent() ? NatronLookAndFeel::tabFill : accentColour);
    }
}

void ParameterPanel::applyScale() {
    // The largest cells (up to full size) at which every section fits the width.
    scale = 1.0f;
    if (!fitToBox) {
        return;
    }
    for (; scale > 0.42f; scale -= 0.04f) {
        int width = 0;
        placeInColumns(getHeight(), width);
        if (width <= getWidth()) {
            break;
        }
    }
    const bool small = scale < 0.8f;
    for (auto& control : controls) {
        if (control.slider != nullptr) { // small knobs show their value while turning instead of in a box
            control.slider->setTextBoxStyle(small || compact ? juce::Slider::NoTextBox : juce::Slider::TextBoxBelow, false,
                                            cellWidth() - 14, 16);
            control.slider->setPopupDisplayEnabled(small || compact, true, nullptr);
        }
        if (control.label != nullptr) {
            control.label->setFont(NatronLookAndFeel::labelFont(juce::jlimit(8.0f, 10.5f, 10.5f * scale + 1.5f)));
        }
    }
}

void ParameterPanel::resized() {
    applyScale();
    const auto placed = currentPlacement();
    for (size_t s = 0; s < sections.size(); s++) {
        const auto& area = placed[s].area;
        const int top = area.getY() + headerHeight(sections[s]);
        for (size_t k = 0; k < sections[s].parameters.size(); k++) {
            const int column = static_cast<int>(k) % placed[s].columns;
            const int row = static_cast<int>(k) / placed[s].columns;
            const juce::Rectangle<int> cell(area.getX() + padding + column * cellWidth(), top + row * cellHeight(),
                                            cellWidth(), cellHeight());
            auto& control = controls[static_cast<size_t>(sections[s].parameters[k])];
            auto inner = cell.reduced(3, 2);
            if (control.menu != nullptr) {
                control.label->setBounds(inner.removeFromTop(compact ? 12 : 16));
                control.menu->setBounds(compact ? inner.withSizeKeepingCentre(inner.getWidth(), 24)
                                                : inner.withSizeKeepingCentre(inner.getWidth(), 28).translated(0, -10));
            } else if (control.slider != nullptr) {
                control.label->setBounds(inner.removeFromTop(compact || scale < 0.8f ? 12 : 15));
                control.slider->setBounds(inner);
            } else {
                control.label->setBounds(inner.removeFromTop(compact || scale < 0.8f ? 12 : 15));
                const int size = juce::jmin(30, inner.getHeight(), inner.getWidth());
                control.toggle->setBounds(inner.withSizeKeepingCentre(size, size));
            }
        }
    }
}
