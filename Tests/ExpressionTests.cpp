#include "TestHelpers.h"

using namespace rb;

// MIDI expression (MIDI-2) and tempo-synced LFOs, measured on the real instruments.
class ExpressionTests : public juce::UnitTest
{
public:
    ExpressionTests() : juce::UnitTest ("Expression", "devices") {}

    // A plain sine voice: osc 1 sine only, filter open, no drift or spread.
    static Set sineSet (InstrumentType type = InstrumentType::analog)
    {
        TrackPreset preset;
        preset.name = "Sine";
        preset.type = type;
        if (type == InstrumentType::analog)
            preset.values = { { "osc1Shape", 0.0f }, { "oscMix", 0.0f }, { "cutoff", 20000.0f }, { "filterType", 0.0f },
                              { "drift", 0.0f }, { "spread", 0.0f }, { "ampAttack", 0.001f }, { "ampSustain", 1.0f },
                              { "filterEnv", 0.0f }, { "velToFilter", 0.0f } };
        Set set;
        set.tempo = 120.0;
        addTrack (set, preset);
        set.tracks[0].effects[0].type = (int) EffectType::none;
        set.tracks[0].effects[1].type = (int) EffectType::none;
        return set;
    }

    static double frequencyOf (const juce::AudioBuffer<float>& b, int start, int length)
    {
        int crossings = 0;
        for (int i = start + 1; i < start + length; ++i)
            if (b.getSample (0, i - 1) <= 0.0f && b.getSample (0, i) > 0.0f)
                ++crossings;
        return crossings * 48000.0 / length;
    }

    // Renders n samples into one buffer.
    static juce::AudioBuffer<float> render (rbtest::Rig& rig, int n)
    {
        juce::AudioBuffer<float> out (2, n);
        for (int done = 0; done < n;)
        {
            const int count = juce::jmin (rbtest::Rig::blockSize, n - done);
            rig.session.process (rig.buffer, count);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, done, rig.buffer, ch, 0, count);
            done += count;
        }
        return out;
    }

    static double highFrequencyRatio (const juce::AudioBuffer<float>& b)
    {
        double diff = 0.0, level = 0.0;
        for (int i = 1; i < b.getNumSamples(); ++i)
        {
            const double d = b.getSample (0, i) - b.getSample (0, i - 1);
            diff += d * d;
            level += (double) b.getSample (0, i) * b.getSample (0, i);
        }
        return level > 0.0 ? diff / level : 0.0;
    }

    void runTest() override
    {
        beginTest ("Pitch bend moves the pitch by up to two semitones");
        for (auto type : { InstrumentType::analog })
        {
            rbtest::Rig rig;
            auto set = sineSet (type);
            rig.open (set);
            const int id = set.tracks[0].id;

            rig.post (Command::Type::noteOn, id, 0, 69, 1.0f);
            auto before = render (rig, 24000);
            expectWithinAbsoluteError (frequencyOf (before, 4800, 19200), 440.0, 6.0);

            rig.post (Command::Type::pitchBend, id, 0, 0, 1.0f);
            auto bent = render (rig, 24000);
            expectWithinAbsoluteError (frequencyOf (bent, 4800, 19200), 440.0 * std::pow (2.0, 2.0 / 12.0), 6.0);

            rig.post (Command::Type::pitchBend, id, 0, 0, -0.5f);
            auto down = render (rig, 24000);
            expectWithinAbsoluteError (frequencyOf (down, 4800, 19200), 440.0 * std::pow (2.0, -1.0 / 12.0), 6.0);
        }

        beginTest ("Pressure opens the filter by two octaves (poly and channel)");
        for (auto type : { InstrumentType::analog, InstrumentType::wave })
        {
            // Full pressure at a 250 Hz cutoff must sound like no pressure at 1 kHz.
            auto brightness = [&] (float cutoff, int pressureNote, float pressure)
            {
                TrackPreset preset;
                preset.name = "Dark";
                preset.type = type;
                preset.values = { { "cutoff", cutoff }, { "filterEnv", 0.0f }, { "keyTrack", 0.0f }, { "drift", 0.0f },
                                  { "ampSustain", 1.0f }, { "velToFilter", 0.0f }, { "spread", 0.0f },
                                  { "osc1Pos", 0.7f }, { "osc2Level", 0.0f }, { "unisonSpread", 0.0f } };
                Set set;
                addTrack (set, preset);
                set.tracks[0].effects[0].type = (int) EffectType::none;
                set.tracks[0].effects[1].type = (int) EffectType::none;

                rbtest::Rig rig;
                rig.open (set);
                const int id = set.tracks[0].id;
                rig.post (Command::Type::noteOn, id, 0, 48, 1.0f);
                render (rig, 2400);
                if (pressure > 0.0f)
                    rig.post (Command::Type::pressure, id, 0, pressureNote, pressure);
                render (rig, 7200);
                return highFrequencyRatio (render (rig, 9600));
            };

            const double dark = brightness (250.0f, 0, 0.0f);
            const double reference = brightness (1000.0f, 0, 0.0f);
            const double poly = brightness (250.0f, 48, 1.0f);
            const double channel = brightness (250.0f, -1, 1.0f);
            const auto name = getInstrumentDisplayName (type);

            expect (reference > dark * 1.5, name + ": the reference is brighter");
            expectWithinAbsoluteError (poly / reference, 1.0, 0.1, name + ": poly pressure");
            expectWithinAbsoluteError (channel / reference, 1.0, 0.1, name + ": channel pressure");
        }

        beginTest ("Tempo-synced LFO follows the tempo");
        {
            auto lfoPeriod = [&] (double tempo)
            {
                TrackPreset preset;
                preset.name = "Tremolo";
                preset.type = InstrumentType::analog;
                preset.values = { { "osc1Shape", 0.0f }, { "oscMix", 0.0f }, { "cutoff", 20000.0f }, { "drift", 0.0f },
                                  { "spread", 0.0f }, { "ampSustain", 1.0f }, { "lfoSync", 1.0f }, { "lfoDivision", 5.0f }, // 1/4
                                  { "lfoShape", 4.0f }, { "lfoDest", 2.0f }, { "lfoAmount", 1.0f } };
                Set set;
                set.tempo = tempo;
                addTrack (set, preset);
                set.tracks[0].effects[0].type = (int) EffectType::none;
                set.tracks[0].effects[1].type = (int) EffectType::none;

                rbtest::Rig rig;
                rig.open (set);
                rig.post (Command::Type::noteOn, set.tracks[0].id, 0, 69, 1.0f);
                auto audio = render (rig, 48000 * 3);

                // Envelope in 5 ms windows, then the autocorrelation peak.
                std::vector<double> env;
                for (int i = 0; i + 240 <= audio.getNumSamples(); i += 240)
                    env.push_back (audio.getRMSLevel (0, i, 240));
                double mean = 0.0;
                for (auto v : env)
                    mean += v;
                mean /= (double) env.size();

                double best = 0.0;
                int bestLag = 0;
                for (int lag = 20; lag < (int) env.size() / 2; ++lag)
                {
                    double sum = 0.0;
                    for (size_t i = 0; i + (size_t) lag < env.size(); ++i)
                        sum += (env[i] - mean) * (env[i + (size_t) lag] - mean);
                    if (sum > best) { best = sum; bestLag = lag; }
                }
                return bestLag * 0.005;
            };

            expectWithinAbsoluteError (lfoPeriod (120.0), 0.5, 0.03);
            expectWithinAbsoluteError (lfoPeriod (60.0), 1.0, 0.05);
        }
    }
};

static ExpressionTests expressionTests;
