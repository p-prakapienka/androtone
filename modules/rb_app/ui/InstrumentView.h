#pragma once

namespace rb
{

// One track's instrument view (INS, PAD, CAP): device section, clip preview/editor, capture
// controls, performance options and the pads.
class InstrumentView : public juce::Component,
                       private Studio::Listener,
                       private juce::Timer
{
public:
    InstrumentView (Studio&, Navigator&, int trackId, int slot);
    ~InstrumentView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    int getTrackId() const { return trackId; }
    int getEditSlot() const { return editSlot; }

    // Quantize settings shared by the action bar button and the editor (EDIT-10, INS-5).
    static double quantizeGrid;
    static float quantizeStrength;
    static bool quantizeTriplets;

private:
    class CaptureStrip;
    class PerformanceStrip;

    void studioChanged() override;
    void captureChanged() override;
    void timerCallback() override;

    void refreshPads();
    void updateHeader();
    void updateSlotFollow();
    void duplicateClip();
    void quantizeAll();
    void showQuantizeMenu();

    Studio& studio;
    Navigator& nav;
    int trackId;
    int editSlot;
    bool followPlaying;

    HeaderBar header;
    DevicePanel devicePanel;
    ClipEditor clipEditor;
    std::unique_ptr<CaptureStrip> captureStrip;
    std::unique_ptr<PerformanceStrip> performanceStrip;
    std::unique_ptr<PadSurface> pads;
    juce::String padKind;

    IconButton undoButton { Icon::undo }, redoButton { Icon::redo }, perfButton { Icon::repeat },
               duplicateButton { Icon::duplicate }, quantizeButton { Icon::quantize }, playButton { Icon::play };
    LongPress quantizeLongPress;
    bool showPerformance = false;
};

} // namespace rb
