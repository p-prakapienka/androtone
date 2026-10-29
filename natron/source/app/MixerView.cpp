#include "MixerView.h"

MixerView::MixerView(Session& sessionToUse) :
    session(sessionToUse) {
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);
    rebuild();
}

void MixerView::rebuild() {
    panels.clear();
    for (int m = 0; m < session.getNumMachines(); m++) {
        const auto* parameters = session.getMixerParameters(m);
        if (parameters == nullptr) {
            continue;
        }
        panels.push_back(std::make_unique<ParameterPanel>(
            *parameters, [this, m](const juce::String& id, float value) { session.setMixerParameter(m, id, value); },
            session.getSong().machines[static_cast<size_t>(m)].name));
    }
    panels.push_back(std::make_unique<ParameterPanel>(
        session.getMasterParameters(),
        [this](const juce::String& id, float value) { session.setMasterParameter(id, value); }, "Master"));

    for (auto& panel : panels) {
        content.addAndMakeVisible(*panel);
    }
    resized();
}

void MixerView::resized() {
    viewport.setBounds(getLocalBounds());
    const int width = juce::jmax(1, viewport.getMaximumVisibleWidth());
    int y = 0;
    for (auto& panel : panels) {
        const int height = panel->getHeightForWidth(width);
        panel->setBounds(0, y, width, height);
        y += height + 12;
    }
    content.setSize(width, y);
}
