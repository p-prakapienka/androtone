#pragma once

namespace rb
{

// Clip preview and MIDI editor of the instrument view (EDIT-1..EDIT-12, CAP-2).
// Shows the clip's notes, pending (uncommitted) notes and automation; with no clip in the
// slot it offers fixed-length clip creation.
// STUB: preview and fixed-length creation only, until the full editor lands.
class ClipEditor : public juce::Component,
                   private Studio::Listener,
                   private juce::Timer
{
public:
    ClipEditor (Studio&, Navigator&, int trackId);
    ~ClipEditor() override;

    void setSlot (int slot);
    int getSlot() const { return slot; }

    bool isExpanded() const { return expanded; }
    void setExpanded (bool);
    std::function<void (bool expanded)> onExpandedChanged;

    // Opens the quantization settings (INS-5 long press).
    void showQuantizeSettings() {}

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void studioChanged() override { repaint(); }
    void captureChanged() override { repaint(); }
    void timerCallback() override { repaint(); }
    const Clip* clip() const;

    Studio& studio;
    Navigator& nav;
    int trackId;
    int slot = -1;
    bool expanded = false;
};

} // namespace rb
