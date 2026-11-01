#include "ChannelStripView.h"

#include "NatronLookAndFeel.h"
#include "Theme.h"

namespace {
    // Value rings by job, so a glance down the console finds the sends.
    juce::Colour ringFor(const juce::String& id) {
        if (id == "delaySend") {
            return juce::Colour(0xff7fb8e6);
        }
        if (id == "reverbSend") {
            return juce::Colour(0xffe6b85c);
        }
        if (id == "pan") {
            return juce::Colour(0xff86d19c);
        }
        if (id == "width") {
            return juce::Colour(0xffc6a4e0);
        }
        if (id == "volume") {
            return juce::Colour(0xfff4f4f4);
        }
        return juce::Colour(0xffb8bec6); // EQ
    }
}

ChannelStripView::ChannelStripView(Session& sessionToUse, int machineIndex) :
    session(sessionToUse), machine(machineIndex) {
    const auto& state = session.getSong().machines[static_cast<size_t>(machine)];
    name = state.name;
    colour = Theme::machineColour(state.type);
    const auto* parameters = session.getMixerParameters(machine);
    for (const auto* id : { "eqLow", "eqMid", "eqHigh", "delaySend", "reverbSend", "pan", "width", "volume" }) {
        const int index = parameters->indexOf(id);
        if (index < 0) {
            continue;
        }
        const auto& spec = parameters->getSpec(index);
        auto knob = std::make_unique<TouchKnob>();
        knob->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob->setRange(spec.minValue, spec.maxValue, spec.interval);
        knob->setDoubleClickReturnValue(true, spec.defaultValue);
        knob->setScrollWheelEnabled(false);
        knob->getProperties().set(NatronLookAndFeel::capColourProperty, static_cast<juce::int64>(ringFor(id).getARGB()));
        knob->setPopupDisplayEnabled(true, true, nullptr); // the value shows while turning
        knob->onValueChange = [this, id = juce::String(id), knob = knob.get()] {
            session.setMixerParameter(machine, id, static_cast<float>(knob->getValue()));
        };
        addAndMakeVisible(*knob);
        auto label = std::make_unique<juce::Label>(id, spec.name.toUpperCase());
        label->setFont(NatronLookAndFeel::labelFont(10.0f));
        label->setJustificationType(juce::Justification::centred);
        label->setColour(juce::Label::textColourId, NatronLookAndFeel::textDim);
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*label);
        knobs.push_back({ id, std::move(knob), std::move(label) });
    }
    for (auto* button : { &mute, &solo }) {
        button->setClickingTogglesState(true);
        addAndMakeVisible(*button);
    }
    mute.setColour(juce::TextButton::buttonOnColourId, Theme::muteColour);
    solo.setColour(juce::TextButton::buttonOnColourId, Theme::soloColour);
    mute.onClick = [this] { session.setMixerParameter(machine, "mute", mute.getToggleState() ? 1.0f : 0.0f); };
    solo.onClick = [this] { session.setMixerParameter(machine, "solo", solo.getToggleState() ? 1.0f : 0.0f); };
    refresh();
}

void ChannelStripView::refresh() {
    for (auto& knob : knobs) {
        knob.knob->setValue(session.getMixerParameter(machine, knob.id), juce::dontSendNotification);
    }
    mute.setToggleState(session.getMixerParameter(machine, "mute") >= 0.5f, juce::dontSendNotification);
    solo.setToggleState(session.getMixerParameter(machine, "solo") >= 0.5f, juce::dontSendNotification);
}

void ChannelStripView::paint(juce::Graphics& g) {
    // Edge to edge, so it lines up with the machine panel beside it.
    auto area = getLocalBounds().toFloat();
    NatronLookAndFeel::drawPanel(g, area, NatronLookAndFeel::surface);
    // Its rack position, top right.
    g.setColour(NatronLookAndFeel::textDim.withAlpha(0.6f));
    g.setFont(NatronLookAndFeel::labelFont(16.0f));
    g.drawText(juce::String(machine + 1), getLocalBounds().reduced(8, 5).removeFromTop(18), juce::Justification::topRight);
    // Name key with the machine's colour mark.
    auto plate = area.removeFromBottom(34.0f).reduced(7.0f, 6.0f);
    NatronLookAndFeel::drawKey(g, plate, NatronLookAndFeel::raised, false);
    g.setColour(colour);
    g.fillRoundedRectangle(plate.removeFromLeft(4.0f).reduced(0.0f, 5.0f).translated(4.0f, 0.0f), 1.5f);
    g.setColour(NatronLookAndFeel::text);
    g.setFont(NatronLookAndFeel::labelFont(11.0f));
    g.drawFittedText(name.toUpperCase(), plate.toNearestInt().reduced(6, 0), juce::Justification::centred, 1);
}

void ChannelStripView::resized() {
    auto area = getLocalBounds().reduced(8, 8);
    auto footer = area.removeFromBottom(footerHeight - 16);
    // As many knob columns as the width allows (at least two), cells sharing the width.
    const int columns = juce::jlimit(2, 4, area.getWidth() / 50);
    const int cell = area.getWidth() / columns;
    const int rows = (static_cast<int>(knobs.size()) + columns - 1) / columns;
    const int rowHeight = area.getHeight() / juce::jmax(1, rows);
    const int knobSize = juce::jlimit(24, 56, juce::jmin(rowHeight - labelHeight, cell - 4));
    for (size_t i = 0; i < knobs.size(); i++) {
        const int column = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        const juce::Rectangle<int> bounds(area.getX() + column * cell, area.getY() + row * rowHeight, cell, rowHeight);
        knobs[i].label->setBounds(bounds.withHeight(labelHeight));
        knobs[i].knob->setBounds(bounds.withTrimmedTop(labelHeight).withSizeKeepingCentre(knobSize, knobSize));
    }
    auto buttons = footer.removeFromTop(36);
    mute.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(3, 2));
    solo.setBounds(buttons.reduced(3, 2));
}
