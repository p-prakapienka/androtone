#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"

class ProjectTab : public juce::Component, private juce::ListBoxModel {
public:
    explicit ProjectTab(AndrotoneAudioProcessor& p) : processorRef(p) {
        projectList.setModel(this);
        projectList.setRowHeight(44);
        projectList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        projectList.setColour(juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(projectList);
        projectList.selectRow(processorRef.getCurrentProjectIndex(), false, false);
    }

    ~ProjectTab() override {
        projectList.setModel(nullptr);
    }

    void resized() override {
        projectList.setBounds(getLocalBounds().reduced(10));
    }

private:
    int getNumRows() override {
        return processorRef.getNumProjects();
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height,
                          bool rowIsSelected) override {
        if (rowIsSelected) {
            g.setColour(findColour(juce::TextButton::buttonOnColourId));
            g.fillRoundedRectangle(0.0f, 2.0f, static_cast<float>(width),
                                   static_cast<float>(height - 4), 4.0f);
            g.setColour(findColour(juce::TextButton::textColourOnId));
        } else {
            g.setColour(findColour(juce::TextButton::textColourOffId));
        }

        g.setFont(juce::FontOptions(16.0f));
        g.drawText(processorRef.getProjectName(rowNumber), 12, 0, width - 24, height,
                   juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int lastRowSelected) override {
        processorRef.loadProject(lastRowSelected);
    }

    AndrotoneAudioProcessor& processorRef;
    juce::ListBox projectList { "Projects", this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectTab)
};
