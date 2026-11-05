#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../PluginProcessor.h"
#include "AndrotoneLookAndFeel.h"

// Launch-mode and follow-action settings for one clip, shown in a call-out from the Session tab.
// Every control writes straight into the clip's (atomic) ClipSettings, so changes apply live.
class ClipPropertiesPanel : public juce::Component {
public:
    ClipPropertiesPanel(AndrotoneAudioProcessor& p, int trackIndex, int clipIndex) :
        settings(p.getClipSettings(trackIndex, clipIndex)) {

        title.setText("Track " + juce::String(trackIndex + 1) + "  /  Clip " + juce::String(clipIndex + 1),
                      juce::dontSendNotification);
        title.setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::ACCENT));
        addAndMakeVisible(title);

        setUpLabel(launchLabel, "Launch");
        launchMode.addItemList({ "Trigger", "Gate", "Toggle", "Repeat" }, 1);
        launchMode.setSelectedId(static_cast<int>(settings.launchMode.load()) + 1, juce::dontSendNotification);
        launchMode.onChange = [this]() {
            settings.launchMode.store(static_cast<LaunchMode>(launchMode.getSelectedId() - 1));
        };
        addAndMakeVisible(launchMode);

        followEnabled.setButtonText("Follow action");
        followEnabled.setToggleState(settings.followEnabled.load(), juce::dontSendNotification);
        followEnabled.onClick = [this]() {
            settings.followEnabled.store(followEnabled.getToggleState());
            updateEnablement();
        };
        addAndMakeVisible(followEnabled);

        setUpAction(actionA, actionALabel, "A", settings.followActionA);
        setUpAction(actionB, actionBLabel, "B", settings.followActionB);
        setUpChance(chanceA, settings.chanceA);
        setUpChance(chanceB, settings.chanceB);

        linked.setButtonText("Linked to clip length");
        linked.setToggleState(settings.followLinked.load(), juce::dontSendNotification);
        linked.onClick = [this]() {
            settings.followLinked.store(linked.getToggleState());
            updateEnablement();
        };
        addAndMakeVisible(linked);

        setUpLabel(timeLabel, "After");

        multiplier.setSliderStyle(juce::Slider::IncDecButtons);
        multiplier.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 24);
        multiplier.setRange(1.0, 16.0, 1.0);
        multiplier.setValue(settings.followMultiplier.load(), juce::dontSendNotification);
        multiplier.textFromValueFunction = [](double value) {
            const int passes = static_cast<int>(value);
            return juce::String(passes) + (passes == 1 ? " pass" : " passes");
        };
        multiplier.valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
        multiplier.updateText();
        multiplier.onValueChange = [this]() {
            settings.followMultiplier.store(static_cast<int>(multiplier.getValue()));
        };
        addChildComponent(multiplier);

        // Unlinked follow time, in 16th steps, shown as bars.beats.16ths (counted from 0).
        followTime.setSliderStyle(juce::Slider::IncDecButtons);
        followTime.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 24);
        followTime.setRange(1.0, 256.0, 1.0);
        followTime.setValue(settings.followTimeSteps.load(), juce::dontSendNotification);
        followTime.textFromValueFunction = [](double value) {
            const int steps = static_cast<int>(value);
            return juce::String(steps / 16) + "." + juce::String((steps % 16) / 4) + "." + juce::String(steps % 4);
        };
        followTime.valueFromTextFunction = [](const juce::String& text) {
            juce::StringArray parts;
            parts.addTokens(text, ".", {});
            const int bars = parts[0].getIntValue();
            const int beats = parts[1].getIntValue();
            const int sixteenths = parts[2].getIntValue();
            return static_cast<double>(bars * 16 + beats * 4 + sixteenths);
        };
        followTime.updateText();
        followTime.onValueChange = [this]() {
            settings.followTimeSteps.store(static_cast<int>(followTime.getValue()));
        };
        addChildComponent(followTime);

        updateEnablement();
        setSize(300, 7 * ROW_HEIGHT + 2 * PADDING);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(PADDING);
        auto row = [&bounds]() { return bounds.removeFromTop(ROW_HEIGHT).reduced(0, 3); };
        constexpr int labelWidth = 60;
        constexpr int chanceWidth = 90;

        title.setBounds(row());

        auto launchRow = row();
        launchLabel.setBounds(launchRow.removeFromLeft(labelWidth));
        launchMode.setBounds(launchRow);

        followEnabled.setBounds(row());

        auto aRow = row();
        actionALabel.setBounds(aRow.removeFromLeft(labelWidth));
        chanceA.setBounds(aRow.removeFromRight(chanceWidth));
        actionA.setBounds(aRow.withTrimmedRight(6));

        auto bRow = row();
        actionBLabel.setBounds(bRow.removeFromLeft(labelWidth));
        chanceB.setBounds(bRow.removeFromRight(chanceWidth));
        actionB.setBounds(bRow.withTrimmedRight(6));

        linked.setBounds(row());

        auto timeRow = row();
        timeLabel.setBounds(timeRow.removeFromLeft(labelWidth));
        multiplier.setBounds(timeRow);
        followTime.setBounds(timeRow);
    }

private:
    static constexpr int ROW_HEIGHT = 32;
    static constexpr int PADDING = 10;

    ClipSettings& settings;

    juce::Label title;
    juce::Label launchLabel;
    juce::ComboBox launchMode;
    juce::ToggleButton followEnabled;
    juce::Label actionALabel;
    juce::ComboBox actionA;
    juce::Slider chanceA;
    juce::Label actionBLabel;
    juce::ComboBox actionB;
    juce::Slider chanceB;
    juce::ToggleButton linked;
    juce::Label timeLabel;
    juce::Slider multiplier;
    juce::Slider followTime;

    void setUpLabel(juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, juce::Colour(AndrotoneLookAndFeel::Palette::TEXT_MUTED));
        addAndMakeVisible(label);
    }

    void setUpAction(juce::ComboBox& box, juce::Label& label, const juce::String& name,
                     std::atomic<FollowAction>& target) {
        setUpLabel(label, "Action " + name);
        // Order matches the FollowAction enum.
        box.addItemList({ "None", "Stop", "Again", "Previous", "Next", "First", "Last", "Any", "Other" }, 1);
        box.setSelectedId(static_cast<int>(target.load()) + 1, juce::dontSendNotification);
        box.onChange = [&box, &target]() {
            target.store(static_cast<FollowAction>(box.getSelectedId() - 1));
        };
        addAndMakeVisible(box);
    }

    void setUpChance(juce::Slider& slider, std::atomic<int>& target) {
        slider.setSliderStyle(juce::Slider::LinearBar);
        slider.setRange(0.0, 100.0, 1.0);
        slider.setTextValueSuffix("%");
        slider.setValue(target.load(), juce::dontSendNotification);
        slider.onValueChange = [&slider, &target]() {
            target.store(static_cast<int>(slider.getValue()));
        };
        addAndMakeVisible(slider);
    }

    void updateEnablement() {
        const bool enabled = followEnabled.getToggleState();
        for (auto* component : std::initializer_list<juce::Component*> {
                 &actionA, &actionB, &chanceA, &chanceB, &linked, &multiplier, &followTime,
                 &actionALabel, &actionBLabel, &timeLabel }) {
            component->setEnabled(enabled);
        }
        multiplier.setVisible(linked.getToggleState());
        followTime.setVisible(!linked.getToggleState());
    }
};
