#pragma once

#include <cmath>
#include <cstdint>

// Maps musical time (beats) to sample time and back. Positions count samples since playback
// started. A tempo change re-anchors the mapping at the sample where it takes effect, so the
// sample an event lands on depends only on the tempo history — never on how the host splits audio
// into blocks. Owned by the audio thread.
class Transport {
public:
    static constexpr int beatsPerBar = 4;

    void prepare(double newSampleRate) {
        sampleRate = newSampleRate;
        rewind();
    }

    // Back to beat 0 at sample 0, keeping the tempo.
    void rewind() {
        position = 0;
        anchorSample = 0;
        anchorBeat = 0.0;
    }

    // From `sample` on, time continues from `beat` (a loop jump, or starting mid-song).
    void jump(int64_t sample, double beat) {
        anchorSample = sample;
        anchorBeat = beat;
    }

    // Takes effect from the current position onwards.
    void setTempo(double bpm) {
        anchorBeat = beatAt(position);
        anchorSample = position;
        tempo = bpm;
    }

    double getTempo() const { return tempo; }
    double getSampleRate() const { return sampleRate; }
    double samplesPerBeat() const { return sampleRate * 60.0 / tempo; }

    int64_t getPosition() const { return position; }
    void advance(int numSamples) { position += numSamples; }

    double beatAt(int64_t sample) const {
        return anchorBeat + static_cast<double>(sample - anchorSample) / samplesPerBeat();
    }

    // The first sample at or after `beat`. The small tolerance keeps beats that fall exactly on a
    // sample (the common case: 1/16 steps at most tempos) from rounding up to the next one.
    int64_t sampleAt(double beat) const {
        return anchorSample + static_cast<int64_t>(std::ceil((beat - anchorBeat) * samplesPerBeat() - 1.0e-6));
    }

private:
    double sampleRate = 48000.0;
    double tempo = 120.0;
    int64_t position = 0;
    int64_t anchorSample = 0;
    double anchorBeat = 0.0;
};
