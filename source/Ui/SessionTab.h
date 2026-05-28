#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

#include "../PluginProcessor.h"

class SessionTab : public juce::Component, private juce::Timer {
public:
    explicit SessionTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
            for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
                auto& button = buttons[clipIdx * numTracks + trackIdx];
                button.setButtonText(juce::String(clipIdx + 1));
                button.onClick = [this, trackIdx, clipIdx]() {
                    processorRef.setCurrentClip(trackIdx, clipIdx);
                };
                addAndMakeVisible(button);
            }
        }

        refreshToggleStates();
        startTimerHz(15);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        const int cellW = bounds.getWidth() / numTracks;
        const int cellH = bounds.getHeight() / numClips;
        const int side = juce::jmax(0, juce::jmin(cellW, cellH) - 10);

        for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
            for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
                const int cellX = bounds.getX() + trackIdx * cellW;
                const int cellY = bounds.getY() + clipIdx * cellH;
                const int x = cellX + (cellW - side) / 2;
                const int y = cellY + (cellH - side) / 2;
                buttons[clipIdx * numTracks + trackIdx].setBounds(x, y, side, side);
            }
        }
    }

private:
    static constexpr int numTracks = AndrotoneAudioProcessor::getNumTracks();
    static constexpr int numClips = 2;

    void timerCallback() override {
        blinkTick++;
        refreshToggleStates();
    }

    void refreshToggleStates() {
        // ~2.5 Hz blink at 15 Hz timer (toggle every 3 ticks ≈ 200 ms)
        const bool blinkOn = (blinkTick / 3) % 2 == 0;

        for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
            const int activeClip = processorRef.getCurrentClip(trackIdx);
            const int queuedClip = processorRef.getNextClip(trackIdx);
            for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
                bool on = (clipIdx == activeClip);
                if (clipIdx == queuedClip) {
                    on = blinkOn;
                }
                buttons[clipIdx * numTracks + trackIdx]
                    .setToggleState(on, juce::dontSendNotification);
            }
        }
    }

    AndrotoneAudioProcessor& processorRef;
    std::array<juce::TextButton, numTracks * numClips> buttons;
    int blinkTick = 0;
};
