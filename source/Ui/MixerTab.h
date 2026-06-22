#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

#include "../PluginProcessor.h"

class MixerTab : public juce::Component {
public:
    explicit MixerTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        const int numTracks = processorRef.getNumActiveTracks();

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

            auto reverbSlider = std::make_unique<juce::Slider>();
            reverbSlider->setRange(0.0, 1.0);
            reverbSlider->setValue(processorRef.getTrackReverbSend(t));
            reverbSlider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            reverbSlider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
            reverbSlider->onValueChange = [this, t]() {
                processorRef.setTrackReverbSend(t, (float) reverbSliders[t]->getValue());
            };
            addAndMakeVisible(*reverbSlider);
            reverbSliders.push_back(std::move(reverbSlider));

            auto reverbLabel = std::make_unique<juce::Label>();
            reverbLabel->setText("Rev", juce::dontSendNotification);
            reverbLabel->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*reverbLabel);
            reverbLabels.push_back(std::move(reverbLabel));

            auto delaySlider = std::make_unique<juce::Slider>();
            delaySlider->setRange(0.0, 1.0);
            delaySlider->setValue(processorRef.getTrackDelaySend(t));
            delaySlider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            delaySlider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
            delaySlider->onValueChange = [this, t]() {
                processorRef.setTrackDelaySend(t, (float) delaySliders[t]->getValue());
            };
            addAndMakeVisible(*delaySlider);
            delaySliders.push_back(std::move(delaySlider));

            auto delayLabel = std::make_unique<juce::Label>();
            delayLabel->setText("Dly", juce::dontSendNotification);
            delayLabel->setJustificationType(juce::Justification::centred);
            addAndMakeVisible(*delayLabel);
            delayLabels.push_back(std::move(delayLabel));
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

            auto delayArea = col.removeFromTop(90);
            auto delayLabelArea = delayArea.removeFromTop(20);
            delayLabels[t]->setBounds(delayLabelArea);
            delaySliders[t]->setBounds(delayArea);

            auto reverbArea = col.removeFromTop(90);
            auto reverbLabelArea = reverbArea.removeFromTop(20);
            reverbLabels[t]->setBounds(reverbLabelArea);
            reverbSliders[t]->setBounds(reverbArea);

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
    std::vector<std::unique_ptr<juce::Slider>> reverbSliders;
    std::vector<std::unique_ptr<juce::Label>> reverbLabels;
    std::vector<std::unique_ptr<juce::Slider>> delaySliders;
    std::vector<std::unique_ptr<juce::Label>> delayLabels;
    juce::Slider masterSlider;
    juce::Label masterLabel;
};
