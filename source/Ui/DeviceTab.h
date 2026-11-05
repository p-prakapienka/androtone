#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

#include "../PluginProcessor.h"
#include "AndrotoneLookAndFeel.h"

// The selected track's devices: its instrument (Sampler / Synth / Drums) and its insert effects
// (filter + compressor). Controls are generated from the devices' parameter specs.
class DeviceTab : public juce::Component, private juce::Timer {
public:
    explicit DeviceTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        for (int t = 0; t < processorRef.getNumActiveTracks(); t++) {
            auto button = std::make_unique<juce::TextButton>(juce::String(t + 1));
            button->setClickingTogglesState(true);
            button->setRadioGroupId(1);
            button->setColour(juce::TextButton::buttonOnColourId, AndrotoneAudioProcessor::getTrackColour(t));
            button->onClick = [this, t]() { selectTrack(t); };
            addAndMakeVisible(*button);
            trackButtons.push_back(std::move(button));
        }

        instrumentChooser.addItemList({ "Sampler", "Synth", "Drums" }, 1);
        instrumentChooser.onChange = [this]() {
            processorRef.setInstrument(track, static_cast<InstrumentType>(instrumentChooser.getSelectedItemIndex()));
            buildControls();
        };
        addAndMakeVisible(instrumentChooser);

        for (auto* label : { &instrumentHeading, &effectsHeading }) {
            label->setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::ACCENT));
            addAndMakeVisible(*label);
        }
        effectsHeading.setText("Effects", juce::dontSendNotification);
        gainReduction.setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::TEXT_MUTED));
        addAndMakeVisible(gainReduction);

        selectTrack(0);
        startTimerHz(10);
    }

    void selectTrack(int t) {
        track = t;
        if (t < static_cast<int>(trackButtons.size())) {
            trackButtons[static_cast<size_t>(t)]->setToggleState(true, juce::dontSendNotification);
        }
        instrumentChooser.setSelectedItemIndex(static_cast<int>(processorRef.getInstrument(track)), juce::dontSendNotification);
        buildControls();
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        auto top = bounds.removeFromTop(34);
        for (auto& button : trackButtons) {
            button->setBounds(top.removeFromLeft(40).reduced(2));
        }
        top.removeFromLeft(12);
        instrumentChooser.setBounds(top.removeFromLeft(150).reduced(0, 3));
        bounds.removeFromTop(8);

        instrumentHeading.setBounds(bounds.removeFromTop(22));
        layoutKnobs(instrumentKnobs, bounds.removeFromTop(KNOB_ROW_HEIGHT * rowsFor(instrumentKnobs.size(), bounds.getWidth())));
        bounds.removeFromTop(10);
        auto heading = bounds.removeFromTop(22);
        effectsHeading.setBounds(heading.removeFromLeft(120));
        gainReduction.setBounds(heading);
        layoutKnobs(effectKnobs, bounds.removeFromTop(KNOB_ROW_HEIGHT * rowsFor(effectKnobs.size(), bounds.getWidth())));
    }

private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
    };

    static constexpr int KNOB_WIDTH = 84;
    static constexpr int KNOB_ROW_HEIGHT = 96;

    AndrotoneAudioProcessor& processorRef;
    int track = 0;
    std::vector<std::unique_ptr<juce::TextButton>> trackButtons;
    juce::ComboBox instrumentChooser;
    juce::Label instrumentHeading, effectsHeading, gainReduction;
    std::vector<std::unique_ptr<Knob>> instrumentKnobs, effectKnobs;

    void timerCallback() override {
        const double reduction = processorRef.getGainReductionDb(track);
        gainReduction.setText(reduction > 0.05 ? "Compressor: -" + juce::String(reduction, 1) + " dB" : juce::String(),
                              juce::dontSendNotification);
    }

    static int rowsFor(size_t count, int width) {
        const int perRow = juce::jmax(1, width / KNOB_WIDTH);
        return juce::jmax(1, static_cast<int>((count + static_cast<size_t>(perRow) - 1) / static_cast<size_t>(perRow)));
    }

    void layoutKnobs(std::vector<std::unique_ptr<Knob>>& knobs, juce::Rectangle<int> area) {
        const int perRow = juce::jmax(1, area.getWidth() / KNOB_WIDTH);
        for (size_t i = 0; i < knobs.size(); i++) {
            const int column = static_cast<int>(i) % perRow;
            const int row = static_cast<int>(i) / perRow;
            auto cell = juce::Rectangle<int>(area.getX() + column * KNOB_WIDTH, area.getY() + row * KNOB_ROW_HEIGHT,
                                             KNOB_WIDTH, KNOB_ROW_HEIGHT).reduced(4);
            knobs[i]->label.setBounds(cell.removeFromTop(16));
            knobs[i]->slider.setBounds(cell);
        }
    }

    static juce::String formatValue(const ParamSpec& spec, double value) {
        if (juce::String(spec.id) == "wave") {
            return waveName(static_cast<int>(value));
        }
        if (juce::String(spec.id) == "filterMode") {
            return filterModeName(static_cast<int>(value));
        }
        if (spec.integer) {
            return juce::String(static_cast<int>(value)) + spec.unit;
        }
        if (std::abs(value) >= 100.0) {
            return juce::String(static_cast<int>(std::round(value))) + spec.unit;
        }
        return juce::String(value, 2) + spec.unit;
    }

    void makeKnobs(std::vector<std::unique_ptr<Knob>>& knobs, const std::vector<ParamSpec>& specs, DeviceParams& params) {
        for (auto& knob : knobs) {
            removeChildComponent(&knob->slider);
            removeChildComponent(&knob->label);
        }
        knobs.clear();
        for (size_t i = 0; i < specs.size(); i++) {
            const auto spec = specs[i];
            auto knob = std::make_unique<Knob>();
            knob->label.setText(spec.name, juce::dontSendNotification);
            knob->label.setJustificationType(juce::Justification::centred);
            knob->label.setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::TEXT_MUTED));
            auto& slider = knob->slider;
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, KNOB_WIDTH - 8, 18);
            slider.setRange(spec.min, spec.max, spec.integer ? 1.0 : 0.0);
            if (spec.skewMidPoint > 0.0f) {
                slider.setSkewFactorFromMidPoint(spec.skewMidPoint);
            }
            slider.setDoubleClickReturnValue(true, spec.defaultValue);
            slider.textFromValueFunction = [spec](double v) { return formatValue(spec, v); };
            slider.setValue(params.get(static_cast<int>(i)), juce::dontSendNotification);
            slider.onValueChange = [&params, &slider, i]() { params.set(static_cast<int>(i), static_cast<float>(slider.getValue())); };
            slider.updateText(); // apply the formatter to the initial value
            addAndMakeVisible(knob->label);
            addAndMakeVisible(slider);
            knobs.push_back(std::move(knob));
        }
    }

    void buildControls() {
        const auto type = processorRef.getInstrument(track);
        instrumentHeading.setText(juce::String("Track ") + juce::String(track + 1) + "  /  " + instrumentChooser.getText(),
                                  juce::dontSendNotification);
        makeKnobs(instrumentKnobs, instrumentSpecs(type), processorRef.getInstrumentParams(track));
        makeKnobs(effectKnobs, effectSpecs(), processorRef.getEffectParams(track));
        resized();
    }
};
