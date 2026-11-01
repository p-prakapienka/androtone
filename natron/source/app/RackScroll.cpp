#include "RackScroll.h"

#include "NatronLookAndFeel.h"
#include "Theme.h"
#include "core/MachineRegistry.h"

#include <limits>

namespace {
    constexpr int titleBarHeight = 24;
    constexpr int screenGap = 6;
}

// A machine's sound editor, one screen: its whole panel (shrunk to fit), and along the bottom
// mute / solo, its sample or formula editor where it has one, a keyboard that plays it, and
// octave keys.
class RackScroll::SoundScreen : public juce::Component {
public:
    SoundScreen(Session& sessionToUse, int machineIndex, juce::MidiKeyboardState& keyboardState,
                std::function<void(const juce::String&)> errorHandler) :
        session(sessionToUse), machine(machineIndex), showError(std::move(errorHandler)),
        keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {
        const auto& state = session.getSong().machines[static_cast<size_t>(machine)];
        panel = std::make_unique<ParameterPanel>(
            *session.getParameters(machine),
            [this](const juce::String& id, float value) { session.setParameter(machine, id, value); },
            juce::String(machine + 1) + "   " + state.name + (state.name == state.type ? juce::String() : "  -  " + state.type));
        panel->setAccentColour(Theme::machineColour(state.type));
        panel->setLayout(state.type, Theme::panelColour(state.type));
        addAndMakeVisible(*panel);

        for (auto* button : { &mute, &solo }) {
            button->setClickingTogglesState(true);
            addAndMakeVisible(*button);
        }
        mute.setColour(juce::TextButton::buttonOnColourId, Theme::muteColour);
        solo.setColour(juce::TextButton::buttonOnColourId, Theme::soloColour);
        mute.setToggleState(session.getMixerParameter(machine, "mute") >= 0.5f, juce::dontSendNotification);
        solo.setToggleState(session.getMixerParameter(machine, "solo") >= 0.5f, juce::dontSendNotification);
        mute.onClick = [this] { session.setMixerParameter(machine, "mute", mute.getToggleState() ? 1.0f : 0.0f); };
        solo.onClick = [this] { session.setMixerParameter(machine, "solo", solo.getToggleState() ? 1.0f : 0.0f); };

        if (MachineExtrasPanel::hasExtras(state.type)) {
            extrasButton.setButtonText(state.type == "Bytebeat" ? "Formula" : "Sample");
            extrasButton.onClick = [this] {
                auto extras = std::make_unique<MachineExtrasPanel>(session, machine, showError);
                extras->setSize(juce::jmin(640, getWidth() - 20), extras->getIdealHeight());
                juce::CallOutBox::launchAsynchronously(std::move(extras), extrasButton.getScreenBounds(), nullptr);
            };
            addAndMakeVisible(extrasButton);
        }

        keyboard.setScrollButtonsVisible(false); // our octave keys move it instead (see shiftOctave)
        keyboard.setAvailableRange(lowestKey, lowestKey + keysShown);
        addAndMakeVisible(keyboard);
        octaveUp.onClick = [this] { shiftOctave(12); };
        octaveDown.onClick = [this] { shiftOctave(-12); };
        addAndMakeVisible(octaveUp);
        addAndMakeVisible(octaveDown);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(NatronLookAndFeel::background);
    }

    void resized() override {
        auto area = getLocalBounds();
        auto keys = area.removeFromBottom(juce::jlimit(70, 130, getHeight() / 3)).reduced(0, 3);
        panel->setBounds(area);

        auto left = keys.removeFromLeft(juce::jmin(150, keys.getWidth() / 5));
        auto buttons = left.removeFromTop(left.getHeight() / 2);
        mute.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(3));
        solo.setBounds(buttons.reduced(3));
        extrasButton.setBounds(left.reduced(3));
        auto octave = keys.removeFromRight(48);
        octaveUp.setBounds(octave.removeFromTop(octave.getHeight() / 2).reduced(2));
        octaveDown.setBounds(octave.reduced(2));
        keyboard.setBounds(keys.reduced(2, 0));
        // About an octave and a half across, keys sized for fingers.
        keyboard.setKeyWidth(static_cast<float>(keyboard.getWidth()) / 11.0f); // 11 white keys: C to F an octave up
    }

private:
    Session& session;
    int machine;
    std::function<void(const juce::String&)> showError;
    std::unique_ptr<ParameterPanel> panel;
    juce::TextButton mute { "M" }, solo { "S" }, extrasButton;
    juce::MidiKeyboardComponent keyboard;
    IconButton octaveUp { "Octave up", NatronLookAndFeel::Icon::octaveUp };
    IconButton octaveDown { "Octave down", NatronLookAndFeel::Icon::octaveDown };

    // Without scroll buttons the keyboard starts at the bottom of its range, so the range is what
    // the octave keys move: 11 white keys from a C.
    static constexpr int keysShown = 17;
    int lowestKey = 48; // around middle C

    void shiftOctave(int semitones) {
        lowestKey = juce::jlimit(12, 96, lowestKey + semitones);
        keyboard.setAvailableRange(lowestKey, lowestKey + keysShown);
    }
};

// A machine's pattern editor, one screen: its toolbar of pattern keys over the roll.
class RackScroll::PatternScreen : public juce::Component {
public:
    PatternScreen(Session& session, const Engine& engine, int machine) : editor(session, engine) {
        editor.setMachine(machine);
        addAndMakeVisible(editor);
    }

    void resized() override { editor.setBounds(getLocalBounds()); }

private:
    PatternEditorView editor;
};

// FX, Mixer and Master screens: a title bar over the content.
class RackScroll::TitledScreen : public juce::Component {
public:
    TitledScreen(juce::String titleText, std::unique_ptr<juce::Component> body) :
        title(std::move(titleText)), content(std::move(body)) {
        addAndMakeVisible(*content);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(NatronLookAndFeel::background);
        auto bar = getLocalBounds().removeFromTop(titleBarHeight).toFloat().reduced(6.0f, 6.0f);
        g.setColour(NatronLookAndFeel::accent);
        g.fillRoundedRectangle(bar.removeFromLeft(3.0f), 1.5f);
        bar.removeFromLeft(7.0f);
        g.setColour(NatronLookAndFeel::text);
        g.setFont(NatronLookAndFeel::labelFont(12.0f));
        g.drawText(title.toUpperCase(), bar, juce::Justification::centredLeft);
    }

    void resized() override { content->setBounds(getLocalBounds().withTrimmedTop(titleBarHeight)); }

private:
    juce::String title;
    std::unique_ptr<juce::Component> content;
};

// The scroll grip: notches down the left edge and a marker for where the view is. Dragging it moves
// the whole page proportionally.
class RackScroll::Grip : public juce::Component {
public:
    explicit Grip(RackScroll& ownerToUse) : owner(ownerToUse) {}

    void paint(juce::Graphics& g) override {
        g.fillAll(NatronLookAndFeel::background);
        g.setColour(NatronLookAndFeel::line);
        for (int y = 8; y < getHeight() - 4; y += 12) {
            g.fillRoundedRectangle(8.0f, static_cast<float>(y), static_cast<float>(getWidth() - 16), 2.0f, 1.0f);
        }
        const auto& viewport = owner.viewport;
        const int total = juce::jmax(1, owner.content.getHeight());
        const float top = static_cast<float>(viewport.getViewPositionY()) / static_cast<float>(total) * static_cast<float>(getHeight());
        const float size = static_cast<float>(viewport.getHeight()) / static_cast<float>(total) * static_cast<float>(getHeight());
        g.setColour(NatronLookAndFeel::accent.withAlpha(0.22f));
        g.fillRoundedRectangle(3.0f, top, static_cast<float>(getWidth()) - 6.0f, juce::jmax(10.0f, size), 4.0f);
        g.setColour(NatronLookAndFeel::accent);
        g.fillRoundedRectangle(3.0f, top, 2.0f, juce::jmax(10.0f, size), 1.0f);
    }

    void mouseDown(const juce::MouseEvent& event) override { scrollTo(event.position.y); }
    void mouseDrag(const juce::MouseEvent& event) override { scrollTo(event.position.y); }
    void mouseUp(const juce::MouseEvent&) override { owner.settleOnNearestScreen(); }

private:
    RackScroll& owner;

    void scrollTo(float y) {
        const double fraction = juce::jlimit(0.0, 1.0, static_cast<double>(y) / juce::jmax(1, getHeight()));
        const int target = juce::roundToInt(fraction * owner.content.getHeight() - owner.viewport.getHeight() / 2.0);
        owner.viewport.setViewPosition(0, juce::jmax(0, target));
    }
};

RackScroll::RackScroll(Session& sessionToUse, const Engine& engineToShow, juce::MidiKeyboardState& keyboardState,
                       std::function<void(const juce::String&)> errorHandler) :
    session(sessionToUse), engine(engineToShow), keyboard(keyboardState), showError(std::move(errorHandler)) {
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    viewport.onScroll = [this] {
        updateMachineInView();
        lastScrollTime = juce::Time::getMillisecondCounterHiRes();
        if (grip != nullptr) {
            grip->repaint();
        }
    };
    viewport.setScrollBarsShown(false, false); // the grip shows where we are
    addAndMakeVisible(viewport);
    grip = std::make_unique<Grip>(*this);
    addAndMakeVisible(*grip);
    startTimerHz(10);
    addButton.onClick = [this] { showAddMenu(); };
    rebuild();
}

RackScroll::~RackScroll() {
    stopTimer();
    screens.clear(); // before `content` goes
}

void RackScroll::rebuild() {
    const auto position = viewport.getViewPosition();
    timeline = nullptr;
    screens.clear();
    screenMachine.clear();

    const int numMachines = session.getNumMachines();
    for (int m = 0; m < numMachines; m++) {
        screens.push_back(std::make_unique<SoundScreen>(session, m, keyboard, showError));
        screenMachine.push_back(m);
        screens.push_back(std::make_unique<PatternScreen>(session, engine, m));
        screenMachine.push_back(m);
    }

    const auto range = [](int first, int count) { return juce::String(first + 1) + "-" + juce::String(first + count); };
    for (int first = 0; first < juce::jmax(1, numMachines); first += FxRackView::machinesPerScreen) {
        screens.push_back(std::make_unique<TitledScreen>("FX " + range(first, FxRackView::machinesPerScreen),
                                                         std::make_unique<FxRackView>(session, first)));
        screenMachine.push_back(-1);
    }
    for (int first = 0; first < juce::jmax(1, numMachines); first += MixerView::stripsPerScreen) {
        screens.push_back(std::make_unique<TitledScreen>("Mixer " + range(first, MixerView::stripsPerScreen),
                                                         std::make_unique<MixerView>(session, first, false)));
        screenMachine.push_back(-1);
    }
    screens.push_back(std::make_unique<TitledScreen>("Master", std::make_unique<MixerView>(session, 0, true)));
    screenMachine.push_back(-1);
    auto song = std::make_unique<SongView>(session, engine);
    timeline = song.get();
    screens.push_back(std::move(song));
    screenMachine.push_back(-1);

    for (auto& screen : screens) {
        content.addAndMakeVisible(*screen);
    }
    content.addChildComponent(addButton);
    addButton.setVisible(numMachines < Engine::maxMachines);
    layoutScreens();
    viewport.setViewPosition(position);
    machineInView = -1;
    updateMachineInView();
}

int RackScroll::screenHeight() const {
    return juce::jmax(240, viewport.getHeight());
}

void RackScroll::layoutScreens() {
    const int width = juce::jmax(1, viewport.getWidth());
    const int height = screenHeight();
    int y = 0;
    if (session.getNumMachines() == 0 && addButton.isVisible()) {
        addButton.setBounds(0, 0, juce::jmin(width, 320), 48); // no machines: the add key leads the page
        y = 48 + screenGap;
    }
    for (size_t i = 0; i < screens.size(); i++) {
        screens[i]->setBounds(0, y, width, height);
        y += height;
        // "Add machine" sits after the last machine's pattern editor.
        const bool lastMachineScreen = screenMachine[i] >= 0 && (i + 1 == screens.size() || screenMachine[i + 1] < 0);
        if (lastMachineScreen && addButton.isVisible()) {
            addButton.setBounds(0, y + screenGap, juce::jmin(width, 320), 48);
            y += 48 + 2 * screenGap;
        }
    }
    content.setSize(width, y);
}

void RackScroll::timerCallback() {
    // Settle on a screen once a swipe has come to rest and no finger is down.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (!settling && now - lastScrollTime > 250.0 && now - lastScrollTime < 1000.0
        && juce::Desktop::getInstance().getNumDraggingMouseSources() == 0) {
        settleOnNearestScreen();
    }
}

void RackScroll::settleOnNearestScreen() {
    const int position = viewport.getViewPositionY();
    int best = position;
    int bestDistance = std::numeric_limits<int>::max();
    for (auto& screen : screens) {
        const int distance = std::abs(screen->getY() - position);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = screen->getY();
        }
    }
    if (best != position && bestDistance < screenHeight() / 2) {
        const juce::ScopedValueSetter<bool> guard(settling, true);
        viewport.setViewPosition(0, best);
    }
    lastScrollTime = 0.0;
}

void RackScroll::updateMachineInView() {
    // The machine owning the screen under the top third of the view.
    const int probe = viewport.getViewPositionY() + viewport.getHeight() / 3;
    int machine = -1;
    for (size_t i = 0; i < screens.size(); i++) {
        if (screens[i]->getBounds().contains(0, probe)) {
            machine = screenMachine[i];
            break;
        }
    }
    if (machine >= 0 && machine != machineInView) {
        machineInView = machine;
        if (onMachineInView) {
            onMachineInView(machine);
        }
    }
}

void RackScroll::showMachine(int machine) {
    for (size_t i = 0; i < screens.size(); i++) {
        if (screenMachine[i] == machine) {
            viewport.setViewPosition(0, screens[i]->getY());
            return;
        }
    }
}

void RackScroll::toggleTimeline() {
    if (timeline == nullptr) {
        return;
    }
    if (isTimelineInView()) {
        viewport.setViewPosition(0, positionBeforeTimeline);
    } else {
        positionBeforeTimeline = viewport.getViewPositionY();
        viewport.setViewPosition(0, timeline->getY());
    }
}

bool RackScroll::isTimelineInView() const {
    return timeline != nullptr && std::abs(viewport.getViewPositionY() - timeline->getY()) < viewport.getHeight() / 2;
}

void RackScroll::repaintPlaying() {} // the pattern editors repaint themselves while playing

void RackScroll::showAddMenu() {
    juce::PopupMenu menu;
    const auto types = MachineRegistry::getTypeNames();
    for (int i = 0; i < types.size(); i++) {
        if (types[i] != "TestTone") { // the placeholder machine stays out of the menu
            menu.addItem(i + 1, types[i]);
        }
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton), [this, types](int result) {
        if (result > 0) {
            const int added = session.addMachine(types[result - 1]);
            if (added >= 0 && onRackChanged) {
                onRackChanged();
                showMachine(added);
            }
        }
    });
}

void RackScroll::paint(juce::Graphics& g) {
    g.fillAll(NatronLookAndFeel::background);
}

void RackScroll::resized() {
    auto area = getLocalBounds();
    grip->setBounds(area.removeFromLeft(gripWidth));
    viewport.setBounds(area);
    layoutScreens();
}
