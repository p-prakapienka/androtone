#include "MixerView.h"

#include "NatronLookAndFeel.h"
#include "Theme.h"

MixerView::MixerView(Session& sessionToUse, int firstMachine, bool masterScreen) :
    session(sessionToUse), first(firstMachine), showsMaster(masterScreen) {
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(false, true);
    addAndMakeVisible(viewport);
    rebuild();
}

MixerView::~MixerView() = default;

void MixerView::rebuild() {
    channelStrips.clear();
    master.reset();
    if (showsMaster) {
        master = std::make_unique<ParameterPanel>(
            session.getMasterParameters(), [this](const juce::String& id, float value) { session.setMasterParameter(id, value); });
        master->setLayout("Master", NatronLookAndFeel::surface);
        master->setAccentColour(NatronLookAndFeel::accent);
        content.addAndMakeVisible(*master);
    } else {
        for (int m = first; m < juce::jmin(first + stripsPerScreen, session.getNumMachines()); m++) {
            if (session.getMixerParameters(m) != nullptr) {
                channelStrips.push_back(std::make_unique<ChannelStripView>(session, m));
                content.addAndMakeVisible(*channelStrips.back());
            }
        }
    }
    resized();
}

void MixerView::paint(juce::Graphics& g) {
    g.fillAll(NatronLookAndFeel::background);
    if (showsMaster) {
        return;
    }
    // Positions without a machine are empty strips with a blank name plate.
    const int width = getWidth() / stripsPerScreen;
    for (int c = static_cast<int>(channelStrips.size()); c < stripsPerScreen; c++) {
        auto strip = juce::Rectangle<int>(c * width, 0, width - 4, getHeight()).toFloat().reduced(2.0f);
        g.setColour(NatronLookAndFeel::surface.darker(0.25f));
        g.fillRoundedRectangle(strip, 8.0f);
        g.setColour(NatronLookAndFeel::line.withAlpha(0.6f));
        g.drawRoundedRectangle(strip.reduced(0.5f), 8.0f, 1.0f);
    }
}

void MixerView::resized() {
    viewport.setBounds(getLocalBounds());
    if (master != nullptr) { // the master section shrinks to fit its screen
        viewport.setScrollBarsShown(false, false);
        master->setBounds(getLocalBounds());
        content.setSize(getWidth(), getHeight());
        return;
    }
    // Eight strips share the width (scrolling sideways only when they can't fit).
    viewport.setScrollBarsShown(false, true);
    const int height = juce::jmax(220, getHeight() - 2);
    const int width = juce::jmax(ChannelStripView::minimumWidth, getWidth() / stripsPerScreen) - 4;
    int x = 0;
    for (auto& strip : channelStrips) {
        strip->setBounds(x, 0, width, height);
        x = strip->getRight() + 4;
    }
    content.setSize(juce::jmax(x, viewport.getWidth()), height);
    if (content.getWidth() > viewport.getWidth()) {
        content.setSize(content.getWidth(), height - viewport.getScrollBarThickness());
    }
}
