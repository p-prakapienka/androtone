#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../PluginProcessor.h"
#include "AndrotoneLookAndFeel.h"

// Set management: new / open / save / save as. A set (.androtone, XML) holds every clip, the
// arrangement, the mixer and the global settings; the undo history is not saved.
class ProjectTab : public juce::Component, private juce::Timer {
public:
    explicit ProjectTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        for (auto [button, text] : { std::make_pair(&newButton, "New Set"), std::make_pair(&openButton, "Open..."),
                                     std::make_pair(&saveButton, "Save"), std::make_pair(&saveAsButton, "Save As...") }) {
            button->setButtonText(text);
            addAndMakeVisible(*button);
        }
        newButton.onClick = [this]() { processorRef.newSet(); };
        openButton.onClick = [this]() { openSet(); };
        saveButton.onClick = [this]() {
            const auto file = processorRef.getCurrentSetFile();
            if (file == juce::File()) {
                saveSetAs();
            } else {
                report(processorRef.saveSet(file) ? "Saved " + file.getFileName() : "Could not save " + file.getFullPathName());
            }
        };
        saveAsButton.onClick = [this]() { saveSetAs(); };

        title.setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::ACCENT));
        title.setFont(juce::FontOptions(18.0f));
        addAndMakeVisible(title);
        status.setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::TEXT_MUTED));
        addAndMakeVisible(status);
        refreshTitle();
        startTimerHz(2);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(20);
        title.setBounds(bounds.removeFromTop(32));
        bounds.removeFromTop(10);
        auto row = bounds.removeFromTop(40);
        for (auto* button : { &newButton, &openButton, &saveButton, &saveAsButton }) {
            button->setBounds(row.removeFromLeft(120).reduced(4, 2));
        }
        bounds.removeFromTop(10);
        status.setBounds(bounds.removeFromTop(28));
    }

private:
    AndrotoneAudioProcessor& processorRef;
    juce::TextButton newButton, openButton, saveButton, saveAsButton;
    juce::Label title, status;
    std::unique_ptr<juce::FileChooser> chooser;

    void timerCallback() override {
        refreshTitle();
    }

    void refreshTitle() {
        const auto file = processorRef.getCurrentSetFile();
        title.setText(file == juce::File() ? "Untitled set" : file.getFileNameWithoutExtension(), juce::dontSendNotification);
    }

    void report(const juce::String& message) {
        status.setText(message, juce::dontSendNotification);
        refreshTitle();
    }

    static juce::File defaultFolder() {
        return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    }

    void openSet() {
        chooser = std::make_unique<juce::FileChooser>("Open set", defaultFolder(), "*.androtone");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& fc) {
                                 const auto file = fc.getResult();
                                 if (file != juce::File()) {
                                     report(processorRef.loadSet(file) ? "Opened " + file.getFileName()
                                                                       : file.getFileName() + " is not an Androtone set");
                                 }
                             });
    }

    void saveSetAs() {
        chooser = std::make_unique<juce::FileChooser>("Save set", defaultFolder().getChildFile("Untitled.androtone"), "*.androtone");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                             [this](const juce::FileChooser& fc) {
                                 auto file = fc.getResult();
                                 if (file == juce::File()) {
                                     return;
                                 }
                                 file = file.withFileExtension(".androtone");
                                 report(processorRef.saveSet(file) ? "Saved " + file.getFileName()
                                                                   : "Could not save " + file.getFullPathName());
                             });
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectTab)
};
