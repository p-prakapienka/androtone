#pragma once

namespace rb
{

// Sample tab body (SMP-2..SMP-8): waveform of the selected pad's / the sampler's sample with
// its start point and gain, Load Sample, Record (microphone or resampled output) and the
// sample options.
//
// Gestures on the waveform (SMP-4): drag horizontally to move the start point, vertically to
// change the gain (one undo step per drag); pinch (or trackpad magnify / ctrl+wheel) zooms.
class SampleView : public juce::Component,
                   private Studio::Listener,
                   private juce::Timer
{
public:
    SampleView (Studio&, Navigator&, int trackId);
    ~SampleView() override;

    enum class Source { microphone, resample };

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isRecording() const { return recording; }

private:
    void studioChanged() override;
    void timerCallback() override;

    const Track* track() const;
    int targetPad() const;                  // selected pad (drums) or -1 (sampler)
    juce::String currentRef() const;
    SamplePtr currentSample();
    ParamTarget paramTarget (const juce::String& id) const; // paramIndex < 0 when missing
    float paramValue (const juce::String& id, float fallback) const;
    void setParam (const juce::String& id, float value, bool recordAutomation);

    juce::Rectangle<int> waveArea() const;
    void setZoom (double newZoom, double anchorFraction, float anchorX);
    void clampView();

    // Recording (SMP-3, SMP-5, SMP-8, GLB-3).
    void toggleRecording();
    void beginTake();
    void finishTake();
    void updateRecordButton();

    void loadSample();
    void showOptions();
    void saveSampleToLibrary();
    void flash (const juce::String& message);

    Studio& studio;
    Navigator& nav;
    int trackId;

    WaveformPeaks peaks;
    juce::String shownRef;

    // View: zoom >= 1, viewStart = first visible fraction of the sample.
    double zoom = 1.0, viewStart = 0.0;

    // Gestures.
    enum class Drag { none, undecided, start, gain, pinch };
    Drag drag = Drag::none;
    std::map<int, juce::Point<float>> touches;
    juce::Point<float> downPosition;
    float dragStartFraction = 0.0f, dragStartGainNormalised = 0.0f;
    float pinchStartDistance = 1.0f;
    double pinchStartZoom = 1.0, pinchAnchor = 0.0;
    bool gestureOpen = false;

    // Recording.
    static Source lastSource;
    Source source = Source::microphone;
    bool recording = false, waitingForPermission = false;
    int recordPad = -1;
    double recordStartMs = 0.0;
    bool recordPadWasSounding = false;
    bool handlingChange = false;

    juce::String flashMessage;
    double flashUntilMs = 0.0;

    IconButton loadButton { Icon::folder, "Load" }, recordButton { Icon::mic, "Mic" }, optionsButton { Icon::dots, "More" };

    struct LongPressListener : public juce::MouseListener
    {
        LongPress longPress;
        void mouseDown (const juce::MouseEvent& e) override { longPress.mouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { longPress.mouseDrag (e); }
        void mouseUp (const juce::MouseEvent&) override { longPress.mouseUp(); }
    };
    LongPressListener recordLongPress;
};

} // namespace rb
