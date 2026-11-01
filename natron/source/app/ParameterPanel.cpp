#include "ParameterPanel.h"

ParameterPanel::ParameterPanel(const ParameterSet& parametersToShow, Setter onChange, juce::String titleText) :
    parameters(parametersToShow), setter(std::move(onChange)), title(std::move(titleText)) {
    for (int i = 0; i < parameters.size(); i++) {
        const auto& spec = parameters.getSpec(i);
        Control control;
        control.id = spec.id;

        control.label = std::make_unique<juce::Label>(spec.id, spec.name.toUpperCase());
        control.label->setJustificationType(juce::Justification::centred);
        control.label->setMinimumHorizontalScale(0.6f);
        control.label->setColour(juce::Label::textColourId, NatronLookAndFeel::textDim);
        control.label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*control.label);

        if (!spec.choices.isEmpty()) {
            control.menu = std::make_unique<juce::ComboBox>(spec.name);
            for (int c = 0; c < spec.choices.size(); c++) {
                control.menu->addItem(spec.choices[c], c + 1);
            }
            control.menu->onChange = [this, id = spec.id, min = spec.minValue, menu = control.menu.get()] {
                setter(id, min + static_cast<float>(menu->getSelectedId() - 1));
            };
            addAndMakeVisible(*control.menu);
        } else if (spec.isToggle()) {
            control.toggle = std::make_unique<juce::ToggleButton>();
            control.toggle->onClick = [this, id = spec.id, button = control.toggle.get()] {
                setter(id, button->getToggleState() ? 1.0f : 0.0f);
            };
            addAndMakeVisible(*control.toggle);
        } else {
            control.slider = std::make_unique<TouchKnob>();
            control.slider->setRange(spec.minValue, spec.maxValue, spec.interval);
            control.slider->setDoubleClickReturnValue(true, spec.defaultValue);
            control.slider->setScrollWheelEnabled(false); // wheel scrolls the page, never turns a knob
            control.slider->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            control.slider->setPopupDisplayEnabled(true, true, nullptr); // the value shows while turning
            control.slider->setNumDecimalPlacesToDisplay(spec.interval > 0.0f ? 0 : 2);
            // Wide ranges (Hz, seconds) are easier to set logarithmically.
            if (spec.minValue > 0.0f && spec.maxValue / spec.minValue >= 100.0f) {
                control.slider->setSkewFactorFromMidPoint(std::sqrt(spec.minValue * spec.maxValue));
            }
            control.slider->onValueChange = [this, id = spec.id, slider = control.slider.get()] {
                setter(id, static_cast<float>(slider->getValue()));
            };
            addAndMakeVisible(*control.slider);
        }
        controls.push_back(std::move(control));
    }
    sections = PanelLayouts::sectionsFor({}, parameters);
    refresh();
}

void ParameterPanel::setLayout(const juce::String& type, juce::Colour colour) {
    sections = PanelLayouts::sectionsFor(type, parameters);
    panelColour = colour;
    // Inside a section, labels drop a leading repeat of its title ("KICK TUNE" under KICK: "TUNE").
    for (const auto& section : sections) {
        const auto prefix = section.title.toUpperCase() + " ";
        for (const int index : section.parameters) {
            const auto name = parameters.getSpec(index).name.toUpperCase();
            if (section.title.isNotEmpty() && name.startsWith(prefix)) {
                controls[static_cast<size_t>(index)].label->setText(name.substring(prefix.length()), juce::dontSendNotification);
            }
        }
    }
    resized();
    repaint();
}

void ParameterPanel::setAccentColour(juce::Colour colour) {
    accentColour = colour;
    for (auto& control : controls) {
        if (control.slider != nullptr) {
            control.slider->setColour(juce::Slider::rotarySliderFillColourId, colour);
        }
    }
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

ParameterPanel::Layout ParameterPanel::layoutFor(int width, int height) const {
    // Try 1-4 rows of sections and 1-4 control rows per row of sections; keep the arrangement whose
    // smallest knob is biggest. Cells stop growing at maxCellWidth / maxCellHeight; sections pack
    // left to right and the rows sit together, centred top to bottom, so nothing stretches.
    Layout layout;
    layout.placed.assign(sections.size(), {});
    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(gap);
    if (title.isNotEmpty()) {
        area.removeFromTop(titleHeight - gap);
    }
    if (sections.empty() || area.isEmpty()) {
        return layout;
    }
    const auto headerOf = [](const PanelLayouts::Section& s) {
        return s.title.isNotEmpty() ? NatronLookAndFeel::sectionTabHeight + 2 : gap;
    };

    if (columnsLayout) {
        const int count = static_cast<int>(sections.size());
        const int columnWidth = juce::jmin(maxCellWidth + 2 * gap, (area.getWidth() - (count - 1) * gap) / count);
        int x = area.getX();
        for (size_t s = 0; s < sections.size(); s++) {
            const int controlsHere = juce::jmax(1, static_cast<int>(sections[s].parameters.size()));
            const int cellHeight = juce::jmin(maxCellHeight, (area.getHeight() - headerOf(sections[s]) - gap - columnFooter) / controlsHere);
            layout.placed[s] = { { x, area.getY(), columnWidth, area.getHeight() }, 1, columnWidth - 2 * gap, cellHeight };
            x += columnWidth + gap;
        }
        layout.naturalWidth = x;
        const int shift = juce::jmax(0, (width - layout.naturalWidth) / 2);
        for (auto& place : layout.placed) {
            place.area.translate(shift, 0);
        }
        return layout;
    }

    int total = 0;
    for (const auto& section : sections) {
        total += static_cast<int>(section.parameters.size());
    }
    float bestScore = -1.0f;
    const int maxRows = juce::jmin(4, static_cast<int>(sections.size()));
    for (int rows = 1; rows <= maxRows; rows++) {
        // Split the sections, in order, into `rows` runs of about equal control counts.
        std::vector<std::pair<size_t, size_t>> runs; // [begin, end)
        size_t begin = 0;
        int counted = 0;
        for (int r = 0; r < rows; r++) {
            const int target = total * (r + 1) / rows;
            const size_t lastAllowed = sections.size() - static_cast<size_t>(rows - r - 1); // leave one per later row
            size_t end = begin;
            while (end < lastAllowed
                   && (end == begin || counted + static_cast<int>(sections[end].parameters.size()) / 2 <= target)) {
                counted += static_cast<int>(sections[end].parameters.size());
                end++;
            }
            if (r == rows - 1) {
                end = sections.size();
            }
            runs.push_back({ begin, end });
            begin = end;
        }

        const int rowSpace = (area.getHeight() - (rows - 1) * gap) / rows;
        std::vector<Placed> candidate(sections.size());
        std::vector<int> rowHeights;
        float score = 1.0e9f;
        int widest = 0;
        for (const auto& [from, to] : runs) {
            const int sectionsHere = static_cast<int>(to - from);
            const int spare = area.getWidth() - (sectionsHere - 1) * gap - sectionsHere * 2 * gap;
            float rowBest = -1.0f;
            int rowHeight = 0;
            for (int inner = 1; inner <= 4; inner++) { // control rows inside each section
                int columns = 0;
                int header = 0;
                for (size_t s = from; s < to; s++) {
                    columns += (static_cast<int>(sections[s].parameters.size()) + inner - 1) / inner;
                    header = juce::jmax(header, headerOf(sections[s]));
                }
                const int cellWidth = juce::jmin(maxCellWidth, spare / juce::jmax(1, columns));
                const int cellHeight = juce::jmin(maxCellHeight, (rowSpace - header - gap) / inner);
                const float knob = static_cast<float>(juce::jmin(cellWidth - 6, cellHeight - labelHeight - 4));
                if (knob <= rowBest + 0.5f) {
                    continue;
                }
                rowBest = knob;
                rowHeight = header + inner * cellHeight + gap;
                int x = area.getX();
                for (size_t s = from; s < to; s++) {
                    const int sectionColumns = (static_cast<int>(sections[s].parameters.size()) + inner - 1) / inner;
                    const int sectionWidth = sectionColumns * cellWidth + 2 * gap;
                    candidate[s] = { { x, 0, sectionWidth, rowHeight }, sectionColumns, cellWidth, cellHeight };
                    x += sectionWidth + gap;
                }
                widest = juce::jmax(widest, x - gap - area.getX());
            }
            rowHeights.push_back(rowHeight);
            score = juce::jmin(score, juce::jmin(static_cast<float>(maxKnob), rowBest));
        }
        if (score > bestScore + 0.5f) {
            bestScore = score;
            // Rows stack from the top, the block centred in the height.
            int used = (rows - 1) * gap;
            for (const int h : rowHeights) {
                used += h;
            }
            int y = area.getY() + juce::jmax(0, (area.getHeight() - used) / 2);
            for (size_t r = 0; r < runs.size(); r++) {
                for (size_t s = runs[r].first; s < runs[r].second; s++) {
                    candidate[s].area.setY(y);
                }
                y += rowHeights[r] + gap;
            }
            layout.placed = candidate;
            layout.naturalWidth = widest + 2 * gap;
        }
    }
    // Centre the packed sections in the width they were given.
    const int shift = juce::jmax(0, (width - layout.naturalWidth) / 2);
    for (auto& place : layout.placed) {
        place.area.translate(shift, 0);
    }
    return layout;
}

int ParameterPanel::getNaturalWidth(int maxWidth, int height) const {
    return juce::jmin(maxWidth, layoutFor(maxWidth, height).naturalWidth);
}

juce::Rectangle<int> ParameterPanel::getColumnFooter(size_t section) const {
    if (section >= placed.size() || columnFooter <= 0) {
        return {};
    }
    const auto& area = placed[section].area;
    return area.withTop(area.getBottom() - columnFooter - gap).withHeight(columnFooter).reduced(gap, 0);
}

void ParameterPanel::paint(juce::Graphics& g) {
    NatronLookAndFeel::drawPanel(g, getLocalBounds().toFloat(), panelColour);
    if (title.isNotEmpty()) {
        auto bar = getLocalBounds().removeFromTop(titleHeight).reduced(gap + 2, 5);
        g.setColour(accentColour);
        g.fillRoundedRectangle(bar.removeFromLeft(4).toFloat(), 2.0f);
        bar.removeFromLeft(8);
        g.setColour(NatronLookAndFeel::text);
        g.setFont(NatronLookAndFeel::labelFont(juce::jlimit(10.0f, 14.0f, static_cast<float>(getWidth()) / 12.0f)));
        g.drawFittedText(title.toUpperCase(), bar, juce::Justification::centredLeft, 1, 0.7f);
    }
    for (size_t s = 0; s < sections.size() && s < placed.size(); s++) {
        NatronLookAndFeel::drawSection(g, placed[s].area.toFloat(), panelColour, sections[s].title, accentColour);
    }
}

void ParameterPanel::resized() {
    placed = layoutFor(getWidth(), getHeight()).placed;
    for (size_t s = 0; s < sections.size() && s < placed.size(); s++) {
        const auto& place = placed[s];
        const int header = sections[s].title.isNotEmpty() ? NatronLookAndFeel::sectionTabHeight + 2 : gap;
        // Centre the grid in its section.
        const int count = static_cast<int>(sections[s].parameters.size());
        const int rows = (count + place.columns - 1) / place.columns;
        const int footer = columnsLayout ? columnFooter : 0;
        const int top = place.area.getY() + header
                        + juce::jmax(0, (place.area.getHeight() - header - gap - footer - rows * place.cellHeight) / 2);
        const int left = place.area.getX() + gap
                         + juce::jmax(0, (place.area.getWidth() - 2 * gap - place.columns * place.cellWidth) / 2);
        const int knobSize = juce::jlimit(18, 72, juce::jmin(place.cellWidth - 6, place.cellHeight - labelHeight - 4));
        const float fontHeight = juce::jlimit(8.5f, 11.0f, static_cast<float>(knobSize) * 0.2f + 2.0f);

        for (int k = 0; k < count; k++) {
            const juce::Rectangle<int> cell(left + (k % place.columns) * place.cellWidth,
                                            top + (k / place.columns) * place.cellHeight, place.cellWidth, place.cellHeight);
            auto& control = controls[static_cast<size_t>(sections[s].parameters[static_cast<size_t>(k)])];
            control.label->setFont(NatronLookAndFeel::labelFont(fontHeight));
            const auto inner = cell.reduced(2, 1);
            if (control.menu != nullptr) {
                const int menuHeight = juce::jlimit(22, 32, knobSize / 2 + 8);
                auto block = inner.withSizeKeepingCentre(inner.getWidth(), menuHeight + labelHeight + 2);
                control.menu->setBounds(block.removeFromTop(menuHeight));
                control.label->setBounds(block.withTrimmedTop(2));
            } else {
                auto block = inner.withSizeKeepingCentre(inner.getWidth(), knobSize + labelHeight);
                const auto knob = block.removeFromTop(knobSize).withSizeKeepingCentre(knobSize, knobSize);
                control.label->setBounds(block);
                if (control.slider != nullptr) {
                    control.slider->setBounds(knob);
                } else {
                    const int size = juce::jmin(30, knobSize);
                    control.toggle->setBounds(knob.withSizeKeepingCentre(size, size));
                }
            }
        }
    }
}
