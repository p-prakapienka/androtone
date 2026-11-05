#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

#include "../PluginProcessor.h"
#include "AndrotoneLookAndFeel.h"
#include "ClipPropertiesPanel.h"

// A clip slot that reports both press and release, which the Gate and Repeat launch modes need.
// A right-click (desktop) asks for the clip's properties instead of launching.
class ClipSlotButton : public juce::TextButton {
public:
    std::function<void()> onPress;
    std::function<void()> onRelease;
    std::function<void()> onEdit;

    void mouseDown(const juce::MouseEvent& e) override {
        if (e.mods.isPopupMenu()) {
            editing = true;
            if (onEdit) {
                onEdit();
            }
            return;
        }
        editing = false;
        TextButton::mouseDown(e);
        if (onPress) {
            onPress();
        }
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (editing) {
            editing = false;
            return;
        }
        TextButton::mouseUp(e);
        if (onRelease) {
            onRelease();
        }
    }

private:
    bool editing = false;
};

// The clip launcher grid. Columns are tracks, rows are scenes (the ▶ column launches a whole row),
// and the bottom row holds per-track stop buttons plus stop-all.
//
// Clips launch on the global launch-quantization grid chosen in the control panel; queued launches
// and stops blink until they take effect. With the ✎ toggle on, tapping a clip opens its launch /
// follow-action settings instead (right-click does the same on desktop).
class SessionTab : public juce::Component, private juce::Timer {
public:
    explicit SessionTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        numTracks = processorRef.getNumActiveTracks();
        numClips = processorRef.getNumActiveClips();

        slots = std::vector<ClipSlotButton>(static_cast<size_t>(numTracks * numClips));
        sceneButtons = std::vector<juce::TextButton>(static_cast<size_t>(numClips));
        stopButtons = std::vector<juce::TextButton>(static_cast<size_t>(numTracks));
        trackLabels = std::vector<juce::Label>(static_cast<size_t>(numTracks));

        const juce::Colour muted { AndrotoneLookAndFeel::Palette::TEXT_MUTED };

        editToggle.setButtonText(juce::String::fromUTF8("\xe2\x9c\x8e")); // ✎
        editToggle.setClickingTogglesState(true);
        editToggle.setTooltip("Edit clip launch & follow settings");
        addAndMakeVisible(editToggle);

        for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
            auto& label = trackLabels[static_cast<size_t>(trackIdx)];
            label.setText(juce::String(trackIdx + 1), juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setColour(juce::Label::textColourId, muted);
            addAndMakeVisible(label);
        }

        for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
            for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
                auto& slot = slotAt(trackIdx, clipIdx);

                if (processorRef.isClipEmpty(trackIdx, clipIdx)) {
                    // Empty slots read as an outlined "+" with no fill, distinct from real clips.
                    // Launching one stops its track.
                    slot.setButtonText("+");
                    slot.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
                    slot.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
                    slot.setColour(juce::TextButton::textColourOffId, muted);
                    slot.setColour(juce::TextButton::textColourOnId, muted);
                    slot.setColour(juce::ComboBox::outlineColourId, muted);
                } else {
                    slot.onEdit = [this, trackIdx, clipIdx]() { showClipProperties(trackIdx, clipIdx); };
                }

                slot.onPress = [this, trackIdx, clipIdx]() {
                    if (editToggle.getToggleState()) {
                        if (!processorRef.isClipEmpty(trackIdx, clipIdx)) {
                            showClipProperties(trackIdx, clipIdx);
                        }
                        return;
                    }
                    processorRef.pressClip(trackIdx, clipIdx);
                };
                slot.onRelease = [this, trackIdx, clipIdx]() {
                    // Always forward the release, even in edit mode, so a held Gate / Repeat clip
                    // can't get stuck if edit mode was switched on mid-press.
                    processorRef.releaseClip(trackIdx, clipIdx);
                };
                addAndMakeVisible(slot);
            }
        }

        for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
            auto& sceneButton = sceneButtons[static_cast<size_t>(clipIdx)];
            sceneButton.setButtonText(juce::String::fromUTF8("\xe2\x96\xb6")); // ▶
            sceneButton.onClick = [this, clipIdx]() { processorRef.launchScene(clipIdx); };
            addAndMakeVisible(sceneButton);
        }

        for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
            auto& stopButton = stopButtons[static_cast<size_t>(trackIdx)];
            stopButton.setButtonText(juce::String::fromUTF8("\xe2\x96\xa0")); // ■
            stopButton.onClick = [this, trackIdx]() { processorRef.stopTrack(trackIdx); };
            addAndMakeVisible(stopButton);
        }

        stopAllButton.setButtonText(juce::String::fromUTF8("\xe2\x96\xa0")); // ■
        stopAllButton.setTooltip("Stop all clips");
        stopAllButton.onClick = [this]() { processorRef.stopAllClips(); };
        addAndMakeVisible(stopAllButton);

        refreshStates();
        startTimerHz(15);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);
        if (numTracks == 0 || numClips == 0) {
            return;
        }

        // Header row (✎ + track numbers), one row per scene, then the stop row.
        const int numRows = numClips + 2;
        const int sceneCellW = bounds.getWidth() / (2 * numTracks + 1);
        const int trackCellW = sceneCellW * 2;
        const int cellH = bounds.getHeight() / numRows;
        const int side = juce::jmax(0, juce::jmin(trackCellW, cellH) - 10);
        const int sceneSide = juce::jmax(0, juce::jmin(sceneCellW, cellH) - 10);

        auto sceneCell = [&](int row) {
            return juce::Rectangle<int>(bounds.getX(), bounds.getY() + row * cellH, sceneCellW, cellH);
        };
        auto trackCell = [&](int row, int trackIdx) {
            return juce::Rectangle<int>(bounds.getX() + sceneCellW + trackIdx * trackCellW,
                                        bounds.getY() + row * cellH, trackCellW, cellH);
        };

        editToggle.setBounds(sceneCell(0).withSizeKeepingCentre(sceneSide, side));
        for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
            trackLabels[static_cast<size_t>(trackIdx)].setBounds(trackCell(0, trackIdx));
        }

        for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
            const int row = clipIdx + 1;
            sceneButtons[static_cast<size_t>(clipIdx)].setBounds(sceneCell(row).withSizeKeepingCentre(sceneSide, side));
            for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
                slotAt(trackIdx, clipIdx).setBounds(trackCell(row, trackIdx).withSizeKeepingCentre(side, side));
            }
        }

        const int stopRow = numClips + 1;
        const int stopSide = side * 2 / 3;
        stopAllButton.setBounds(sceneCell(stopRow).withSizeKeepingCentre(sceneSide, stopSide));
        for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
            stopButtons[static_cast<size_t>(trackIdx)].setBounds(
                trackCell(stopRow, trackIdx).withSizeKeepingCentre(side, stopSide));
        }
    }

    void timerCallback() override {
        blinkTick++;
        refreshStates();
    }

private:
    AndrotoneAudioProcessor& processorRef;
    int numTracks = 0;
    int numClips = 0;

    std::vector<ClipSlotButton> slots;
    std::vector<juce::TextButton> sceneButtons;
    std::vector<juce::TextButton> stopButtons;
    std::vector<juce::Label> trackLabels;
    juce::TextButton editToggle;
    juce::TextButton stopAllButton;

    int blinkTick = 0;

    ClipSlotButton& slotAt(int trackIdx, int clipIdx) {
        return slots[static_cast<size_t>(clipIdx * numTracks + trackIdx)];
    }

    void showClipProperties(int trackIdx, int clipIdx) {
        auto& slot = slotAt(trackIdx, clipIdx);
        juce::CallOutBox::launchAsynchronously(
            std::make_unique<ClipPropertiesPanel>(processorRef, trackIdx, clipIdx),
            slot.getScreenBounds(),
            nullptr
        );
    }

    // Clip label: its number, plus a marker for a non-default launch mode and for follow actions.
    juce::String slotText(int trackIdx, int clipIdx) {
        auto& settings = processorRef.getClipSettings(trackIdx, clipIdx);
        juce::String text(clipIdx + 1);

        switch (settings.launchMode.load()) {
            case LaunchMode::Gate: text << " G"; break;
            case LaunchMode::Toggle: text << " T"; break;
            case LaunchMode::Repeat: text << " R"; break;
            case LaunchMode::Trigger: break;
        }
        if (settings.followEnabled.load()) {
            text << juce::String::fromUTF8(" \xe2\x86\xb7"); // ↷
        }
        return text;
    }

    void refreshStates() {
        // ~2.5 Hz blink at 15 Hz timer (toggle every 3 ticks ≈ 200 ms)
        const bool blinkOn = (blinkTick / 3) % 2 == 0;

        for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
            const int playingClip = processorRef.getPlayingClip(trackIdx);
            const int queuedClip = processorRef.getQueuedClip(trackIdx);
            const bool stopQueued = processorRef.isTrackStopQueued(trackIdx);

            for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
                auto& slot = slotAt(trackIdx, clipIdx);
                if (processorRef.isClipEmpty(trackIdx, clipIdx)) {
                    // Empty slots never fill or blink — they stay an outlined "+".
                    slot.setToggleState(false, juce::dontSendNotification);
                    continue;
                }

                bool on = clipIdx == playingClip && !stopQueued;
                if (clipIdx == queuedClip) {
                    on = blinkOn;
                } else if (clipIdx == playingClip && stopQueued) {
                    on = blinkOn;
                }
                slot.setToggleState(on, juce::dontSendNotification);

                const auto text = slotText(trackIdx, clipIdx);
                if (slot.getButtonText() != text) {
                    slot.setButtonText(text);
                }
            }

            // A track's stop button lights while it has a clip playing, and blinks while a stop is queued.
            auto& stopButton = stopButtons[static_cast<size_t>(trackIdx)];
            stopButton.setToggleState(stopQueued ? blinkOn : false, juce::dontSendNotification);
            stopButton.setEnabled(playingClip >= 0);
        }

        const int currentScene = processorRef.getCurrentScene();
        const int queuedScene = processorRef.getQueuedScene();
        for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
            bool on = clipIdx == currentScene;
            if (clipIdx == queuedScene) {
                on = blinkOn;
            }
            sceneButtons[static_cast<size_t>(clipIdx)].setToggleState(on, juce::dontSendNotification);
        }

        stopAllButton.setToggleState(processorRef.isStopAllQueued() && blinkOn, juce::dontSendNotification);
    }
};
