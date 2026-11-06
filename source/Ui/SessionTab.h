#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

#include "../PluginProcessor.h"
#include "CliptoneLookAndFeel.h"
#include "ClipPropertiesPanel.h"

// A clip slot that reports both press and release, which the Gate and Repeat launch modes need.
// A right-click (desktop) asks for the clip's properties instead of launching.
class ClipSlotButton : public juce::TextButton {
public:
    std::function<void()> onPress;
    std::function<void()> onRelease;
    std::function<void()> onEdit;
    std::function<void()> onDoubleTap;

    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (onDoubleTap) {
            onDoubleTap();
        }
    }

    // Visual state cache so refreshes only restyle on change.
    int styledAs = -1;

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
// and stops blink until they take effect. Double-tap an empty slot to create a one-bar clip. With
// the ✎ toggle on, tapping a clip opens it in the Clip tab (tapping an empty slot creates one);
// right-click opens its launch / follow-action settings.
class SessionTab : public juce::Component, private juce::Timer {
public:
    // Asks the editor to show a clip in the Clip tab.
    std::function<void(int, int)> onOpenClip;

    explicit SessionTab(CliptoneAudioProcessor& p) : processorRef(p) {
        numTracks = processorRef.getNumActiveTracks();
        numClips = processorRef.getNumActiveClips();

        slots = std::vector<ClipSlotButton>(static_cast<size_t>(numTracks * numClips));
        sceneButtons = std::vector<juce::TextButton>(static_cast<size_t>(numClips));
        stopButtons = std::vector<juce::TextButton>(static_cast<size_t>(numTracks));
        trackLabels = std::vector<juce::TextButton>(static_cast<size_t>(numTracks));

        const juce::Colour muted { CliptoneLookAndFeel::Palette::TEXT_MUTED };

        editToggle.setButtonText(juce::String::fromUTF8("\xe2\x9c\x8e")); // ✎
        editToggle.setClickingTogglesState(true);
        editToggle.setTooltip("Edit mode: tap a clip to open it, tap an empty slot to create one");
        addAndMakeVisible(editToggle);

        for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
            // Track header: tap to arm the track for recording (red when armed).
            auto& label = trackLabels[static_cast<size_t>(trackIdx)];
            label.setButtonText(juce::String(trackIdx + 1));
            label.setTooltip("Arm track " + juce::String(trackIdx + 1) + " for recording");
            label.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            label.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFE57373));
            label.setColour(juce::TextButton::textColourOffId, muted);
            label.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            label.onClick = [this, trackIdx]() {
                const auto mods = juce::ModifierKeys::getCurrentModifiers();
                processorRef.setTrackArmed(trackIdx, !processorRef.isTrackArmed(trackIdx), mods.isShiftDown() || mods.isCommandDown());
            };
            addAndMakeVisible(label);
        }

        for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
            for (int trackIdx = 0; trackIdx < numTracks; trackIdx++) {
                auto& slot = slotAt(trackIdx, clipIdx);

                slot.onEdit = [this, trackIdx, clipIdx]() {
                    if (processorRef.hasClip(trackIdx, clipIdx)) {
                        showClipProperties(trackIdx, clipIdx);
                    }
                };
                slot.onDoubleTap = [this, trackIdx, clipIdx]() {
                    if (!editToggle.getToggleState() && !processorRef.hasClip(trackIdx, clipIdx)) {
                        createClip(trackIdx, clipIdx);
                    }
                };
                slot.onPress = [this, trackIdx, clipIdx]() {
                    if (editToggle.getToggleState()) {
                        if (!processorRef.hasClip(trackIdx, clipIdx)) {
                            createClip(trackIdx, clipIdx);
                        } else if (onOpenClip) {
                            onOpenClip(trackIdx, clipIdx);
                        }
                        return;
                    }
                    if (processorRef.getRecordingSlot(trackIdx) == clipIdx) {
                        processorRef.stopRecording(trackIdx); // pressing the recording clip plays it
                        return;
                    }
                    if (processorRef.isTrackArmed(trackIdx) && !processorRef.hasClip(trackIdx, clipIdx)) {
                        processorRef.recordIntoSlot(trackIdx, clipIdx); // clip record button
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
            trackLabels[static_cast<size_t>(trackIdx)].setBounds(trackCell(0, trackIdx).withSizeKeepingCentre(side, side * 2 / 3));
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
    CliptoneAudioProcessor& processorRef;
    int numTracks = 0;
    int numClips = 0;

    std::vector<ClipSlotButton> slots;
    std::vector<juce::TextButton> sceneButtons;
    std::vector<juce::TextButton> stopButtons;
    std::vector<juce::TextButton> trackLabels;
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

    void createClip(int trackIdx, int clipIdx) {
        processorRef.getClipSettings(trackIdx, clipIdx).reset();
        processorRef.performClipEdit(trackIdx, clipIdx, nullptr, ClipData::createEmpty(), "Create clip");
        if (onOpenClip) {
            onOpenClip(trackIdx, clipIdx);
        }
    }

    // Empty slots: outlined "+". Clips: filled with their colour (dim when idle, full when playing).
    void styleSlot(ClipSlotButton& slot, int trackIdx, int clipIdx, bool armed, bool recording) {
        const bool has = processorRef.hasClip(trackIdx, clipIdx);
        const juce::Colour colour = recording ? juce::Colour(0xFFE57373) : processorRef.getClipColour(trackIdx, clipIdx);
        const int style = recording ? 1 : (has ? static_cast<int>(colour.getARGB() & 0x7FFFFFFF) : (armed ? 2 : 0));
        if (slot.styledAs == style) {
            return;
        }
        slot.styledAs = style;
        if (!has && !recording) {
            const juce::Colour muted = juce::Colour(armed ? 0xFFE57373 : CliptoneLookAndFeel::Palette::TEXT_MUTED);
            slot.setButtonText(armed ? juce::String::fromUTF8("\xe2\x97\x8f") : juce::String("+"));
            slot.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
            slot.setColour(juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
            slot.setColour(juce::TextButton::textColourOffId, muted);
            slot.setColour(juce::TextButton::textColourOnId, muted);
            slot.setColour(juce::ComboBox::outlineColourId, muted);
        } else {
            slot.setColour(juce::TextButton::buttonColourId, colour.withAlpha(0.28f));
            slot.setColour(juce::TextButton::buttonOnColourId, colour);
            slot.setColour(juce::TextButton::textColourOffId, juce::Colour(CliptoneLookAndFeel::Palette::TEXT));
            slot.setColour(juce::TextButton::textColourOnId, juce::Colour(CliptoneLookAndFeel::Palette::BACKGROUND));
            slot.setColour(juce::ComboBox::outlineColourId, colour.withAlpha(0.6f));
        }
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
            const bool armed = processorRef.isTrackArmed(trackIdx);
            const int recordingSlot = processorRef.getRecordingSlot(trackIdx);
            trackLabels[static_cast<size_t>(trackIdx)].setToggleState(armed, juce::dontSendNotification);

            for (int clipIdx = 0; clipIdx < numClips; clipIdx++) {
                auto& slot = slotAt(trackIdx, clipIdx);
                styleSlot(slot, trackIdx, clipIdx, armed, clipIdx == recordingSlot);
                if (clipIdx == recordingSlot) {
                    slot.setToggleState(blinkOn, juce::dontSendNotification);
                    continue;
                }
                if (!processorRef.hasClip(trackIdx, clipIdx)) {
                    // Empty slots never fill or blink — an outlined "+" (or a record button when armed).
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
