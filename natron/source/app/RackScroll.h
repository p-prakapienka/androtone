#pragma once

#include "FxRackView.h"
#include "MachineExtrasPanel.h"
#include "MixerView.h"
#include "ParameterPanel.h"
#include "PatternEditorView.h"
#include "RackView.h"
#include "core/Session.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

// The rack: every screen stacked in one vertical scroll, in this order:
//   machine 1 sound editor, machine 1 pattern editor, ... machine N sound editor, machine N
//   pattern editor, (add machine), FX 1-8, FX 9-16, Mixer 1-8, Mixer 9-16, Master.
// Every screen is exactly one view tall; after a swipe the page settles on the nearest screen. A
// grip down the left edge scrolls the whole page at once (drag it, or tap it to jump). The machine
// whose screens are in view is the one the keyboard and MIDI devices play (onMachineInView).
class RackScroll : public juce::Component, private juce::Timer {
public:
    RackScroll(Session& sessionToUse, const Engine& engineToShow, juce::MidiKeyboardState& keyboardState,
               std::function<void(const juce::String&)> errorHandler);
    ~RackScroll() override;

    // Rebuilds every screen from the song, keeping the scroll position.
    void rebuild();
    // Scrolls to machine `machine`'s sound editor.
    void showMachine(int machine);
    void repaintPlaying(); // the pads and grids that follow the playhead

    std::function<void(int machine)> onMachineInView;
    std::function<void()> onRackChanged; // a machine was added or removed, or patterns pasted/cleared

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class SoundScreen;
    class PatternScreen;
    class TitledScreen;
    class Grip;
    class Scroller : public juce::Viewport {
    public:
        std::function<void()> onScroll;
        void visibleAreaChanged(const juce::Rectangle<int>&) override {
            if (onScroll) {
                onScroll();
            }
        }
    };
    static constexpr int gripWidth = 30;

    Session& session;
    const Engine& engine;
    juce::MidiKeyboardState& keyboard;
    std::function<void(const juce::String&)> showError;

    Scroller viewport;
    std::unique_ptr<Grip> grip;
    double lastScrollTime = 0.0;
    bool settling = false;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> screens;
    std::vector<int> screenMachine; // the machine each screen belongs to, or -1
    juce::TextButton addButton { "+  ADD MACHINE" };
    int machineInView = -1;

    void layoutScreens();
    void updateMachineInView();
    void settleOnNearestScreen();
    int screenHeight() const;
    void showAddMenu();
    void timerCallback() override;
};
