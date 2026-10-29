namespace rb::wave
{

namespace
{
constexpr int srcSize = WaveTableBank::sourceSize;

struct SineLut
{
    std::array<float, srcSize> values {};

    SineLut()
    {
        for (int i = 0; i < srcSize; ++i)
            values[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * i / srcSize);
    }

    float at (int index) const { return values[(size_t) (index & (srcSize - 1))]; }

    // Adds amp * sin (2 pi (harmonic * p + phase)) over one cycle.
    void addPartial (float* out, int harmonic, float amp, float phase) const
    {
        const int offset = (int) std::lround (phase * srcSize);
        for (int n = 0; n < srcSize; ++n)
            out[n] += amp * at (harmonic * n + offset);
    }
};

float frac (float x) { return x - std::floor (x); }

// Fixed pseudo-random partial phases: a low crest factor (louder after peak normalisation)
// and the same for every frame, so neighbouring frames morph without cancelling.
float partialPhase (int h) { return frac (std::sin ((float) h * 12.9898f) * 43758.547f); }

float basicShape (int shape, float p)
{
    switch (shape)
    {
        case 0:  return std::sin (dsp::twoPi * p);
        case 1:  return 1.0f - 4.0f * std::abs (frac (p + 0.25f) - 0.5f);
        case 2:  return 2.0f * frac (p + 0.5f) - 1.0f;
        default: return p < 0.5f ? 1.0f : -1.0f;
    }
}

// Generates one frame (srcSize samples, one cycle). t = frame position 0..1.
void generateFrame (int table, float t, float* out, const SineLut& lut)
{
    std::fill (out, out + srcSize, 0.0f);
    const auto phaseAt = [] (int n) { return (float) n / (float) srcSize; };

    switch (table)
    {
        case 0: // Basic: sine -> triangle -> saw -> square
        {
            const float x = t * 3.0f;
            const int a = juce::jmin (2, (int) x);
            const float f = x - (float) a;
            for (int n = 0; n < srcSize; ++n)
                out[n] = (1.0f - f) * basicShape (a, phaseAt (n)) + f * basicShape (a + 1, phaseAt (n));
            break;
        }

        case 1: // Harmonics: the number of partials sweeps from 1 to 256
        {
            const float count = std::pow (2.0f, t * 8.0f);
            for (int h = 1; h <= 256; ++h)
            {
                const float weight = juce::jlimit (0.0f, 1.0f, count - (float) h + 1.0f);
                if (weight > 0.0f)
                    lut.addPartial (out, h, weight / std::sqrt ((float) h), h == 1 ? 0.0f : partialPhase (h));
            }
            break;
        }

        case 2: // Vocal: formants gliding through the vowels a e i o u
        {
            static constexpr float vowels[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 },
                                                    { 570, 840, 2410 },  { 300, 870, 2240 } };
            const float x = t * 4.0f;
            const int a = juce::jmin (3, (int) x);
            const float f = x - (float) a;
            float formants[3];
            for (int i = 0; i < 3; ++i)
                formants[i] = vowels[a][i] + f * (vowels[a + 1][i] - vowels[a][i]);

            constexpr float f0 = 120.0f;
            constexpr float bandwidths[3] = { 90.0f, 120.0f, 170.0f };
            constexpr float gains[3] = { 1.0f, 0.6f, 0.3f };
            for (int h = 1; h <= 90; ++h)
            {
                const float hz = f0 * (float) h;
                float amp = 0.015f;
                for (int i = 0; i < 3; ++i)
                {
                    const float d = (hz - formants[i]) / bandwidths[i];
                    amp += gains[i] / (1.0f + d * d);
                }
                amp /= std::pow ((float) h, 0.3f);
                lut.addPartial (out, h, amp, partialPhase (h));
            }
            break;
        }

        case 3: // PWM: pulse width 50 % -> 5 %
        {
            const float width = 0.5f - 0.45f * t;
            for (int n = 0; n < srcSize; ++n)
                out[n] = phaseAt (n) < width ? 1.0f : -1.0f;
            break;
        }

        case 4: // Bell: two-operator FM, 2:7 ratio over a sine body, index rising
        {
            const float index = 0.3f + 4.5f * t;
            for (int n = 0; n < srcSize; ++n)
            {
                const float p = phaseAt (n);
                out[n] = 0.6f * std::sin (dsp::twoPi * p)
                         + std::sin (dsp::twoPi * 2.0f * p + index * std::sin (dsp::twoPi * 7.0f * p));
            }
            break;
        }

        case 5: // Digital: sample-and-hold steps and amplitude quantisation, getting coarser
        {
            const float steps = std::round (std::pow (2.0f, 6.0f - 4.0f * t));
            const float levels = std::round (std::pow (2.0f, 4.0f - 3.0f * t));
            for (int n = 0; n < srcSize; ++n)
            {
                const float p = std::floor (phaseAt (n) * steps) / steps;
                const float v = 0.8f * std::sin (dsp::twoPi * p) + 0.3f * std::sin (dsp::twoPi * 3.0f * p);
                out[n] = std::round (v * levels) / levels;
            }
            break;
        }

        case 6: // Sync: hard-synced saw, slave ratio 1 -> 8
        {
            const float ratio = 1.0f + 7.0f * t;
            for (int n = 0; n < srcSize; ++n)
                out[n] = 2.0f * frac (ratio * phaseAt (n) + 0.5f) - 1.0f;
            break;
        }

        case 7: // Organ: drawbar registrations from flute to full
        default:
        {
            static constexpr int partials[7] = { 1, 2, 3, 4, 5, 6, 8 };
            static constexpr float registrations[5][7] = {
                { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
                { 1.0f, 0.7f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f },
                { 1.0f, 0.8f, 0.8f, 0.6f, 0.0f, 0.0f, 0.2f },
                { 1.0f, 0.8f, 0.6f, 0.8f, 0.4f, 0.5f, 0.6f },
                { 1.0f, 1.0f, 1.0f, 1.0f, 0.8f, 0.8f, 1.0f },
            };
            const float x = t * 4.0f;
            const int a = juce::jmin (3, (int) x);
            const float f = x - (float) a;
            for (int i = 0; i < 7; ++i)
            {
                const float w = registrations[a][i] + f * (registrations[a + 1][i] - registrations[a][i]);
                if (w > 0.0f)
                    lut.addPartial (out, partials[i], w / std::sqrt ((float) partials[i]), partialPhase (partials[i] + 3));
            }
            break;
        }
    }
}

int orderFor (int size)
{
    int order = 0;
    while ((1 << order) < size)
        ++order;
    return order;
}
} // namespace

const juce::StringArray& WaveTableBank::getTableNames()
{
    static const juce::StringArray names { "Basic", "Harmonics", "Vocal", "PWM", "Bell", "Digital", "Sync", "Organ" };
    return names;
}

const WaveTableBank& WaveTableBank::get()
{
    static const WaveTableBank bank;
    return bank;
}

WaveTableBank::WaveTableBank()
{
    size_t offset = 0;
    for (int level = 0; level < numLevels; ++level)
    {
        levelOffsets[(size_t) level] = offset;
        offset += (size_t) levelSize (level) + 1;
    }
    frameStride = offset;

    const int numTables = getNumTables();
    data.assign ((size_t) numTables * (size_t) numFrames * frameStride, 0.0f);

    for (int i = 0; i <= sineSize; ++i)
        sineRow[(size_t) i] = (float) std::sin (juce::MathConstants<double>::twoPi * i / sineSize);

    const SineLut lut;
    juce::dsp::FFT forward (orderFor (srcSize));
    std::array<std::unique_ptr<juce::dsp::FFT>, numLevels> inverse;
    for (int level = 0; level < numLevels; ++level)
        inverse[(size_t) level] = std::make_unique<juce::dsp::FFT> (orderFor (levelSize (level)));

    std::vector<float> source ((size_t) srcSize);
    std::vector<float> spectrum ((size_t) srcSize * 2);
    std::vector<float> work ((size_t) srcSize * 2);

    for (int table = 0; table < numTables; ++table)
    {
        for (int frame = 0; frame < numFrames; ++frame)
        {
            generateFrame (table, (float) frame / (float) (numFrames - 1), source.data(), lut);

            std::fill (spectrum.begin(), spectrum.end(), 0.0f);
            std::copy (source.begin(), source.end(), spectrum.begin());
            forward.performRealOnlyForwardTransform (spectrum.data(), true);

            float* frameData = data.data() + ((size_t) table * (size_t) numFrames + (size_t) frame) * frameStride;

            for (int level = 0; level < numLevels; ++level)
            {
                const int size = levelSize (level);
                const int harmonics = juce::jmin (512 >> level, size / 2 - 1);
                const float scale = (float) size / (float) srcSize;

                std::fill (work.begin(), work.end(), 0.0f);
                for (int k = 1; k <= harmonics; ++k)
                {
                    work[(size_t) (2 * k)] = spectrum[(size_t) (2 * k)] * scale;
                    work[(size_t) (2 * k + 1)] = spectrum[(size_t) (2 * k + 1)] * scale;
                }
                inverse[(size_t) level]->performRealOnlyInverseTransform (work.data());

                float* row = frameData + levelOffsets[(size_t) level];
                std::copy (work.begin(), work.begin() + size, row);
                row[size] = row[0];
            }

            // Normalise on the widest level: peak at most 0.95, RMS at most 0.5, so frames and
            // tables are roughly equally loud. Every level gets the same gain.
            const float* top = frameData;
            float peak = 0.0f, sumSquares = 0.0f;
            for (int n = 0; n < levelSize (0); ++n)
            {
                peak = juce::jmax (peak, std::abs (top[n]));
                sumSquares += top[n] * top[n];
            }
            const float rms = std::sqrt (sumSquares / (float) levelSize (0));
            const float gain = juce::jmin (0.95f / juce::jmax (1.0e-6f, peak), 0.5f / juce::jmax (1.0e-6f, rms));
            for (size_t i = 0; i < frameStride; ++i)
                frameData[i] *= gain;
        }
    }
}

} // namespace rb::wave
