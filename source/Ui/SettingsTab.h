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
    }

private:
    AndrotoneAudioProcessor& processorRef;
    juce::Label heading;
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
