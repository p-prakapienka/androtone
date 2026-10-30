#pragma once

#include "core/SampleData.h"
#include "core/Session.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Machine-specific controls beyond knobs, editing the machine's data through the Session:
//  Sampler  — load a sample (or go back to the built-in one)
//  Drums    — load a sample into a slot (or reset the slot to the kit)
//  Vocoder  — load a voice (or use the built-in one)
//  Bytebeat — edit the formula
class MachineExtrasPanel : public juce::Component {
public:
    MachineExtrasPanel(Session& sessionToUse, int machineIndex, std::function<void(const juce::String&)> errorHandler);

    static bool hasExtras(const juce::String& machineType);
    int getIdealHeight() const { return 84; }

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    Session& session;
    int machine;
    juce::String type;
    std::function<void(const juce::String&)> showError;

    juce::Label status;
    juce::ComboBox slotBox;
    juce::TextButton loadButton { "Load sample..." };
    juce::TextButton resetButton { "Built-in" };
    juce::TextEditor formula;
    juce::TextButton applyButton { "Apply" };
    std::unique_ptr<juce::FileChooser> chooser;

    void loadSample();
    void useSample(const SampleData& sample);
    void resetToBuiltIn();
    void updateStatus();
};
