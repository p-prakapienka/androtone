#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "AndrotoneLookAndFeel.h"
#include "ClipPropertiesPanel.h"
#include "NoteGrid.h"

// Clip view: the selected clip's properties and its note editor.
//
// Toolbar operations apply to the selected notes, or to every note when none is selected
// (transpose, quantize, mute), each as one undo step.
class ClipTab : public juce::Component, private juce::Timer {
public:
    explicit ClipTab(AndrotoneAudioProcessor& p) : processorRef(p), grid(p) {
        addAndMakeVisible(grid);
        grid.onSelectionChanged = [this]() { updateButtons(); };

        addAndMakeVisible(title);
        title.setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::ACCENT));

        setUpButton(lengthDown, "-", "Shorten clip by a bar", [this]() { changeLength(-Clip::BEATS_PER_BAR); });
        setUpButton(lengthUp, "+", "Lengthen clip by a bar", [this]() { changeLength(Clip::BEATS_PER_BAR); });
        addAndMakeVisible(lengthLabel);
        lengthLabel.setJustificationType(juce::Justification::centred);

        loopButton.setButtonText("Loop");
        loopButton.setClickingTogglesState(true);
        loopButton.onClick = [this]() {
            const bool looping = loopButton.getToggleState();
            grid.commitEdit(looping ? "Loop on" : "Loop off", [looping](ClipData& d) {
                d.looping = looping;
                if (looping) {
                    d.loopEnd = juce::jmax(d.loopEnd, d.loopStart + ClipData::MIN_LENGTH);
                }
            });
            refresh();
        };
        addAndMakeVisible(loopButton);

        gridChooser.addItemList({ "1/32", "1/16", "1/8", "1/4", "1/2", "1 Bar" }, 1);
        gridChooser.setSelectedId(2, juce::dontSendNotification);
        gridChooser.setTooltip("Grid");
        gridChooser.onChange = [this]() {
            static const double sizes[] { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0 };
            grid.setGrid(sizes[gridChooser.getSelectedItemIndex()]);
        };
        addAndMakeVisible(gridChooser);

        snapButton.setButtonText("Snap");
        snapButton.setClickingTogglesState(true);
        snapButton.setToggleState(true, juce::dontSendNotification);
        snapButton.onClick = [this]() { grid.setSnap(snapButton.getToggleState()); };
        addAndMakeVisible(snapButton);

        foldButton.setButtonText("Fold");
        foldButton.setClickingTogglesState(true);
        foldButton.setTooltip("Show only rows that contain notes");
        foldButton.onClick = [this]() { grid.setFold(foldButton.getToggleState()); };
        addAndMakeVisible(foldButton);

        quantizeAmount.setRange(0.0, 100.0, 1.0);
        quantizeAmount.setValue(100.0, juce::dontSendNotification);
        quantizeAmount.setSliderStyle(juce::Slider::LinearBar);
        quantizeAmount.setTextValueSuffix("%");
        quantizeAmount.setTooltip("Quantize amount");
        addAndMakeVisible(quantizeAmount);
        setUpButton(quantizeButton, "Quantize", "Move note starts towards the grid", [this]() { quantize(); });

        setUpButton(octaveDown, "-12", "Transpose down an octave", [this]() { transpose(-12); });
        setUpButton(semiDown, "-1", "Transpose down a semitone", [this]() { transpose(-1); });
        setUpButton(semiUp, "+1", "Transpose up a semitone", [this]() { transpose(1); });
        setUpButton(octaveUp, "+12", "Transpose up an octave", [this]() { transpose(12); });

        setUpButton(duplicateButton, "x2 Loop", "Duplicate the loop and its notes", [this]() { duplicateLoop(); });
        setUpButton(muteButton, "Mute", "Deactivate / activate the selected notes", [this]() { toggleMute(); });
        setUpButton(selectAllButton, "All", "Select all notes", [this]() { grid.selectAll(); });
        setUpButton(deleteButton, "Del", "Delete the selected notes", [this]() { deleteSelection(); });
        setUpButton(zoomOut, juce::String::fromUTF8("\xe2\x88\x92"), "Zoom out", [this]() { grid.zoomBy(1.6, 0.0); });
        setUpButton(zoomIn, "+", "Zoom in", [this]() { grid.zoomBy(0.625, 0.0); });
        setUpButton(launchButton, "Launch...", "Launch mode and follow actions", [this]() { showLaunchSettings(); });
        setUpButton(toArrangementButton, "To Arr.", "Place this clip on the arrangement at the insert marker", [this]() {
            processorRef.placeClipInArrangement(grid.getTrack(), grid.getClipIndex());
        });
        setUpButton(removeClipButton, "Remove", "Delete this clip from its slot", [this]() {
            const auto data = processorRef.getClipData(grid.getTrack(), grid.getClipIndex());
            if (data != nullptr) {
                processorRef.performClipEdit(grid.getTrack(), grid.getClipIndex(), data, nullptr, "Delete clip");
                refresh();
            }
        });

        updateButtons();
        startTimerHz(4);
    }

    void showClip(int trackIndex, int clipIndex) {
        grid.setClip(trackIndex, clipIndex);
        refresh();
    }

    // After undo / redo or edits elsewhere.
    void refresh() {
        grid.reload();
        const auto data = processorRef.getClipData(grid.getTrack(), grid.getClipIndex());
        if (data == nullptr) {
            title.setText(grid.getTrack() < 0 ? "No clip" : "Empty slot", juce::dontSendNotification);
            lengthLabel.setText({}, juce::dontSendNotification);
        } else {
            const juce::String name = data->name.isNotEmpty() ? data->name : "Clip " + juce::String(grid.getClipIndex() + 1);
            title.setText("Track " + juce::String(grid.getTrack() + 1) + "  /  " + name, juce::dontSendNotification);
            lengthLabel.setText(formatBars(data->looping ? data->loopEnd - data->loopStart : data->endMarker - data->startMarker),
                                juce::dontSendNotification);
            loopButton.setToggleState(data->looping, juce::dontSendNotification);
        }
        updateButtons();
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(6);
        auto row1 = bounds.removeFromTop(30);
        bounds.removeFromTop(4);
        auto row2 = bounds.removeFromTop(30);
        bounds.removeFromTop(6);

        title.setBounds(row1.removeFromLeft(juce::jmin(260, row1.getWidth() / 3)));
        auto place = [](juce::Rectangle<int>& row, juce::Component& c, int width) {
            c.setBounds(row.removeFromLeft(width).reduced(2, 0));
        };
        place(row1, lengthDown, 30);
        place(row1, lengthLabel, 64);
        place(row1, lengthUp, 30);
        place(row1, loopButton, 56);
        place(row1, launchButton, 80);
        row1.removeFromLeft(8);
        place(row1, zoomOut, 30);
        place(row1, zoomIn, 30);
        row1.removeFromLeft(8);
        place(row1, toArrangementButton, 70);
        place(row1, removeClipButton, 70);

        place(row2, gridChooser, 74);
        place(row2, snapButton, 56);
        place(row2, foldButton, 52);
        row2.removeFromLeft(6);
        place(row2, quantizeButton, 76);
        place(row2, quantizeAmount, 56);
        row2.removeFromLeft(6);
        place(row2, octaveDown, 44);
        place(row2, semiDown, 38);
        place(row2, semiUp, 38);
        place(row2, octaveUp, 44);
        row2.removeFromLeft(6);
        place(row2, duplicateButton, 66);
        place(row2, muteButton, 50);
        place(row2, selectAllButton, 40);
        place(row2, deleteButton, 40);

        grid.setBounds(bounds);
    }

private:
    AndrotoneAudioProcessor& processorRef;
    NoteGrid grid;
    juce::Label title;
    juce::TextButton lengthDown, lengthUp, loopButton, snapButton, foldButton, quantizeButton;
    juce::TextButton octaveDown, semiDown, semiUp, octaveUp, duplicateButton, muteButton, selectAllButton, deleteButton;
    juce::TextButton zoomOut, zoomIn, launchButton, removeClipButton, toArrangementButton;
    juce::Label lengthLabel;
    juce::ComboBox gridChooser;
    juce::Slider quantizeAmount;

    void timerCallback() override {
        // Pick up changes made elsewhere (undo/redo, session edits).
        const auto data = processorRef.getClipData(grid.getTrack(), grid.getClipIndex());
        if (data != lastSeen) {
            const bool ownEdit = data == grid.getShownData();
            lastSeen = data;
            if (!ownEdit) {
                grid.clearSelection(); // undo / redo / outside change: old indices are meaningless
            }
            refresh();
        }
    }

    ClipData::Ptr lastSeen;

    void setUpButton(juce::TextButton& button, const juce::String& text, const juce::String& tip, std::function<void()> action) {
        button.setButtonText(text);
        button.setTooltip(tip);
        button.onClick = std::move(action);
        addAndMakeVisible(button);
    }

    void updateButtons() {
        const bool has = grid.hasClip();
        for (auto* c : std::initializer_list<juce::Component*> {
                 &lengthDown, &lengthUp, &loopButton, &quantizeButton, &quantizeAmount, &octaveDown, &semiDown,
                 &semiUp, &octaveUp, &duplicateButton, &muteButton, &selectAllButton, &launchButton, &removeClipButton, &toArrangementButton }) {
            c->setEnabled(has);
        }
        deleteButton.setEnabled(has && !grid.getSelection().empty());
    }

    static juce::String formatBars(double beats) {
        const int bars = static_cast<int>(beats / Clip::BEATS_PER_BAR);
        const double rest = beats - bars * Clip::BEATS_PER_BAR;
        if (rest < 1e-6) {
            return juce::String(bars) + (bars == 1 ? " bar" : " bars");
        }
        return juce::String(beats, 2) + " beats";
    }

    void changeLength(double delta) {
        grid.commitEdit("Change clip length", [delta](ClipData& d) {
            if (d.looping) {
                d.loopEnd = juce::jmax(d.loopStart + Clip::BEATS_PER_BAR, d.loopEnd + delta);
                d.endMarker = juce::jmax(d.endMarker + delta, d.loopEnd);
            } else {
                d.endMarker = juce::jmax(d.startMarker + Clip::BEATS_PER_BAR, d.endMarker + delta);
                d.loopEnd = juce::jmin(d.loopEnd, d.endMarker);
            }
        });
        grid.fitView();
        refresh();
    }

    void quantize() {
        const double step = grid.getGrid();
        const double amount = quantizeAmount.getValue() / 100.0;
        const auto targets = grid.getTargetIndices();
        grid.commitEdit("Quantize", [&](ClipData& d) { ClipEdits::quantize(d, targets, step, amount); });
    }

    void transpose(int semitones) {
        const auto targets = grid.getTargetIndices();
        grid.commitEdit("Transpose", [&](ClipData& d) { ClipEdits::transpose(d, targets, semitones); });
    }

    void toggleMute() {
        const auto targets = grid.getTargetIndices();
        grid.commitEdit("Mute notes", [&](ClipData& d) {
            bool anyActive = false;
            for (int index : targets) {
                anyActive = anyActive || !d.notes[static_cast<size_t>(index)].muted;
            }
            for (int index : targets) {
                d.notes[static_cast<size_t>(index)].muted = anyActive;
            }
        });
    }

    void deleteSelection() {
        const auto& selection = grid.getSelection();
        const std::set<int> doomed(selection.begin(), selection.end());
        grid.clearSelection();
        grid.commitEdit("Delete notes", [&](ClipData& d) {
            std::vector<MidiNote> kept;
            for (int i = 0; i < static_cast<int>(d.notes.size()); i++) {
                if (doomed.count(i) == 0) {
                    kept.push_back(d.notes[static_cast<size_t>(i)]);
                }
            }
            d.notes = kept;
        });
        refresh();
    }

    // §8.9: the loop doubles; its notes are copied after it, and later notes shift along.
    void duplicateLoop() {
        grid.commitEdit("Duplicate loop", [](ClipData& d) { ClipEdits::duplicateLoop(d); });
        grid.fitView();
        refresh();
    }

private:
    void showLaunchSettings() {
        if (!grid.hasClip()) {
            return;
        }
        juce::CallOutBox::launchAsynchronously(
            std::make_unique<ClipPropertiesPanel>(processorRef, grid.getTrack(), grid.getClipIndex()),
            launchButton.getScreenBounds(), nullptr);
    }
};
