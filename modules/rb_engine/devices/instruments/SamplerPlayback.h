#pragma once

// Helpers shared by the Drum Kit pads and the Sampler: interpolated sample reads, the
// end-of-sample fade and a declicker for voices that must stop at once. Allocation-free.

namespace rb::sampler
{

// 4-point, 3rd-order Hermite interpolation at a fractional frame position. Frames outside
// the sample read as silence, so playback runs smoothly into and out of the data.
inline float readHermite (const float* data, int numFrames, double position)
{
    const double floorPos = std::floor (position);
    const int base = (int) floorPos;
    const float f = (float) (position - floorPos);

    float xm1, x0, x1, x2;
    if (base >= 1 && base + 2 < numFrames)
    {
        xm1 = data[base - 1];
        x0 = data[base];
        x1 = data[base + 1];
        x2 = data[base + 2];
    }
    else
    {
        auto at = [data, numFrames] (int i) { return (i >= 0 && i < numFrames) ? data[i] : 0.0f; };
        xm1 = at (base - 1);
        x0 = at (base);
        x1 = at (base + 1);
        x2 = at (base + 2);
    }

    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

struct Frame
{
    float l = 0.0f, r = 0.0f;
};

// Reads one stereo frame; mono samples feed both sides.
inline Frame readFrame (const SampleData& sample, double position)
{
    const int frames = sample.getNumFrames();
    const float l = readHermite (sample.buffer.getReadPointer (0), frames, position);
    if (sample.buffer.getNumChannels() < 2)
        return { l, l };
    return { l, readHermite (sample.buffer.getReadPointer (1), frames, position) };
}

// Gain that ramps to zero over the last ~2 ms of the sample, so samples that are cut off
// mid-waveform (fresh recordings, a trimmed start) end without a click.
inline float endFade (const SampleData& sample, double position)
{
    const double fadeFrames = juce::jmax (1.0, sample.sampleRate * 0.002);
    const double remaining = (double) sample.getNumFrames() - position;
    if (remaining >= fadeFrames)
        return 1.0f;
    return remaining <= 0.0 ? 0.0f : (float) (remaining / fadeFrames);
}

// When a voice must stop at once (its sample changes, a hard steal, all notes off), its last
// output value is handed to the declicker, which lets it decay to zero over ~1 ms instead of
// jumping. Rendered additively into the output.
class Declicker
{
public:
    void prepare (double sampleRate) { coeff = std::exp (-1.0f / (float) (sampleRate * 0.0012)); }
    void reset() { values.fill (0.0f); }

    // Order: dry left, dry right, send left, send right.
    void add (float dl, float dr, float sl = 0.0f, float sr = 0.0f)
    {
        values[0] += dl;
        values[1] += dr;
        values[2] += sl;
        values[3] += sr;
    }

    bool isActive() const
    {
        return std::abs (values[0]) + std::abs (values[1]) + std::abs (values[2]) + std::abs (values[3]) > 1.0e-6f;
    }

    void render (float* dl, float* dr, float* sl, float* sr, int numSamples)
    {
        if (! isActive())
        {
            values.fill (0.0f);
            return;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            dl[i] += values[0];
            dr[i] += values[1];
            if (sl != nullptr)
            {
                sl[i] += values[2];
                sr[i] += values[3];
            }
            for (auto& v : values)
                v *= coeff;
        }
    }

private:
    std::array<float, 4> values {};
    float coeff = 0.97f;
};

// Tiny deterministic noise source for per-voice effects.
struct NoiseSource
{
    juce::uint32 state = 0x12345678u;

    float next()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) state * (2.0f / 4294967295.0f) - 1.0f;
    }
};

} // namespace rb::sampler
