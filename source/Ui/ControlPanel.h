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
            refreshPlayGlyph();
        };
        addAndMakeVisible(playButton);

        // Loop on (default) means song mode off: each track just loops its current clip.
        loopButton.setButtonText(juce::String::fromUTF8("\xe2\x88\x9e"));  // ∞
        loopButton.setClickingTogglesState(true);
        loopButton.setToggleState(!processorRef.isSongMode(), juce::dontSendNotification);
        // Active loop is shown by tinting the text accent, not by filling the background.
        loopButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(AndrotoneLookAndFeel::Palette::SURFACE));
        loopButton.setColour(juce::TextButton::textColourOnId, juce::Colour(AndrotoneLookAndFeel::Palette::ACCENT));
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

        // Global launch quantization: queued clip / scene launches wait for the next boundary of this size.
        quantization.addItemList({ "None", "1/16", "1/8", "1/4", "1/2", "1 Bar", "2 Bars", "4 Bars", "8 Bars" }, 1);
        quantization.setSelectedItemIndex(processorRef.getQuantizationIndex(), juce::dontSendNotification);
        quantization.setTooltip("Launch quantization");
        quantization.onChange = [this]() {
            processorRef.setQuantizationIndex(quantization.getSelectedItemIndex());
        };
        addAndMakeVisible(quantization);

        undoButton.setButtonText(juce::String::fromUTF8("\xe2\x86\xba"));  // ↺
        undoButton.onClick = [this]() { processorRef.getUndoManager().undo(); refreshUndo(); };
        addAndMakeVisible(undoButton);

        redoButton.setButtonText(juce::String::fromUTF8("\xe2\x86\xbb"));  // ↻
        redoButton.onClick = [this]() { processorRef.getUndoManager().redo(); refreshUndo(); };
        addAndMakeVisible(redoButton);
        refreshUndo();

        // Flat transport controls: no button outlines.
        for (auto* button : { &playButton, &loopButton, &undoButton, &redoButton }) {
            button->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        }

        // Launching a clip or scene also starts the transport, so keep the play glyph in sync.
        startTimerHz(10);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        const int columnWidth = bounds.getWidth() / 6;

        // Each control sits centred in its own sixth of the panel: undo redo tempo quantization loop play.
        auto placeInColumn = [&bounds, columnWidth](juce::Component& control, int width, int height) {
            control.setBounds(bounds.removeFromLeft(columnWidth).withSizeKeepingCentre(width, height));
        };

        placeInColumn(undoButton, 44, 40);
        placeInColumn(redoButton, 44, 40);
        placeInColumn(tempoSlider, 70, 24);
        placeInColumn(quantization, juce::jmin(columnWidth - 4, 80), 26);
        placeInColumn(loopButton, 44, 40);
        placeInColumn(playButton, 44, 40);
    }

private:
    void timerCallback() override {
        refreshPlayGlyph();
        refreshUndo();
    }

    void refreshUndo() {
        auto& undo = processorRef.getUndoManager();
        undoButton.setEnabled(undo.canUndo());
        redoButton.setEnabled(undo.canRedo());
        undoButton.setTooltip(undo.canUndo() ? "Undo " + undo.getUndoDescription() : "Undo");
        redoButton.setTooltip(undo.canRedo() ? "Redo " + undo.getRedoDescription() : "Redo");
    }

    void refreshPlayGlyph() {
        const auto glyph = processorRef.isPlaying() ? stopGlyph() : playGlyph();
        if (playButton.getButtonText() != glyph) {
            playButton.setButtonText(glyph);
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
    juce::ComboBox quantization;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanel)
};
