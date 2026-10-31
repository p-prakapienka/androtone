#include "MixerView.h"

#include "NatronLookAndFeel.h"
#include "Theme.h"

namespace {
    // Knob caps by job, so a glance down the console finds the sends.
    juce::Colour capFor(const juce::String& id) {
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
        return juce::Colour(0xff9a9fa6); // EQ
    }

    const juce::Colour stripColour { 0xffb7bcc3 };
}

// One machine's channel: knobs in two columns, mute / solo, and its name plate.
class MixerView::Strip : public juce::Component {
public:
    static constexpr int labelHeight = 14;
    static constexpr int footerHeight = 30 + 40 + 16; // name plate, M / S, margins
    static constexpr int cellWidth = 58;

    // Knob columns for a strip `height` tall: as few as keep knobs at least 40 px.
    static int columnsFor(int height) {
        for (int columns = 2; columns < 4; columns++) {
            const int rows = (8 + columns - 1) / columns;
            if ((height - footerHeight) / rows - labelHeight >= 40) {
                return columns;
            }
        }
        return 4;
    }
    static int widthFor(int height) { return columnsFor(height) * cellWidth + 16; }

    Strip(Session& sessionToUse, int machineIndex) :
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
            knob->setValue(parameters->get(index), juce::dontSendNotification);
            knob->getProperties().set(NatronLookAndFeel::capColourProperty, static_cast<juce::int64>(capFor(id).getARGB()));
            knob->setPopupDisplayEnabled(true, true, nullptr); // the value shows while turning
            knob->onValueChange = [this, id = juce::String(id), knob = knob.get()] {
                session.setMixerParameter(machine, id, static_cast<float>(knob->getValue()));
            };
            addAndMakeVisible(*knob);
            auto label = std::make_unique<juce::Label>(id, spec.name.toUpperCase());
            label->setFont(NatronLookAndFeel::labelFont(10.0f));
            label->setJustificationType(juce::Justification::centred);
            label->setColour(juce::Label::textColourId, NatronLookAndFeel::ink);
            addAndMakeVisible(*label);
            knobs.push_back({ std::move(knob), std::move(label) });
        }
        for (auto* button : { &mute, &solo }) {
            button->setClickingTogglesState(true);
            addAndMakeVisible(*button);
        }
        mute.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd0643c));
        solo.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffd9b23a));
        mute.setToggleState(session.getMixerParameter(machine, "mute") >= 0.5f, juce::dontSendNotification);
        solo.setToggleState(session.getMixerParameter(machine, "solo") >= 0.5f, juce::dontSendNotification);
        mute.onClick = [this] { session.setMixerParameter(machine, "mute", mute.getToggleState() ? 1.0f : 0.0f); };
        solo.onClick = [this] { session.setMixerParameter(machine, "solo", solo.getToggleState() ? 1.0f : 0.0f); };
    }

    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds().toFloat().reduced(2.0f);
        NatronLookAndFeel::drawPanel(g, area, stripColour);
        // Fine vertical brushing.
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        for (float x = area.getX() + 4.0f; x < area.getRight() - 4.0f; x += 3.0f) {
            g.drawVerticalLine(juce::roundToInt(x), area.getY() + 4.0f, area.getBottom() - 34.0f);
        }
        // Name plate in the machine's colour.
        const auto plate = area.removeFromBottom(30.0f).reduced(5.0f, 4.0f);
        g.setColour(colour);
        g.fillRoundedRectangle(plate, 3.0f);
        g.setColour(colour.darker(0.5f));
        g.drawRoundedRectangle(plate, 3.0f, 1.0f);
        g.setColour(colour.getPerceivedBrightness() > 0.55f ? NatronLookAndFeel::ink : juce::Colours::white);
        g.setFont(NatronLookAndFeel::labelFont(12.0f));
        g.drawFittedText(name.toUpperCase(), plate.toNearestInt().reduced(3, 0), juce::Justification::centred, 1);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(8, 8);
        auto footer = area.removeFromBottom(footerHeight - 16);
        const int columns = columnsFor(getHeight());
        const int rows = (static_cast<int>(knobs.size()) + columns - 1) / columns;
        const int rowHeight = area.getHeight() / juce::jmax(1, rows);
        const int knobSize = juce::jlimit(32, 60, juce::jmin(rowHeight - labelHeight, cellWidth));
        for (size_t i = 0; i < knobs.size(); i++) {
            const int column = static_cast<int>(i) % columns;
            const int row = static_cast<int>(i) / columns;
            const juce::Rectangle<int> cell(area.getX() + column * cellWidth, area.getY() + row * rowHeight, cellWidth, rowHeight);
            knobs[i].label->setBounds(cell.withHeight(labelHeight));
            knobs[i].knob->setBounds(cell.withTrimmedTop(labelHeight).withSizeKeepingCentre(knobSize, knobSize));
        }
        auto buttons = footer.removeFromTop(36);
        mute.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(3, 2));
        solo.setBounds(buttons.reduced(3, 2));
    }

private:
    struct Knob {
        std::unique_ptr<TouchKnob> knob;
        std::unique_ptr<juce::Label> label;
    };

    Session& session;
    int machine;
    juce::String name;
    juce::Colour colour;
    std::vector<Knob> knobs;
    juce::TextButton mute { "M" };
    juce::TextButton solo { "S" };
};

MixerView::MixerView(Session& sessionToUse) :
    session(sessionToUse) {
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport);
    rebuild();
}

void MixerView::rebuild() {
    channelStrips.clear();
    for (int m = 0; m < session.getNumMachines(); m++) {
        if (session.getMixerParameters(m) != nullptr) {
            channelStrips.push_back(std::make_unique<Strip>(session, m));
            content.addAndMakeVisible(*channelStrips.back());
        }
    }
    master = std::make_unique<ParameterPanel>(
        session.getMasterParameters(), [this](const juce::String& id, float value) { session.setMasterParameter(id, value); },
        "Master");
    master->setLayout("Master", juce::Colour(0xff9aa3ad));
    master->setFitToHeight(true);
    content.addAndMakeVisible(*master);
    resized();
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(NatronLookAndFeel::background);
}

void MixerView::resized() {
    viewport.setBounds(getLocalBounds());
    const int height = juce::jmax(200, viewport.getHeight() - viewport.getScrollBarThickness() - 2);
    int x = 0;
    for (auto& strip : channelStrips) {
        strip->setBounds(x, 0, Strip::widthFor(height), height);
        x = strip->getRight() + 4;
    }
    if (master != nullptr) {
        master->setBounds(x + 4, 0, master->getWidthForHeight(height), height);
        x = master->getRight();
    }
    content.setSize(juce::jmax(x, viewport.getWidth()), height);
}
