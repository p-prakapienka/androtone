#pragma once

namespace rb
{

// Playing surfaces (PAD-1..PAD-6). All are multi-touch: each finger plays its own note, and
// sliding a finger onto another pad moves the note there.
class PadSurface : public juce::Component,
                   private juce::Timer
{
public:
    PadSurface (Studio&, int trackId);
    ~PadSurface() override;

    void setTrack (int trackId);

    // Which note a point plays (or -1) and the velocity for it.
    virtual int noteAt (juce::Point<float>) const = 0;
    virtual float velocityAt (juce::Point<float> p, int note) const;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Called when a pad is touched (drum kits select the pad, PAD-1).
    std::function<void (int note)> onNoteTouched;

protected:
    const Track* track() const;
    bool isLit (int note) const;

    Studio& studio;
    int trackId;
    std::map<int, int> heldBySource; // touch index -> note

private:
    void timerCallback() override { repaint(); }
    void release (int source);
};

// 4x4 drum pads, pad 0 bottom-left (PAD-1).
class DrumPadGrid : public PadSurface
{
public:
    using PadSurface::PadSurface;
    int noteAt (juce::Point<float>) const override;
    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<float> padBounds (int pad) const;
};

// One large pad for the selected drum: higher = louder (PAD-2).
class SinglePad : public PadSurface
{
public:
    using PadSurface::PadSurface;
    int noteAt (juce::Point<float>) const override;
    float velocityAt (juce::Point<float>, int note) const override;
    void paint (juce::Graphics&) override;
};

// 5x5 melodic pads in Fourths or Sequential layout, In Key aware (PAD-3, PAD-5).
class MelodicPadGrid : public PadSurface
{
public:
    using PadSurface::PadSurface;
    int noteAt (juce::Point<float>) const override;
    void paint (juce::Graphics&) override;

    static int noteForCell (const Set&, const Track&, AppSettings::MelodicLayout, int row, int column);

private:
    juce::Rectangle<float> cellBounds (int row, int column) const;
};

// Two-octave keyboard; with In Key on, a row of equal keys holding scale notes (PAD-4).
class KeysView : public PadSurface
{
public:
    using PadSurface::PadSurface;
    int noteAt (juce::Point<float>) const override;
    void paint (juce::Graphics&) override;

private:
    struct Key { int note; juce::Rectangle<float> bounds; bool black; };
    std::vector<Key> layoutKeys() const;
};

// Base note of the melodic surfaces for a track (set root, octave option).
int melodicBaseNote (const Set&, const Track&);

} // namespace rb
