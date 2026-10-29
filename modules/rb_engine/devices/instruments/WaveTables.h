#pragma once

namespace rb::wave
{

// Factory wavetables for the Wave synth, generated in code the first time they are needed and
// shared by every instance (read-only afterwards, so safe on the audio thread).
//
// Each table has numFrames single-cycle frames that the position parameter morphs through.
// Every frame is stored band-limited per octave (mipmaps): level L keeps the first
// (512 >> L) harmonics, so playback picks the level whose top harmonic stays below Nyquist.
// Rows carry one guard sample (row[size] == row[0]) for linear interpolation.
class WaveTableBank
{
public:
    static constexpr int sourceSize = 2048;
    static constexpr int numFrames = 16;
    static constexpr int numLevels = 8;
    static constexpr int sineSize = 256;

    static const WaveTableBank& get();
    static const juce::StringArray& getTableNames();
    static int getNumTables() { return getTableNames().size(); }

    static constexpr int levelSize (int level)
    {
        constexpr int sizes[numLevels] = { 2048, 2048, 1024, 512, 256, 256, 256, 256 };
        return sizes[level];
    }

    // Mipmap level for a phase increment (cycles per sample).
    static int levelFor (float increment)
    {
        // Slightly lenient: partials may reach 0.625 * fs, folding back above ~18 kHz at 48 kHz.
        float v = 1024.0f * increment * 0.8f;
        int level = 0;
        while (v > 1.0f && level < numLevels - 1)
        {
            v *= 0.5f;
            ++level;
        }
        return level;
    }

    const float* row (int table, int frame, int level) const
    {
        const auto t = (size_t) juce::jlimit (0, getNumTables() - 1, table);
        const auto f = (size_t) juce::jlimit (0, numFrames - 1, frame);
        return data.data() + (t * (size_t) numFrames + f) * frameStride + levelOffsets[(size_t) level];
    }

    const float* sine() const { return sineRow.data(); } // sineSize + 1 samples, peak 1

private:
    WaveTableBank();

    std::vector<float> data;
    std::array<size_t, numLevels> levelOffsets {};
    size_t frameStride = 0;
    std::array<float, sineSize + 1> sineRow {};
};

} // namespace rb::wave
