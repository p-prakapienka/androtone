#include "TestHelpers.h"

using namespace rb;

namespace
{
constexpr double fxRate = 48000.0;
constexpr int fxBlock = 256;

std::vector<EffectType> allEffects()
{
    std::vector<EffectType> types;
    for (int t = 1; t < numEffectTypes; ++t)
        types.push_back ((EffectType) t);
    return types;
}

bool hasMix (EffectType t) { return t != EffectType::eq && t != EffectType::dynamics; }

// One effect driven like the engine does: setTempo + setParameters before every block.
struct FxRig
{
    explicit FxRig (EffectType t, std::function<void (FxRig&)> configure = {})
        : type (t), spec (getEffectSpec (t)), values (spec.defaults()), fx (createEffect (t))
    {
        if (configure)
            configure (*this);
        fx->prepare (fxRate, fxBlock);
        fx->setTempo (tempo);
        apply();
    }

    void set (const juce::String& id, float v)
    {
        const int index = spec.indexOf (id);
        jassert (index >= 0);
        if (index >= 0)
            values[(size_t) index] = spec.params[(size_t) index].clamp (v);
    }

    void setPreset (const EffectPreset& preset)
    {
        values = spec.defaults();
        for (auto& [id, v] : preset.values)
            set (id, v);
    }

    void apply() { fx->setParameters (values.data(), (int) values.size()); }

    void render (juce::AudioBuffer<float>& buffer, std::function<void (FxRig&, int)> perBlock = {})
    {
        const int total = buffer.getNumSamples();
        int blockIndex = 0;
        for (int start = 0; start < total; start += fxBlock, ++blockIndex)
        {
            const int n = juce::jmin (fxBlock, total - start);
            if (perBlock)
                perBlock (*this, blockIndex);
            fx->setTempo (tempo);
            apply();
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, start, n);
            fx->process (view, n);
        }
    }

    EffectType type;
    const DeviceSpec& spec;
    std::vector<float> values;
    std::unique_ptr<EffectProcessor> fx;
    double tempo = 120.0;
};

juce::AudioBuffer<float> makeNoise (int numSamples, float amplitude, int seed = 1)
{
    juce::AudioBuffer<float> b (2, numSamples);
    juce::Random r (seed);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            b.setSample (ch, i, (r.nextFloat() * 2.0f - 1.0f) * amplitude);
    return b;
}

juce::AudioBuffer<float> makeSine (int numSamples, float hz, float amplitude)
{
    juce::AudioBuffer<float> b (2, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const float v = amplitude * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / (float) fxRate);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

float peakOf (const juce::AudioBuffer<float>& b, int start = 0, int count = -1)
{
    if (count < 0)
        count = b.getNumSamples() - start;
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = start; i < start + count; ++i)
            peak = juce::jmax (peak, std::abs (b.getSample (ch, i)));
    return peak;
}

float rmsOf (const juce::AudioBuffer<float>& b, int channel, int start, int count)
{
    double sum = 0.0;
    for (int i = start; i < start + count; ++i)
        sum += (double) b.getSample (channel, i) * b.getSample (channel, i);
    return (float) std::sqrt (sum / count);
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

// Seconds after `fromSample` until every later 10 ms window stays below `threshold`; -1 if never.
double decayTime (const juce::AudioBuffer<float>& b, int fromSample, float threshold)
{
    constexpr int window = 480;
    int lastLoud = -1;
    for (int start = fromSample; start + window <= b.getNumSamples(); start += window)
        if (peakOf (b, start, window) >= threshold)
            lastLoud = start;
    if (lastLoud + 2 * window > b.getNumSamples())
        return -1.0;
    return (double) (lastLoud + window - fromSample) / fxRate;
}

const EffectPreset* presetWithMax (EffectType type, const juce::String& paramId)
{
    const EffectPreset* best = nullptr;
    float bestValue = -1.0e9f;
    for (auto& p : getFactoryEffectPresets())
        if (p.type == type)
            if (auto it = p.values.find (paramId); it != p.values.end() && it->second > bestValue)
            {
                bestValue = it->second;
                best = &p;
            }
    return best;
}
} // namespace

class EffectTests : public juce::UnitTest
{
public:
    EffectTests() : juce::UnitTest ("Effects", "devices") {}

    void runTest() override
    {
        beginTest ("Effect specs: ids, names, ranges, mix last (DEV-22)");
        {
            const juce::StringArray names { "Auto Filter", "EQ", "Chorus", "Delay", "Dynamics", "Phaser", "Crusher", "Reverb", "Saturator" };
            for (auto type : allEffects())
            {
                const auto& spec = getEffectSpec (type);
                const juce::String what = spec.typeId + ": ";
                expectEquals (spec.typeId, toTypeId (type));
                expectEquals (spec.displayName, names[(int) type - 1]);
                expect (createEffect (type) != nullptr, what + "factory");
                expect (spec.params.size() >= 4 && spec.params.size() <= (size_t) maxDeviceParams, what + "param count");

                juce::StringArray ids;
                for (auto& p : spec.params)
                {
                    expect (! ids.contains (p.id), what + "duplicate id " + p.id);
                    ids.add (p.id);
                    expect (p.minValue < p.maxValue, what + "range of " + p.id);
                    expect (juce::exactlyEqual (p.clamp (p.defaultValue), p.defaultValue), what + "default of " + p.id);
                    if (p.unit == ParamUnit::choice)
                        expectEquals (p.choices.size(), juce::roundToInt (p.maxValue) + 1, what + "choices of " + p.id);
                    if (spec.params.size() > 8)
                        expect (spec.groups.contains (p.group), what + "group of " + p.id);
                }

                if (spec.params.size() > 8)
                    for (auto& g : spec.groups)
                    {
                        int count = 0;
                        for (auto& p : spec.params)
                            count += p.group == g ? 1 : 0;
                        expect (count > 0 && count <= 8, what + "page " + g);
                    }

                if (hasMix (type))
                {
                    expectEquals (spec.params.back().id, juce::String ("mix"), what + "mix last");
                    expect (spec.params.back().unit == ParamUnit::percent, what + "mix unit");
                }
            }
        }

        beginTest ("Defaults: noise and sine stay finite and below +12 dB");
        for (auto type : allEffects())
        {
            const auto id = toTypeId (type);
            {
                FxRig rig (type);
                auto b = makeNoise ((int) fxRate * 2, 1.0f);
                rig.render (b);
                expect (allFinite (b), id + " noise finite");
                expect (peakOf (b) < 4.0f, id + " noise peak " + juce::String (peakOf (b)));
            }
            {
                FxRig rig (type);
                auto b = makeSine ((int) fxRate * 2, 440.0f, 0.9f);
                rig.render (b);
                expect (allFinite (b), id + " sine finite");
                expect (peakOf (b) < 4.0f, id + " sine peak " + juce::String (peakOf (b)));
            }
        }

        beginTest ("Mix 0 is dry");
        for (auto type : allEffects())
        {
            if (! hasMix (type))
                continue;
            FxRig rig (type, [] (FxRig& r) { r.set ("mix", 0.0f); });
            auto input = makeNoise ((int) fxRate, 0.7f, 7);
            input.addFrom (0, 0, makeSine ((int) fxRate, 220.0f, 0.3f), 0, 0, (int) fxRate);
            auto b = input;
            rig.render (b);
            float maxDiff = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    maxDiff = juce::jmax (maxDiff, std::abs (b.getSample (ch, i) - input.getSample (ch, i)));
            expect (maxDiff < 1.0e-6f, toTypeId (type) + " differs by " + juce::String (maxDiff));
        }

        beginTest ("Random parameter sweeps stay finite and bounded");
        for (auto type : allEffects())
        {
            for (int seed = 1; seed <= 3; ++seed)
            {
                juce::Random random (seed * 7919 + (int) type);
                FxRig rig (type);
                auto b = makeNoise ((int) fxRate * 4, 0.25f, seed);
                b.addFrom (0, 0, makeSine ((int) fxRate * 4, 110.0f, 0.25f), 0, 0, (int) fxRate * 4);

                rig.render (b, [&random] (FxRig& r, int block)
                {
                    if (block % 3 != 0)
                        return;
                    for (size_t i = 0; i < r.values.size(); ++i)
                        if (random.nextInt (3) == 0)
                            r.values[i] = r.spec.params[i].fromNormalised (random.nextFloat());
                    if (random.nextInt (20) == 0)
                        r.tempo = 40.0 + 200.0 * random.nextDouble();
                });

                expect (allFinite (b), toTypeId (type) + " finite (seed " + juce::String (seed) + ")");
                expect (peakOf (b) < 16.0f, toTypeId (type) + " bounded, peak " + juce::String (peakOf (b)));
            }
        }

        beginTest ("reset() clears tails: silence in, silence out");
        for (auto type : allEffects())
        {
            FxRig rig (type, [] (FxRig& r)
            {
                if (r.spec.indexOf ("mix") >= 0)
                    r.set ("mix", 1.0f);
            });
            auto noise = makeNoise ((int) fxRate, 0.8f);
            rig.render (noise);
            rig.fx->reset();
            juce::AudioBuffer<float> silence (2, (int) fxRate);
            silence.clear();
            rig.render (silence);
            expect (peakOf (silence) < 1.0e-6f, toTypeId (type) + " after reset: " + juce::String (peakOf (silence)));
        }

        beginTest ("Reverb tail decays to -90 dBFS");
        {
            const auto* preset = presetWithMax (EffectType::reverb, "decay");
            expect (preset != nullptr);
            if (preset != nullptr)
            {
                FxRig rig (EffectType::reverb, [preset] (FxRig& r) { r.setPreset (*preset); r.set ("mix", 1.0f); });
                const int inputLength = (int) fxRate / 2;
                auto b = makeNoise ((int) fxRate * 30, 0.8f);
                for (int ch = 0; ch < 2; ++ch)
                    b.clear (ch, inputLength, b.getNumSamples() - inputLength);
                rig.render (b);
                const double t80 = decayTime (b, inputLength, 1.0e-4f);
                const double t90 = decayTime (b, inputLength, 3.16e-5f);
                const float decaySeconds = preset->values.at ("decay");
                logMessage (preset->name + ": -80 dB after " + juce::String (t80, 2) + " s, -90 dB after "
                            + juce::String (t90, 2) + " s (decay " + juce::String (decaySeconds) + " s)");
                expect (t80 > 0.0 && t80 < decaySeconds * 1.5 + 2.0, "reverb -80 dB time " + juce::String (t80));
                expect (t90 > 0.0 && t90 < decaySeconds * 1.8 + 3.0, "reverb -90 dB time " + juce::String (t90));
                expect (peakOf (b, b.getNumSamples() - (int) fxRate) < 3.16e-5f, "reverb silent at the end");
            }
        }

        beginTest ("Delay tail decays below -80 dBFS at maximum preset feedback");
        {
            const auto* preset = presetWithMax (EffectType::delay, "feedback");
            expect (preset != nullptr);
            if (preset != nullptr)
            {
                FxRig rig (EffectType::delay, [preset] (FxRig& r) { r.setPreset (*preset); r.set ("mix", 1.0f); });
                const int inputLength = (int) fxRate / 2;
                auto b = makeNoise ((int) fxRate * 30, 0.8f);
                for (int ch = 0; ch < 2; ++ch)
                    b.clear (ch, inputLength, b.getNumSamples() - inputLength);
                rig.render (b);
                const double t80 = decayTime (b, inputLength, 1.0e-4f);
                logMessage (preset->name + ": -80 dB after " + juce::String (t80, 2) + " s (feedback "
                            + juce::String (preset->values.at ("feedback")) + ")");
                expect (t80 > 0.0 && t80 < 20.0, "delay -80 dB time " + juce::String (t80));
            }
        }

        beginTest ("EQ: low shelf +6 dB measured at 50 Hz");
        {
            FxRig rig (EffectType::eq, [] (FxRig& r) { r.set ("low", 6.0f); });
            auto input = makeSine ((int) fxRate * 2, 50.0f, 0.25f);
            auto b = input;
            rig.render (b);
            const int n = (int) fxRate;
            const float gainDb = juce::Decibels::gainToDecibels (rmsOf (b, 0, n, n) / rmsOf (input, 0, n, n));
            logMessage ("EQ +6 dB low shelf at 50 Hz: " + juce::String (gainDb, 2) + " dB");
            expectWithinAbsoluteError (gainDb, 6.0f, 1.0f);

            FxRig flat (EffectType::eq);
            auto f = makeNoise ((int) fxRate, 0.5f);
            auto fIn = f;
            flat.render (f);
            const float flatDb = juce::Decibels::gainToDecibels (rmsOf (f, 0, 0, (int) fxRate) / rmsOf (fIn, 0, 0, (int) fxRate));
            expectWithinAbsoluteError (flatDb, 0.0f, 0.1f);
        }

        beginTest ("Saturator: drive raises the level of a quiet sine");
        {
            const auto render = [] (float drive)
            {
                FxRig rig (EffectType::saturator, [drive] (FxRig& r) { r.set ("curve", 0.0f); r.set ("drive", drive); });
                auto b = makeSine ((int) fxRate, 220.0f, 0.05f);
                rig.render (b);
                return rmsOf (b, 0, (int) fxRate / 2, (int) fxRate / 2);
            };
            const float clean = render (0.0f), driven = render (24.0f);
            logMessage ("Saturator RMS: drive 0 dB " + juce::String (clean, 4) + ", drive 24 dB " + juce::String (driven, 4));
            expect (driven > clean * 3.0f, "driven RMS " + juce::String (driven));
        }

        beginTest ("Dynamics: reduces the peak of a loud signal");
        {
            FxRig rig (EffectType::dynamics);
            auto b = makeSine ((int) fxRate * 2, 100.0f, 1.0f);
            rig.render (b);
            const float peak = peakOf (b, (int) fxRate, (int) fxRate);
            logMessage ("Dynamics: 0 dBFS sine peak after compression " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS");
            expect (peak < 0.7f, "peak " + juce::String (peak));

            FxRig quiet (EffectType::dynamics, [] (FxRig& r) { r.set ("makeup", 0.0f); });
            auto q = makeSine ((int) fxRate, 100.0f, 0.05f);
            quiet.render (q);
            expectWithinAbsoluteError (peakOf (q, (int) fxRate / 2, (int) fxRate / 2), 0.05f, 0.002f);
        }

        beginTest ("Delay: 1/4 synced at 120 BPM echoes after 0.5 s");
        {
            for (int pingPong = 0; pingPong < 2; ++pingPong)
            {
                FxRig rig (EffectType::delay, [pingPong] (FxRig& r)
                {
                    r.set ("sync", 1.0f);
                    r.set ("division", (float) r.spec.params[(size_t) r.spec.indexOf ("division")].choices.indexOf ("1/4"));
                    r.set ("feedback", 0.0f);
                    r.set ("modulation", 0.0f);
                    r.set ("filterWidth", 1.0f);
                    r.set ("pingPong", (float) pingPong);
                    r.set ("mix", 1.0f);
                });
                juce::AudioBuffer<float> b (2, (int) fxRate);
                b.clear();
                b.setSample (0, 0, 1.0f);
                b.setSample (1, 0, 1.0f);
                rig.render (b);

                int best = 0;
                for (int i = 0; i < b.getNumSamples(); ++i)
                    if (std::abs (b.getSample (0, i)) > std::abs (b.getSample (0, best)))
                        best = i;
                logMessage ("Delay echo at sample " + juce::String (best) + (pingPong ? " (ping pong)" : ""));
                expect (std::abs (best - 24000) <= 48, "echo at " + juce::String (best));
                expect (std::abs (b.getSample (0, best)) > 0.3f, "echo level");
            }
        }

        beginTest ("Factory effect presets match their specs (DEV-23)");
        {
            std::map<EffectType, juce::StringArray> names;
            for (auto& preset : getFactoryEffectPresets())
            {
                expect (preset.type != EffectType::none);
                const auto& spec = getEffectSpec (preset.type);
                expect (! names[preset.type].contains (preset.name), "duplicate preset " + preset.name);
                names[preset.type].add (preset.name);
                for (auto& [id, value] : preset.values)
                {
                    const int index = spec.indexOf (id);
                    expect (index >= 0, preset.name + ": unknown param " + id);
                    if (index >= 0)
                    {
                        const auto& p = spec.params[(size_t) index];
                        expect (value >= p.minValue && value <= p.maxValue, preset.name + ": " + id + " out of range");
                    }
                }

                FxRig rig (preset.type, [&preset] (FxRig& r) { r.setPreset (preset); });
                auto b = makeNoise ((int) fxRate, 0.5f);
                b.addFrom (0, 0, makeSine ((int) fxRate, 150.0f, 0.4f), 0, 0, (int) fxRate);
                rig.render (b);
                expect (allFinite (b) && peakOf (b) < 4.0f, preset.name + " peak " + juce::String (peakOf (b)));
            }
            for (auto type : allEffects())
                expect (names[type].size() >= 3 && names[type].size() <= 6, toTypeId (type) + " preset count");
        }

        beginTest ("CPU: all nine effects in series");
        {
            std::vector<std::unique_ptr<FxRig>> chain;
            for (auto type : allEffects())
                chain.push_back (std::make_unique<FxRig> (type, [] (FxRig& r)
                {
                    // Engage the optional stages so the measurement is not flattered.
                    if (r.type == EffectType::autoFilter) r.set ("drive", 6.0f);
                    if (r.type == EffectType::saturator) r.set ("colour", 0.3f);
                }));

            const int total = (int) fxRate * 10;
            auto input = makeNoise (total, 0.3f);
            input.addFrom (0, 0, makeSine (total, 220.0f, 0.3f), 0, 0, total);
            juce::AudioBuffer<float> block (2, fxBlock);

            // Best of three runs, so a busy machine does not make the check flaky.
            double factor = 0.0;
            juce::String runs;
            for (int run = 0; run < 3; ++run)
            {
                const double startMs = juce::Time::getMillisecondCounterHiRes();
                for (int start = 0; start < total; start += fxBlock)
                {
                    const int n = juce::jmin (fxBlock, total - start);
                    for (int ch = 0; ch < 2; ++ch)
                        block.copyFrom (ch, 0, input, ch, start, n);
                    for (auto& rig : chain)
                    {
                        rig->fx->setTempo (120.0);
                        rig->apply();
                        rig->fx->process (block, n);
                    }
                }
                const double elapsed = (juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0;
                const double runFactor = 10.0 / juce::jmax (1.0e-6, elapsed);
                runs << juce::String (runFactor, 1) << "x ";
                factor = juce::jmax (factor, runFactor);
            }
            logMessage ("CPU: nine effects in series render at " + juce::String (factor, 1) + "x real time (runs: " + runs.trim() + ")");

            // Per-effect breakdown (informational).
            juce::String breakdown;
            for (auto& rig : chain)
            {
                auto b = input;
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                rig->render (b);
                const double seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                breakdown << rig->spec.typeId << " " << juce::roundToInt (10.0 / juce::jmax (1.0e-6, seconds)) << "x  ";
            }
            logMessage ("CPU per effect: " + breakdown);
            expect (allFinite (block));
           #if ! JUCE_DEBUG
            expect (factor > 30.0, "real-time factor " + juce::String (factor, 1));
           #endif
        }
    }
};

static EffectTests effectTests;
