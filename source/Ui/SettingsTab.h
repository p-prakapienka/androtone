#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"
#include "AndrotoneLookAndFeel.h"

// Global settings: groove (swing) and metronome level.
class SettingsTab : public juce::Component {
public:
    explicit SettingsTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        setUpSlider(swingTiming, swingTimingLabel, "Swing timing",
                    "Delays off-beat 16ths; 100 % puts them on the triplet position",
                    processorRef.getSwingTiming() * 100.0, [this]() { applySwing(); });
        setUpSlider(swingVelocity, swingVelocityLabel, "Swing velocity",
                    "Softens off-beat 16ths (up to -30 % at 100 %)",
                    processorRef.getSwingVelocity() * 100.0, [this]() { applySwing(); });
        setUpSlider(metronomeVolume, metronomeLabel, "Metronome volume", "Level of the click",
                    processorRef.getMetronomeVolume() * 100.0, [this]() {
                        processorRef.setMetronomeVolume(static_cast<float>(metronomeVolume.getValue() / 100.0));
                    });

        for (auto [toggle, text, on] : { std::make_tuple(&exclusiveSolo, "Exclusive solo", processorRef.isExclusiveSolo()),
                                         std::make_tuple(&exclusiveArm, "Exclusive arm", processorRef.isExclusiveArm()) }) {
            toggle->setButtonText(text);
            toggle->setToggleState(on, juce::dontSendNotification);
            addAndMakeVisible(*toggle);
        }
        exclusiveSolo.onClick = [this]() { processorRef.setExclusiveSolo(exclusiveSolo.getToggleState()); };
        exclusiveArm.onClick = [this]() { processorRef.setExclusiveArm(exclusiveArm.getToggleState()); };

        countInLabel.setText("Count-in", juce::dontSendNotification);
        recordQuantizeLabel.setText("Record quantization", juce::dontSendNotification);
        for (auto* label : { &countInLabel, &recordQuantizeLabel }) {
            label->setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::TEXT_MUTED));
            addAndMakeVisible(*label);
        }
        countIn.addItemList({ "None", "1 Bar", "2 Bars", "4 Bars" }, 1);
        const int bars = processorRef.getCountInBars();
        countIn.setSelectedItemIndex(bars == 0 ? 0 : (bars == 1 ? 1 : (bars == 2 ? 2 : 3)), juce::dontSendNotification);
        countIn.onChange = [this]() {
            static const int choices[] { 0, 1, 2, 4 };
            processorRef.setCountInBars(choices[countIn.getSelectedItemIndex()]);
        };
        addAndMakeVisible(countIn);
        recordQuantize.addItemList({ "None", "1/4", "1/8", "1/16", "1/32" }, 1);
        recordQuantize.setSelectedItemIndex(0, juce::dontSendNotification);
        recordQuantize.onChange = [this]() {
            static const double choices[] { 0.0, 1.0, 0.5, 0.25, 0.125 };
            processorRef.setRecordQuantization(choices[recordQuantize.getSelectedItemIndex()]);
        };
        addAndMakeVisible(recordQuantize);

        heading.setText("Groove", juce::dontSendNotification);
        heading.setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::ACCENT));
        addAndMakeVisible(heading);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(20);
        heading.setBounds(bounds.removeFromTop(28));
        for (auto* pair : { &rows[0], &rows[1], &rows[2] }) {
            auto row = bounds.removeFromTop(40);
            pair->first->setBounds(row.removeFromLeft(160));
            pair->second->setBounds(row.removeFromLeft(juce::jmin(360, row.getWidth())).reduced(0, 6));
            if (pair == &rows[1]) {
                bounds.removeFromTop(16); // gap before the metronome row
            }
        }
        bounds.removeFromTop(16);
        auto countRow = bounds.removeFromTop(36);
        countInLabel.setBounds(countRow.removeFromLeft(160));
        countIn.setBounds(countRow.removeFromLeft(140).reduced(0, 4));
        auto quantRow = bounds.removeFromTop(36);
        recordQuantizeLabel.setBounds(quantRow.removeFromLeft(160));
        recordQuantize.setBounds(quantRow.removeFromLeft(140).reduced(0, 4));
        bounds.removeFromTop(16);
        exclusiveSolo.setBounds(bounds.removeFromTop(32).removeFromLeft(240));
        exclusiveArm.setBounds(bounds.removeFromTop(32).removeFromLeft(240));
    }

private:
    AndrotoneAudioProcessor& processorRef;
    juce::Label heading;
    juce::ToggleButton exclusiveSolo, exclusiveArm;
    juce::ComboBox countIn, recordQuantize;
    juce::Label countInLabel, recordQuantizeLabel;
    juce::Slider swingTiming, swingVelocity, metronomeVolume;
    juce::Label swingTimingLabel, swingVelocityLabel, metronomeLabel;
    std::array<std::pair<juce::Label*, juce::Slider*>, 3> rows {
        std::make_pair(&swingTimingLabel, &swingTiming),
        std::make_pair(&swingVelocityLabel, &swingVelocity),
        std::make_pair(&metronomeLabel, &metronomeVolume),
    };

    void setUpSlider(juce::Slider& slider, juce::Label& label, const juce::String& name, const juce::String& tip,
                     double value, std::function<void()> onChange) {
        label.setText(name, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::TEXT_MUTED));
        addAndMakeVisible(label);
        slider.setSliderStyle(juce::Slider::LinearBar);
        slider.setRange(0.0, 100.0, 1.0);
        slider.setTextValueSuffix(" %");
        slider.setTooltip(tip);
        slider.setValue(value, juce::dontSendNotification);
        slider.onValueChange = std::move(onChange);
        addAndMakeVisible(slider);
    }

    void applySwing() {
        processorRef.setSwing(swingTiming.getValue() / 100.0, swingVelocity.getValue() / 100.0);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsTab)
};
