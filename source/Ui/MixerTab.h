#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <memory>
#include <vector>

#include "../PluginProcessor.h"
#include "CliptoneLookAndFeel.h"

// Vertical peak meter. Polls a peak source (which resets on read) and decays smoothly.
class PeakMeter : public juce::Component {
public:
    explicit PeakMeter(std::function<float()> source) : takePeak(std::move(source)) {}

    void poll() {
        const float peak = takePeak();
        level = juce::jmax(peak, level * 0.85f);
        hold = peak >= hold ? peak : hold * 0.97f;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(CliptoneLookAndFeel::Palette::SURFACE));
        g.fillRoundedRectangle(bounds, 2.0f);
        const float db = juce::Decibels::gainToDecibels(level, -60.0f);
        const float fill = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 66.0f);
        auto bar = bounds.removeFromBottom(bounds.getHeight() * fill);
        g.setColour(level > 1.0f ? juce::Colour(0xFFE57373) : juce::Colour(CliptoneLookAndFeel::Palette::ACCENT));
        g.fillRoundedRectangle(bar, 2.0f);
        const float holdDb = juce::Decibels::gainToDecibels(hold, -60.0f);
        const float holdY = getHeight() * (1.0f - juce::jlimit(0.0f, 1.0f, (holdDb + 60.0f) / 66.0f));
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawHorizontalLine(static_cast<int>(holdY), 0.0f, static_cast<float>(getWidth()));
    }

private:
    std::function<float()> takePeak;
    float level = 0.0f;
    float hold = 0.0f;
};

// Mixer: one strip per track (activator, sends, pan, volume + meter, solo, arm), the two return
// strips (level, pre/post) and the main strip.
class MixerTab : public juce::Component, private juce::Timer {
public:
    explicit MixerTab(CliptoneAudioProcessor& p) : processorRef(p) {
        const int numTracks = processorRef.getNumActiveTracks();
        for (int t = 0; t < numTracks; t++) {
            strips.push_back(std::make_unique<TrackStrip>(processorRef, t));
            addAndMakeVisible(*strips.back());
        }
        returnStrips.push_back(std::make_unique<ReturnStrip>(processorRef, Mixer::DELAY_RETURN, "Delay"));
        returnStrips.push_back(std::make_unique<ReturnStrip>(processorRef, Mixer::REVERB_RETURN, "Reverb"));
        for (auto& strip : returnStrips) {
            addAndMakeVisible(*strip);
        }

        masterLabel.setText("Main", juce::dontSendNotification);
        masterLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(masterLabel);
        setUpFader(masterFader, processorRef.getVolume(), [this]() { processorRef.setVolume(static_cast<float>(masterFader.getValue())); });
        addAndMakeVisible(masterFader);
        addAndMakeVisible(masterMeter);

        startTimerHz(30);
    }

    void resized() override {
        auto bounds = getLocalBounds().reduced(8);
        const int columns = static_cast<int>(strips.size() + returnStrips.size()) + 1;
        const int columnWidth = juce::jmin(110, bounds.getWidth() / juce::jmax(1, columns));
        for (auto& strip : strips) {
            strip->setBounds(bounds.removeFromLeft(columnWidth).reduced(3, 0));
        }
        bounds.removeFromLeft(10);
        for (auto& strip : returnStrips) {
            strip->setBounds(bounds.removeFromLeft(columnWidth).reduced(3, 0));
        }
        auto master = bounds.removeFromLeft(columnWidth).reduced(3, 0);
        masterLabel.setBounds(master.removeFromTop(28));
        master.removeFromBottom(4);
        masterMeter.setBounds(master.removeFromRight(10));
        masterFader.setBounds(master);
    }

    static void setUpFader(juce::Slider& fader, float value, std::function<void()> onChange) {
        fader.setSliderStyle(juce::Slider::LinearVertical);
        fader.setRange(0.0, 1.25, 0.001);
        fader.setSkewFactorFromMidPoint(0.4);
        fader.setDoubleClickReturnValue(true, 1.0);
        fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
        fader.textFromValueFunction = [](double v) {
            return v <= 0.0 ? juce::String("-inf") : juce::String(juce::Decibels::gainToDecibels(v), 1) + " dB";
        };
        fader.setValue(value, juce::dontSendNotification);
        fader.onValueChange = std::move(onChange);
    }

    static void setUpKnob(juce::Slider& knob, double min, double max, double value, double reset,
                          std::function<void()> onChange) {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setRange(min, max, 0.001);
        knob.setDoubleClickReturnValue(true, reset);
        knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob.setValue(value, juce::dontSendNotification);
        knob.onValueChange = std::move(onChange);
    }

private:
    struct TrackStrip : public juce::Component {
        TrackStrip(CliptoneAudioProcessor& p, int t) :
            processorRef(p), track(t), meter([&p, t]() { return p.takeTrackPeak(t); }) {
            const auto colour = CliptoneAudioProcessor::getTrackColour(track);

            // Track activator: the track number, lit while the track is on.
            activator.setButtonText(juce::String(track + 1));
            activator.setClickingTogglesState(true);
            activator.setToggleState(processorRef.isTrackActive(track), juce::dontSendNotification);
            activator.setColour(juce::TextButton::buttonOnColourId, colour);
            activator.setTooltip("Track activator (mute when off)");
            activator.onClick = [this]() { processorRef.setTrackActive(track, activator.getToggleState()); };
            addAndMakeVisible(activator);

            setUpKnob(delaySend, 0.0, 1.0, processorRef.getTrackDelaySend(track), 0.0,
                      [this]() { processorRef.setTrackDelaySend(track, static_cast<float>(delaySend.getValue())); });
            setUpKnob(reverbSend, 0.0, 1.0, processorRef.getTrackReverbSend(track), 0.0,
                      [this]() { processorRef.setTrackReverbSend(track, static_cast<float>(reverbSend.getValue())); });
            setUpKnob(panKnob, -1.0, 1.0, processorRef.getTrackPan(track), 0.0,
                      [this]() { processorRef.setTrackPan(track, static_cast<float>(panKnob.getValue())); });
            panKnob.setTooltip("Pan (double-tap to centre)");
            for (auto* knob : { &delaySend, &reverbSend, &panKnob }) {
                addAndMakeVisible(*knob);
            }
            for (auto [label, text] : { std::make_pair(&delayLabel, "Dly"), std::make_pair(&reverbLabel, "Rev"),
                                        std::make_pair(&panLabel, "Pan") }) {
                label->setText(text, juce::dontSendNotification);
                label->setJustificationType(juce::Justification::centred);
                label->setColour(juce::Label::textColourId, juce::Colour(CliptoneLookAndFeel::Palette::TEXT_MUTED));
                label->setFont(juce::FontOptions(11.0f));
                addAndMakeVisible(*label);
            }

            setUpFader(fader, processorRef.getTrackVolume(track),
                       [this]() { processorRef.setTrackVolume(track, static_cast<float>(fader.getValue())); });
            addAndMakeVisible(fader);
            addAndMakeVisible(meter);

            solo.setButtonText("S");
            solo.setClickingTogglesState(true);
            solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF4FC3F7));
            solo.setTooltip("Solo (exclusive; hold Shift / Cmd for multi-solo)");
            solo.onClick = [this]() {
                const auto mods = juce::ModifierKeys::getCurrentModifiers();
                processorRef.setTrackSolo(track, solo.getToggleState(), mods.isShiftDown() || mods.isCommandDown());
            };
            addAndMakeVisible(solo);

            arm.setButtonText(juce::String::fromUTF8("\xe2\x97\x8f")); // ●
            arm.setClickingTogglesState(true);
            arm.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFFE57373));
            arm.setTooltip("Arm for recording (exclusive; hold Shift / Cmd for more)");
            arm.onClick = [this]() {
                const auto mods = juce::ModifierKeys::getCurrentModifiers();
                processorRef.setTrackArmed(track, arm.getToggleState(), mods.isShiftDown() || mods.isCommandDown());
            };
            addAndMakeVisible(arm);
        }

        void refresh() {
            meter.poll();
            solo.setToggleState(processorRef.isTrackSoloed(track), juce::dontSendNotification);
            arm.setToggleState(processorRef.isTrackArmed(track), juce::dontSendNotification);
            activator.setToggleState(processorRef.isTrackActive(track), juce::dontSendNotification);
            // Automated controls move by themselves during playback.
            if (!fader.isMouseButtonDown()) {
                fader.setValue(processorRef.getTrackVolume(track), juce::dontSendNotification);
            }
            if (!panKnob.isMouseButtonDown()) {
                panKnob.setValue(processorRef.getTrackPan(track), juce::dontSendNotification);
            }
            const bool automated = processorRef.isAutomated(track, AUTOMATE_VOLUME) || processorRef.isAutomated(track, AUTOMATE_PAN);
            fader.setColour(juce::Slider::thumbColourId, automated ? juce::Colour(0xFFE57373) : juce::Colour(CliptoneLookAndFeel::Palette::ACCENT));
        }

        void resized() override {
            auto b = getLocalBounds();
            activator.setBounds(b.removeFromTop(28).reduced(4, 0));
            b.removeFromTop(4);
            auto knobRow = [&b](juce::Slider& knob, juce::Label& label) {
                auto row = b.removeFromTop(52);
                label.setBounds(row.removeFromTop(14));
                knob.setBounds(row.withSizeKeepingCentre(38, 38));
            };
            knobRow(delaySend, delayLabel);
            knobRow(reverbSend, reverbLabel);
            knobRow(panKnob, panLabel);
            auto buttons = b.removeFromBottom(28);
            solo.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(2, 0));
            arm.setBounds(buttons.reduced(2, 0));
            b.removeFromBottom(4);
            meter.setBounds(b.removeFromRight(8).withTrimmedBottom(20));
            fader.setBounds(b);
        }

        CliptoneAudioProcessor& processorRef;
        int track;
        juce::TextButton activator, solo, arm;
        juce::Slider delaySend, reverbSend, panKnob, fader;
        juce::Label delayLabel, reverbLabel, panLabel;
        PeakMeter meter;
    };

    struct ReturnStrip : public juce::Component {
        ReturnStrip(CliptoneAudioProcessor& p, int r, const juce::String& name) : processorRef(p), index(r) {
            label.setText(name, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setColour(juce::Label::textColourId, juce::Colour(CliptoneLookAndFeel::Palette::ACCENT));
            addAndMakeVisible(label);

            prePost.setButtonText(processorRef.isReturnPre(index) ? "Pre" : "Post");
            prePost.setTooltip("Sends to this return tap before (Pre) or after (Post) volume, pan and mute");
            prePost.onClick = [this]() {
                const bool pre = !processorRef.isReturnPre(index);
                processorRef.setReturnPre(index, pre);
                prePost.setButtonText(pre ? "Pre" : "Post");
            };
            addAndMakeVisible(prePost);

            setUpFader(fader, processorRef.getReturnLevel(index),
                       [this]() { processorRef.setReturnLevel(index, static_cast<float>(fader.getValue())); });
            addAndMakeVisible(fader);
        }

        void resized() override {
            auto b = getLocalBounds();
            label.setBounds(b.removeFromTop(28));
            b.removeFromTop(4);
            prePost.setBounds(b.removeFromTop(28).reduced(8, 0));
            b.removeFromTop(8);
            b.removeFromBottom(32);
            fader.setBounds(b);
        }

        CliptoneAudioProcessor& processorRef;
        int index;
        juce::Label label;
        juce::TextButton prePost;
        juce::Slider fader;
    };

    void timerCallback() override {
        for (auto& strip : strips) {
            strip->refresh();
        }
        masterMeter.poll();
    }

    CliptoneAudioProcessor& processorRef;
    std::vector<std::unique_ptr<TrackStrip>> strips;
    std::vector<std::unique_ptr<ReturnStrip>> returnStrips;
    juce::Label masterLabel;
    juce::Slider masterFader;
    PeakMeter masterMeter { [this]() { return processorRef.takeMasterPeak(); } };
};
