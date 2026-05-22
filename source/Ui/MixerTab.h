#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

#include "../PluginProcessor.h"

class MixerTab : public juce::Component {
public:
    explicit MixerTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        const int numTracks = AndrotoneAudioProcessor::getNumTracks();

        for (int t = 0; t < numTracks; t++) {
            auto slider = std::make_unique<juce::Slider>();
            slider->setRange(0.0, 1.0);
            slider->setValue(processorRef.getTrackVolume(t));
            slider->setSliderStyle(juce::Slider::LinearVertical);
            slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
            slider->onValueChange = [this, t]() {
                processorRef.setTrackVolume(t, (float) trackSliders[t]->getValue());
            };
            addAndMakeVisible(*slider);
            trackSliders.push_back(std::move(slider));

            auto label = std::make_unique<juce::Label>();
            label->setText("Track " + juce::String(t + 1), juce::dontSendNotification);
            label->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*label);
            trackLabels.push_back(std::move(label));
        }

        masterSlider.setRange(0.0, 1.0);
        masterSlider.setValue(processorRef.getVolume());
        masterSlider.setSliderStyle(juce::Slider::LinearVertical);
        masterSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
        masterSlider.onValueChange = [this]() {
            processorRef.setVolume((float) masterSlider.getValue());
        };
        addAndMakeVisible(masterSlider);

        masterLabel.setText("Master", juce::dontSendNotification);
        masterLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(masterLabel);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        const int totalColumns = static_cast<int>(trackSliders.size()) + 1;
        if (totalColumns == 0) {
            return;
        }
        const int columnWidth = bounds.getWidth() / totalColumns;

        for (size_t t = 0; t < trackSliders.size(); t++) {
            auto col = bounds.removeFromLeft(columnWidth);
            auto labelArea = col.removeFromTop(20);
            trackLabels[t]->setBounds(labelArea);
            trackSliders[t]->setBounds(col);
        }

        auto col = bounds.removeFromLeft(columnWidth);
        auto labelArea = col.removeFromTop(20);
        masterLabel.setBounds(labelArea);
        masterSlider.setBounds(col);
    }

private:
    AndrotoneAudioProcessor& processorRef;
    std::vector<std::unique_ptr<juce::Slider>> trackSliders;
    std::vector<std::unique_ptr<juce::Label>> trackLabels;
    juce::Slider masterSlider;
    juce::Label masterLabel;
};
