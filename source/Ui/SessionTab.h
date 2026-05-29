#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

#include "../PluginProcessor.h"

class SessionTab : public juce::Component, private juce::Timer {
public:
    explicit SessionTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        for (int i = 0; i < numClips; i++) {
            for (int j = 0; j < numTracks; j++) {
                auto& button = buttons[i * numTracks + j];
                button.setButtonText(juce::String(i + 1));
                button.onClick = [this, j, i]() {
                    processorRef.setCurrentClip(j, i);
                };
                addAndMakeVisible(button);
            }
        }

        for (int i = 0; i < numClips; i++) {
            auto& sceneButton = sceneButtons[i];
            sceneButton.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6")));
            sceneButton.onClick = [this, i]() {
                for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
                    processorRef.setCurrentScene(i);
                }
            };
            addAndMakeVisible(sceneButton);
        }

        refreshToggleStates();
        startTimerHz(15);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        const int sceneCellW = bounds.getWidth() / (2 * numTracks + 1);
        const int trackCellW = sceneCellW * 2;
        const int cellH = bounds.getHeight() / numClips;
        const int rowHeight = juce::jmax(0, juce::jmin(trackCellW, cellH) - 10);
        const int sceneSide = juce::jmax(0, juce::jmin(sceneCellW, cellH) - 10);

        for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
            const int cellY = bounds.getY() + clipIdx * cellH;

            const int sceneX = bounds.getX() + (sceneCellW - sceneSide) / 2;
            const int sceneY = cellY + (cellH - rowHeight) / 2;
            sceneButtons[clipIdx].setBounds(sceneX, sceneY, sceneSide, rowHeight);

            for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
                const int cellX = bounds.getX() + sceneCellW + trackIdx * trackCellW;
                const int x = cellX + (trackCellW - rowHeight) / 2;
                const int y = cellY + (cellH - rowHeight) / 2;
                buttons[clipIdx * numTracks + trackIdx].setBounds(x, y, rowHeight, rowHeight);
            }
        }
    }

    void timerCallback() override {
        blinkTick++;
        refreshToggleStates();
    }

private:
    static constexpr int numTracks = AndrotoneAudioProcessor::getNumTracks();
    static constexpr int numClips = 2;

    AndrotoneAudioProcessor& processorRef;

    std::array<juce::TextButton, numTracks * numClips> buttons;
    std::array<juce::TextButton, numClips> sceneButtons;

    int blinkTick = 0;

    void refreshToggleStates() {
        // ~2.5 Hz blink at 15 Hz timer (toggle every 3 ticks ≈ 200 ms)
        const bool blinkOn = (blinkTick / 3) % 2 == 0;

        const int currentScene = processorRef.getCurrentScene();

        for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
            const int activeClip = processorRef.getCurrentClip(trackIdx);
            const int queuedClip = processorRef.getNextClip(trackIdx);
            for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
                bool on = (clipIdx == activeClip);
                if (clipIdx == queuedClip) {
                    on = blinkOn;
                }
                buttons[clipIdx * numTracks + trackIdx].setToggleState(on, juce::dontSendNotification);
                sceneButtons[clipIdx].setToggleState(clipIdx == currentScene, juce::dontSendNotification);
            }
        }
    }
};
