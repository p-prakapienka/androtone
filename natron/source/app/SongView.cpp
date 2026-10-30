#include "SongView.h"

#include "core/Arrangement.h"

namespace {
    const juce::Colour background { 0xff1d1f21 };
    const juce::Colour rowColour { 0xff282a2e };
    const juce::Colour gridLine { 0xff34373b };
    const juce::Colour blockColour { 0xff5f819d };
    const juce::Colour loopColour { 0xffde935f };
    const juce::Colour text { 0xffc5c8c6 };

    constexpr int nameWidth = 110;
    constexpr int rulerHeight = 28;
    constexpr int rowHeight = 48;
    constexpr int visibleBars = 64; // the timeline always shows at least this many
}

class SongView::Timeline : public juce::Component {
public:
    explicit Timeline(SongView& ownerToUse) :
        owner(ownerToUse) {}

    float barWidth = 44.0f;

    int numBars() const { return juce::jmax(visibleBars, owner.session.getSong().endBar() + 16); }
    int idealWidth() const { return nameWidth + juce::roundToInt(numBars() * barWidth); }
    int idealHeight() const { return rulerHeight + owner.session.getNumMachines() * rowHeight; }

    int barAtX(float x) const { return juce::jmax(0, static_cast<int>((x - nameWidth) / barWidth)); }
    float xForBar(double bar) const { return nameWidth + static_cast<float>(bar) * barWidth; }
    int rowAtY(float y) const { return y < rulerHeight ? -1 : static_cast<int>((y - rulerHeight) / rowHeight); }

    juce::Rectangle<float> blockBounds(int row, const SongBlock& block) const {
        return { xForBar(block.startBar) + 1.0f, static_cast<float>(rulerHeight + row * rowHeight) + 4.0f,
                 block.lengthBars * barWidth - 2.0f, static_cast<float>(rowHeight) - 8.0f };
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(background);
        const auto& song = owner.session.getSong();
        const int bars = numBars();
        const float bottom = static_cast<float>(idealHeight());

        // Ruler and loop region.
        if (song.loopEndBar > song.loopStartBar) {
            const juce::Rectangle<float> loop(xForBar(song.loopStartBar), 0.0f,
                                              (song.loopEndBar - song.loopStartBar) * barWidth, rulerHeight - 4.0f);
            g.setColour(loopColour.withAlpha(song.loopEnabled ? 0.8f : 0.25f));
            g.fillRoundedRectangle(loop, 3.0f);
        }
        g.setFont(juce::FontOptions(12.0f));
        for (int bar = 0; bar <= bars; bar++) {
            const float x = xForBar(bar);
            g.setColour(bar % 4 == 0 ? gridLine.brighter(0.3f) : gridLine);
            g.drawVerticalLine(juce::roundToInt(x), rulerHeight * 0.5f, bottom);
            if (bar % 4 == 0 && bar < bars) {
                g.setColour(text);
                g.drawText(juce::String(bar + 1), juce::Rectangle<float>(x + 3.0f, 0.0f, 40.0f, rulerHeight - 4.0f),
                           juce::Justification::centredLeft);
            }
        }

        // Rows and blocks.
        for (int row = 0; row < owner.session.getNumMachines(); row++) {
            const auto& machine = song.machines[static_cast<size_t>(row)];
            const float y = static_cast<float>(rulerHeight + row * rowHeight);
            g.setColour(rowColour);
            g.fillRect(juce::Rectangle<float>(0.0f, y + 1.0f, nameWidth - 2.0f, rowHeight - 2.0f));
            g.setColour(text);
            g.setFont(juce::FontOptions(14.0f));
            g.drawText(machine.name, juce::Rectangle<float>(8.0f, y, nameWidth - 12.0f, rowHeight),
                       juce::Justification::centredLeft);

            for (const auto& block : machine.blocks) {
                const auto bounds = blockBounds(row, block);
                g.setColour(blockColour);
                g.fillRoundedRectangle(bounds, 4.0f);
                g.setColour(background.withAlpha(0.5f));
                g.fillRect(bounds.withLeft(bounds.getRight() - 5.0f).reduced(0.0f, 8.0f)); // resize handle
                g.setColour(juce::Colours::white);
                g.setFont(juce::FontOptions(12.0f));
                g.drawText(PatternBank::nameOf(block.pattern), bounds.reduced(6.0f, 0.0f), juce::Justification::centredLeft);
            }
        }

        if (owner.engine.isPlaying() && owner.engine.isSongMode()) {
            g.setColour(loopColour);
            g.drawVerticalLine(juce::roundToInt(xForBar(owner.engine.getPlayheadBeat() / Transport::beatsPerBar)), 0.0f, bottom);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        mode = Mode::none;
        if (juce::Desktop::getInstance().getNumDraggingMouseSources() > 1) {
            startPinch();
            return;
        }
        const auto position = event.position;
        if (position.x < nameWidth) {
            return;
        }
        const int bar = barAtX(position.x);
        pressBar = bar;

        if (position.y < rulerHeight) {
            mode = Mode::ruler;
            return;
        }
        dragRow = rowAtY(position.y);
        if (dragRow < 0 || dragRow >= owner.session.getNumMachines()) {
            return;
        }
        working = owner.session.getBlocks(dragRow);
        blockIndex = Arrangement::indexAt(working, bar);
        if (blockIndex >= 0 && event.mods.isPopupMenu()) {
            Arrangement::remove(working, blockIndex);
            commit();
            return;
        }
        if (blockIndex >= 0) {
            const auto bounds = blockBounds(dragRow, working[static_cast<size_t>(blockIndex)]);
            mode = position.x > bounds.getRight() - juce::jmax(10.0f, barWidth * 0.3f) ? Mode::resizeBlock : Mode::moveBlock;
            grabOffset = bar - working[static_cast<size_t>(blockIndex)].startBar;
            original = working;
        } else {
            mode = Mode::place;
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (pinching || juce::Desktop::getInstance().getNumDraggingMouseSources() > 1) {
            updatePinch();
            return;
        }
        if (event.getDistanceFromDragStart() < 6) {
            return;
        }
        dragged = true;
        const int bar = barAtX(event.position.x);
        if (mode == Mode::ruler) {
            const int start = juce::jmin(pressBar, bar);
            const int end = juce::jmax(pressBar, bar) + 1;
            owner.session.setLoop(true, start, end);
            repaint();
        } else if (mode == Mode::moveBlock || mode == Mode::resizeBlock) {
            working = original; // re-apply from the pre-drag row, so a drag never eats neighbours for good
            const auto& block = working[static_cast<size_t>(blockIndex)];
            if (mode == Mode::moveBlock) {
                Arrangement::move(working, blockIndex, juce::jmax(0, bar - grabOffset));
            } else {
                Arrangement::resize(working, blockIndex, juce::jmax(1, bar - block.startBar + 1));
            }
            commit();
        }
    }

    void mouseUp(const juce::MouseEvent& event) override {
        if (pinching) {
            pinching = juce::Desktop::getInstance().getNumDraggingMouseSources() > 1;
            return;
        }
        if (!dragged) {
            const auto& song = owner.session.getSong();
            if (mode == Mode::ruler) {
                owner.session.setLoop(!song.loopEnabled, song.loopStartBar, song.loopEndBar);
                repaint();
            } else if (mode == Mode::place) {
                const auto& machine = song.machines[static_cast<size_t>(dragRow)];
                const int pattern = machine.selectedPattern;
                Arrangement::place(working, { pressBar, machine.patterns[static_cast<size_t>(pattern)].bars, pattern });
                commit();
            } else if ((mode == Mode::moveBlock || mode == Mode::resizeBlock) && event.getLengthOfMousePress() >= 500) {
                Arrangement::remove(working, blockIndex); // long press
                commit();
            }
        }
        dragged = false;
        mode = Mode::none;
    }

    void mouseMagnify(const juce::MouseEvent&, float scale) override { zoom(scale); }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        if (event.mods.isCtrlDown() || event.mods.isCommandDown()) {
            zoom(1.0f + wheel.deltaY);
        } else {
            Component::mouseWheelMove(event, wheel);
        }
    }

private:
    enum class Mode { none, ruler, place, moveBlock, resizeBlock };

    SongView& owner;
    Mode mode = Mode::none;
    BlockList working;
    BlockList original;
    int dragRow = -1;
    int blockIndex = -1;
    int pressBar = 0;
    int grabOffset = 0;
    bool dragged = false;
    bool pinching = false;
    float pinchStartDistance = 1.0f;
    float pinchStartWidth = 44.0f;

    void commit() {
        owner.session.setBlocks(dragRow, working);
        owner.resized();
        repaint();
    }

    void zoom(float factor) {
        barWidth = juce::jlimit(12.0f, 160.0f, barWidth * factor);
        owner.resized();
    }

    float fingerDistance() const {
        juce::Array<juce::Point<float>> points;
        for (auto& source : juce::Desktop::getInstance().getMouseSources()) {
            if (source.isDragging()) {
                points.add(source.getScreenPosition());
            }
        }
        return points.size() >= 2 ? std::abs(points[0].x - points[1].x) : 0.0f;
    }

    void startPinch() {
        pinching = true;
        mode = Mode::none;
        pinchStartDistance = juce::jmax(1.0f, fingerDistance());
        pinchStartWidth = barWidth;
    }

    void updatePinch() {
        if (!pinching) {
            startPinch();
        }
        if (const float distance = fingerDistance(); distance > 0.0f) {
            barWidth = juce::jlimit(12.0f, 160.0f, pinchStartWidth * distance / pinchStartDistance);
            owner.resized();
        }
    }
};

SongView::SongView(Session& sessionToUse, const Engine& engineToShow) :
    session(sessionToUse), engine(engineToShow), automation(sessionToUse, AutomationEditor::Mode::song) {
    swingLabel.setText("Swing", juce::dontSendNotification);
    addAndMakeVisible(swingLabel);
    swing.setRange(Song::minSwing * 100.0, Song::maxSwing * 100.0, 1.0);
    swing.setTextValueSuffix(" %");
    swing.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 28);
    swing.onValueChange = [this] { session.setSwing(static_cast<float>(swing.getValue() / 100.0)); };
    addAndMakeVisible(swing);
    addAndMakeVisible(automation);
    timeline = std::make_unique<Timeline>(*this);
    viewport.setViewedComponent(timeline.get(), false);
    addAndMakeVisible(viewport);
    startTimerHz(30);
}

void SongView::refresh() {
    swing.setValue(session.getSwing() * 100.0, juce::dontSendNotification);
    automation.refresh();
    resized();
    timeline->repaint();
}

void SongView::resized() {
    auto area = getLocalBounds();
    auto header = area.removeFromTop(34);
    swingLabel.setBounds(header.removeFromLeft(60));
    swing.setBounds(header.removeFromLeft(300));
    automation.setBounds(area.removeFromBottom(juce::jmin(150, area.getHeight() / 3)));
    area.removeFromBottom(4);
    viewport.setBounds(area);
    timeline->setSize(juce::jmax(viewport.getMaximumVisibleWidth(), timeline->idealWidth()),
                      juce::jmax(viewport.getMaximumVisibleHeight(), timeline->idealHeight()));
}

void SongView::timerCallback() {
    if (engine.isPlaying() && isShowing()) {
        timeline->repaint();
    }
}
