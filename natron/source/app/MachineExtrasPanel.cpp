#include "MachineExtrasPanel.h"

#include "core/SampleData.h"
#include "core/dsp/DrumKit.h"
#include "core/machines/Bytebeat.h"
#include "core/machines/DrumMachine.h"
#include "core/machines/Sampler.h"
#include "core/machines/Vocoder.h"

#include <juce_audio_formats/juce_audio_formats.h>

MachineExtrasPanel::MachineExtrasPanel(Session& sessionToUse, int machineIndex,
                                       std::function<void(const juce::String&)> errorHandler) :
    session(sessionToUse), machine(machineIndex), showError(std::move(errorHandler)) {
    type = session.getSong().machines[static_cast<size_t>(machine)].type;
    addAndMakeVisible(status);

    if (type == Bytebeat::typeName) {
        formula.setText(session.getMachineData(machine).getProperty("formula").toString(), false);
        formula.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 16.0f, juce::Font::plain));
        formula.onReturnKey = [this] { applyButton.triggerClick(); };
        addAndMakeVisible(formula);
        applyButton.onClick = [this] {
            const auto result = session.setMachineData(machine, Bytebeat::dataFor(formula.getText()));
            status.setText(result.wasOk() ? "Playing: " + formula.getText() : result.getErrorMessage(), juce::dontSendNotification);
        };
        addAndMakeVisible(applyButton);
        status.setText("Formula of t (C syntax): + - * / % & | ^ << >> ~ ! < > == != ?:", juce::dontSendNotification);
        return;
    }

    if (type == DrumMachine::typeName) {
        for (int slot = 0; slot < DrumMachine::numSlots; slot++) {
            slotBox.addItem(DrumKit::names()[static_cast<size_t>(slot)], slot + 1);
        }
        slotBox.setSelectedId(1, juce::dontSendNotification);
        slotBox.onChange = [this] { updateStatus(); };
        addAndMakeVisible(slotBox);
        resetButton.setButtonText("Kit sound");
    }
    if (type == Vocoder::typeName) {
        loadButton.setButtonText("Load voice...");
    }
    loadButton.onClick = [this] { loadSample(); };
    resetButton.onClick = [this] { resetToBuiltIn(); };
    addAndMakeVisible(loadButton);
    addAndMakeVisible(resetButton);
    updateStatus();
}

bool MachineExtrasPanel::hasExtras(const juce::String& machineType) {
    return machineType == Sampler::typeName || machineType == DrumMachine::typeName || machineType == Vocoder::typeName
           || machineType == Bytebeat::typeName;
}

void MachineExtrasPanel::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(0xff282a2e));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
}

void MachineExtrasPanel::resized() {
    auto area = getLocalBounds().reduced(8);
    status.setBounds(area.removeFromBottom(28));
    auto row = area.removeFromTop(36);
    if (type == Bytebeat::typeName) {
        applyButton.setBounds(row.removeFromRight(90));
        row.removeFromRight(6);
        formula.setBounds(row);
        return;
    }
    if (slotBox.isVisible()) {
        slotBox.setBounds(row.removeFromLeft(150));
        row.removeFromLeft(6);
    }
    loadButton.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(6);
    resetButton.setBounds(row.removeFromLeft(110));
}

void MachineExtrasPanel::updateStatus() {
    const auto data = session.getMachineData(machine);
    juce::String text;
    if (type == Sampler::typeName) {
        const auto zone = data.getChild(0);
        text = zone.getProperty("builtIn").toString() == "keys" ? "Built-in keys"
                                                                 : zone.getChildWithName("Sample").getProperty("name").toString();
    } else if (type == Vocoder::typeName) {
        const auto sample = data.getChildWithName("Sample");
        text = sample.isValid() ? "Voice: " + sample.getProperty("name").toString() : "Voice: built-in vowels";
    } else if (type == DrumMachine::typeName) {
        const int slot = slotBox.getSelectedId() - 1;
        text = DrumKit::names()[static_cast<size_t>(juce::jmax(0, slot))] + juce::String(": kit sound");
        for (const auto& child : data) {
            if (static_cast<int>(child.getProperty("index", -1)) == slot) {
                text = slotBox.getText() + ": " + child.getChildWithName("Sample").getProperty("name").toString();
            }
        }
    }
    status.setText(text, juce::dontSendNotification);
}

void MachineExtrasPanel::loadSample() {
    chooser = std::make_unique<juce::FileChooser>("Load sample", juce::File::getSpecialLocation(juce::File::userMusicDirectory),
                                                  "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe = juce::Component::SafePointer<MachineExtrasPanel>(this)](const juce::FileChooser& fileChooser) {
        if (safe == nullptr) {
            return;
        }
        const auto url = fileChooser.getURLResult();
        if (url.isEmpty()) {
            return;
        }
        const auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress));
        if (stream == nullptr) {
            safe->showError("Could not open the file");
            return;
        }
        SampleData sample;
        const auto name = url.isLocalFile() ? url.getLocalFile().getFileNameWithoutExtension() : url.getFileName();
        if (const auto result = SampleData::load(*stream, name, sample); result.failed()) {
            safe->showError(result.getErrorMessage());
            return;
        }
        safe->useSample(sample);
    });
}

void MachineExtrasPanel::useSample(const SampleData& sample) {
    juce::ValueTree data;
    if (type == Sampler::typeName) {
        Sampler::Zone zone;
        zone.sample = sample;
        data = Sampler::dataFor({ zone });
    } else if (type == Vocoder::typeName) {
        data = juce::ValueTree("Vocoder");
        data.appendChild(sample.toTree(), nullptr);
    } else if (type == DrumMachine::typeName) {
        data = DrumMachine::withSample(session.getMachineData(machine), slotBox.getSelectedId() - 1, sample);
    }
    if (const auto result = session.setMachineData(machine, data); result.failed()) {
        showError(result.getErrorMessage());
    }
    updateStatus();
}

void MachineExtrasPanel::resetToBuiltIn() {
    juce::ValueTree data;
    if (type == Sampler::typeName) {
        data = Sampler::dataFor({ Sampler::defaultZone() });
    } else if (type == Vocoder::typeName) {
        data = juce::ValueTree("Vocoder");
    } else if (type == DrumMachine::typeName) {
        data = DrumMachine::withoutSample(session.getMachineData(machine), slotBox.getSelectedId() - 1);
    }
    session.setMachineData(machine, data);
    updateStatus();
}
