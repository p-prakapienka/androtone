#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

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

        // Tempo in hundredths of a BPM; drag vertically or scroll to change, double-click to type.
        tempoSlider.setRange(20.0, 999.0, 0.01);
        tempoSlider.setNumDecimalPlacesToDisplay(2);
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

        tapButton.setButtonText("Tap");
        tapButton.setTooltip("Tap tempo: tap once per beat");
        tapButton.onClick = [this]() {
            if (processorRef.tapTempo() > 0.0) {
                tempoSlider.setValue(processorRef.getTempo(), juce::dontSendNotification);
            }
        };
        addAndMakeVisible(tapButton);

        metronomeButton.setButtonText(juce::String::fromUTF8("\xe2\x99\xa9")); // ♩
        metronomeButton.setTooltip("Metronome");
        metronomeButton.setClickingTogglesState(true);
        metronomeButton.setToggleState(processorRef.isMetronomeOn(), juce::dontSendNotification);
        metronomeButton.onClick = [this]() { processorRef.setMetronome(metronomeButton.getToggleState()); };
        addAndMakeVisible(metronomeButton);

        position.setJustificationType(juce::Justification::centred);
        position.setFont(juce::FontOptions(16.0f));
        position.setTooltip("Song position (bars.beats.sixteenths)");
        addAndMakeVisible(position);

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
        for (auto* button : { &playButton, &loopButton, &undoButton, &redoButton, &tapButton }) {
            button->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        }

        // Launching a clip or scene also starts the transport, so keep the play glyph in sync.
        startTimerHz(10);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        const int columnWidth = bounds.getWidth() / 9;

        // Each control sits centred in its own column:
        // undo redo position tempo tap metronome quantization loop play.
        auto placeInColumn = [&bounds, columnWidth](juce::Component& control, int width, int height) {
            control.setBounds(bounds.removeFromLeft(columnWidth).withSizeKeepingCentre(width, height));
        };

        placeInColumn(undoButton, 44, 40);
        placeInColumn(redoButton, 44, 40);
        placeInColumn(position, juce::jmin(columnWidth, 90), 24);
        placeInColumn(tempoSlider, juce::jmin(columnWidth - 4, 76), 24);
        placeInColumn(tapButton, juce::jmin(columnWidth - 4, 52), 36);
        placeInColumn(metronomeButton, 44, 36);
        placeInColumn(quantization, juce::jmin(columnWidth - 4, 80), 26);
        placeInColumn(loopButton, 44, 40);
        placeInColumn(playButton, 44, 40);
    }

private:
    void timerCallback() override {
        refreshPlayGlyph();
        refreshUndo();
        position.setText(formatPosition(processorRef.getSongPosition()), juce::dontSendNotification);
        // The host may drive the tempo (plugin builds).
        if (!tempoSlider.isMouseButtonDown() && std::abs(tempoSlider.getValue() - processorRef.getTempo()) > 0.001) {
            tempoSlider.setValue(processorRef.getTempo(), juce::dontSendNotification);
        }
    }

public:
    // Song position as bars.beats.sixteenths, counted from 1.1.1 (4/4).
    static juce::String formatPosition(double beats) {
        const bool negative = beats < 0.0;
        const double magnitude = std::abs(beats);
        const auto sixteenths = static_cast<juce::int64>(std::floor(magnitude * 4.0 + 1e-6));
        const auto bar = sixteenths / 16;
        const auto beat = (sixteenths / 4) % 4;
        const auto sixteenth = sixteenths % 4;
        return (negative ? "-" : "") + juce::String(bar + 1) + "." + juce::String(beat + 1) + "." + juce::String(sixteenth + 1);
    }

private:

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
    juce::TextButton tapButton;
    juce::TextButton metronomeButton;
    juce::Label position;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ControlPanel)
};
