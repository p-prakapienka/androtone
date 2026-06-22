#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

#include "../PluginProcessor.h"
#include "AndrotoneLookAndFeel.h"

class SessionTab : public juce::Component, private juce::Timer {
public:
    explicit SessionTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        numTracks = processorRef.getNumActiveTracks();
        numClips = processorRef.getNumActiveClips();

        buttons = std::vector<juce::TextButton>(numTracks * numClips);
        sceneButtons = std::vector<juce::TextButton>(numClips);

        for (int i = 0; i < numClips; i++) {
            for (int j = 0; j < numTracks; j++) {
                auto& button = buttons[i * numTracks + j];

                if (processorRef.isClipEmpty(j, i)) {
                    // Empty slots read as an outlined "+" with no fill, distinct from real clips.
                    const juce::Colour muted = juce::Colour(AndrotoneLookAndFeel::Palette::textMuted);
                    button.setButtonText("+");
                    button.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
                    button.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
                    button.setColour(juce::TextButton::textColourOffId, muted);
                    button.setColour(juce::TextButton::textColourOnId, muted);
                    button.setColour(juce::ComboBox::outlineColourId, muted);
                } else {
                    button.setButtonText(juce::String(i + 1));
                }

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
        if (numTracks == 0 || numClips == 0) {
            return;
        }
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
    AndrotoneAudioProcessor& processorRef;
    int numTracks = 0;
    int numClips = 0;

    std::vector<juce::TextButton> buttons;
    std::vector<juce::TextButton> sceneButtons;

    int blinkTick = 0;

    void refreshToggleStates() {
        // ~2.5 Hz blink at 15 Hz timer (toggle every 3 ticks ≈ 200 ms)
        const bool blinkOn = (blinkTick / 3) % 2 == 0;

        for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
            const int activeClip = processorRef.getCurrentClip(trackIdx);
            const int queuedClip = processorRef.getNextClip(trackIdx);
            for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
                auto& button = buttons[clipIdx * numTracks + trackIdx];
                if (processorRef.isClipEmpty(trackIdx, clipIdx)) {
                    // Empty slots never fill or blink — they stay an outlined "+".
                    button.setToggleState(false, juce::dontSendNotification);
                    continue;
                }
                bool on = (clipIdx == activeClip);
                if (clipIdx == queuedClip) {
                    on = blinkOn;
                }
                button.setToggleState(on, juce::dontSendNotification);
            }
        }

        const int currentScene = processorRef.getCurrentScene();
        const int nextScene = processorRef.getNextScene();
        for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
            bool on = (clipIdx == currentScene);
            if (clipIdx == nextScene) {
                on = blinkOn;
            }
            sceneButtons[clipIdx].setToggleState(on, juce::dontSendNotification);
        }
    }
};
