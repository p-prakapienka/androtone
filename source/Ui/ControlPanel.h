#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"
#include "AndrotoneLookAndFeel.h"

// Transport controls shown at the bottom of the editor, outside (and shared across) the tabs.
class ControlPanel : public juce::Component, private juce::Timer {
public:
    explicit ControlPanel(AndrotoneAudioProcessor& p) : processorRef(p) {
        playButton.setButtonText(playGlyph());
        playButton.onClick = [this]() {
            processorRef.setPlaying(!processorRef.isPlaying());
            playButton.setButtonText(processorRef.isPlaying() ? stopGlyph() : playGlyph());
        };
        addAndMakeVisible(playButton);

        // Loop on (default) means song mode off: each track just loops its current clip.
        loopButton.setButtonText(juce::String::fromUTF8("\xe2\x88\x9e"));  // ∞
        loopButton.setClickingTogglesState(true);
        loopButton.setToggleState(!processorRef.isSongMode(), juce::dontSendNotification);
        // Active loop is shown by tinting the text accent, not by filling the background.
        loopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(AndrotoneLookAndFeel::Palette::surface));
        loopButton.setColour(juce::TextButton::textColourOnId, juce::Colour(AndrotoneLookAndFeel::Palette::accent));
        loopButton.onClick = [this]() {
            processorRef.setSongMode(!loopButton.getToggleState());
        };
        addAndMakeVisible(loopButton);

        tempoSlider.setRange(60.0, 200.0, 1.0);
        tempoSlider.setValue(processorRef.getTempo());
        // Render as a plain number box (no track fill / outline); drag vertically or scroll to change.
        tempoSlider.setSliderStyle(juce::Slider::LinearBarVertical);
        tempoSlider.setSliderSnapsToMousePosition(false);
        tempoSlider.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
        tempoSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        tempoSlider.onValueChange = [this]() {
            processorRef.setTempo(tempoSlider.getValue());
        };
        addAndMakeVisible(tempoSlider);

        undoButton.setButtonText(juce::String::fromUTF8("\xe2\x86\xba"));  // ↺
        undoButton.setEnabled(false);
        addAndMakeVisible(undoButton);

        redoButton.setButtonText(juce::String::fromUTF8("\xe2\x86\xbb"));  // ↻
        redoButton.setEnabled(false);
        addAndMakeVisible(redoButton);

        // Flat transport controls: no button outlines.
        for (auto* button : { &playButton, &loopButton, &undoButton, &redoButton }) {
            button->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        }

        startTimerHz(15);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        const int columnWidth = bounds.getWidth() / 5;

        // Each control sits centred in its own fifth of the panel: undo redo tempo loop play.
        auto placeInColumn = [&bounds, columnWidth](juce::Component& control, int width, int height) {
            control.setBounds(bounds.removeFromLeft(columnWidth).withSizeKeepingCentre(width, height));
        };

        placeInColumn(undoButton, 44, 40);
        placeInColumn(redoButton, 44, 40);
        placeInColumn(tempoSlider, 70, 24);
        placeInColumn(loopButton, 44, 40);
        placeInColumn(playButton, 44, 40);
    }

private:
    void timerCallback() override {
        playButton.setButtonText(processorRef.isPlaying() ? stopGlyph() : playGlyph());
        if (!tempoSlider.isMouseButtonDown()) {
            tempoSlider.setValue(processorRef.getTempo(), juce::dontSendNotification);
        }
    }

    static juce::String playGlyph() { return juce::String::fromUTF8("\xe2\x96\xb6"); }  // ▶
    static juce::String stopGlyph() { return juce::String::fromUTF8("\xe2\x96\xa0"); }  // ■

    AndrotoneAudioProcessor& processorRef;
    juce::TextButton playButton;
    juce::TextButton loopButton;
    juce::TextButton undoButton;
    juce::TextButton redoButton;
    juce::Slider tempoSlider;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanel)
};
