#include "ParameterPanel.h"

ParameterPanel::ParameterPanel(const ParameterSet& parametersToShow, Setter onChange, juce::String titleText) :
    parameters(parametersToShow), setter(std::move(onChange)), title(std::move(titleText)) {
    for (int i = 0; i < parameters.size(); i++) {
        const auto& spec = parameters.getSpec(i);
        Control control;
        control.id = spec.id;

        if (spec.isToggle()) {
            control.toggle = std::make_unique<juce::ToggleButton>(spec.name);
            control.toggle->onClick = [this, id = spec.id, button = control.toggle.get()] {
                setter(id, button->getToggleState() ? 1.0f : 0.0f);
            };
            addAndMakeVisible(*control.toggle);
        } else {
            control.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                            juce::Slider::TextBoxBelow);
            control.slider->setRange(spec.minValue, spec.maxValue, spec.interval);
            control.slider->setDoubleClickReturnValue(true, spec.defaultValue);
            control.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, cellWidth - 8, 18);
            control.slider->setNumDecimalPlacesToDisplay(spec.interval > 0.0f ? 0 : 2);
            // Wide ranges (Hz, seconds) are easier to set logarithmically.
            if (spec.minValue > 0.0f && spec.maxValue / spec.minValue >= 100.0f) {
                control.slider->setSkewFactorFromMidPoint(std::sqrt(spec.minValue * spec.maxValue));
            }
            control.slider->onValueChange = [this, id = spec.id, slider = control.slider.get()] {
                setter(id, static_cast<float>(slider->getValue()));
            };
            addAndMakeVisible(*control.slider);

            control.label = std::make_unique<juce::Label>(spec.id, spec.name);
            control.label->setJustificationType(juce::Justification::centred);
            control.label->setFont(juce::FontOptions(12.0f));
            addAndMakeVisible(*control.label);
        }
        controls.push_back(std::move(control));
    }
    refresh();
}

void ParameterPanel::refresh() {
    for (size_t i = 0; i < controls.size(); i++) {
        const float value = parameters.get(static_cast<int>(i));
        if (controls[i].slider != nullptr) {
            controls[i].slider->setValue(value, juce::dontSendNotification);
        } else {
            controls[i].toggle->setToggleState(value >= 0.5f, juce::dontSendNotification);
        }
    }
}

int ParameterPanel::getHeightForWidth(int width) const {
    const int columns = columnsFor(width);
    const int rows = (static_cast<int>(controls.size()) + columns - 1) / columns;
    return (title.isNotEmpty() ? titleHeight : 0) + rows * cellHeight;
}

void ParameterPanel::paint(juce::Graphics& g) {
    if (title.isNotEmpty()) {
        g.setColour(juce::Colour(0xffc5c8c6));
        g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        g.drawText(title, getLocalBounds().removeFromTop(titleHeight).reduced(4, 0), juce::Justification::centredLeft);
    }
}

void ParameterPanel::resized() {
    const int columns = columnsFor(getWidth());
    const int top = title.isNotEmpty() ? titleHeight : 0;
    for (size_t i = 0; i < controls.size(); i++) {
        const int column = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        const juce::Rectangle<int> cell(column * cellWidth, top + row * cellHeight, cellWidth, cellHeight);
        auto& control = controls[i];
        if (control.slider != nullptr) {
            auto area = cell.reduced(4);
            control.label->setBounds(area.removeFromTop(16));
            control.slider->setBounds(area);
        } else {
            control.toggle->setBounds(cell.reduced(4).withSizeKeepingCentre(cellWidth - 8, 32));
        }
    }
}
