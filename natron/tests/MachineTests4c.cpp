// Phase 4 machines, batch 2: PadSynth, Bytebeat, Sampler, Vocoder, Modular; samples; custom drums.
// 48 kHz, 120 BPM (a beat = 24000 samples).

#include "Analysis.h"
#include "TestHelpers.h"
#include "core/SampleData.h"
#include "core/SongExport.h"
#include "core/machines/Bytebeat.h"
#include "core/machines/BytebeatProgram.h"
#include "core/machines/DrumMachine.h"
#include "core/machines/Modular.h"
#include "core/machines/PadSynth.h"
#include "core/machines/Sampler.h"
#include "core/machines/Vocoder.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <functional>
#include <juce_dsp/juce_dsp.h>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
    constexpr double sampleRate = 48000.0;

    SampleData sine(double frequency, double seconds, double rate = 48000.0) {
        SampleData sample;
        sample.name = "sine " + juce::String(frequency);
        sample.sampleRate = rate;
        sample.audio.setSize(1, static_cast<int>(seconds * rate));
        for (int i = 0; i < sample.audio.getNumSamples(); i++) {
            sample.audio.setSample(0, i, static_cast<float>(0.5 * std::sin(juce::MathConstants<double>::twoPi * frequency * i / rate)));
        }
        return sample;
    }

    juce::ValueTree vocoderData(const SampleData& sample) {
        juce::ValueTree tree("Vocoder");
        tree.appendChild(sample.toTree(), nullptr);
        return tree;
    }
}

// ---------------------------------------------------------------------------- shared

TEST_CASE("Batch 2 machines play in tune and stop cleanly", "[machines][4c]") {
    const juce::String type = GENERATE(as<juce::String> {}, PadSynth::typeName, Sampler::typeName, Vocoder::typeName, Modular::typeName);
    const int note = GENERATE(48, 69);
    CAPTURE(type, note);
    MachineRig rig(type);
    if (type == PadSynth::typeName) {
        rig.set("bandwidth", 1.0f); // nearly pure harmonics, so the pitch is unambiguous
        rig.set("attack", 0.01f);
        rig.set("release", 0.05f);
    }
    if (type == Modular::typeName) {
        rig.set("osc2Semitones", 0.0f);
        rig.set("patch2Amount", 0.0f);
    }
    if (type == Vocoder::typeName) {
        rig.set("noise", 0.0f);
        rig.set("sibilance", 0.0f);
    }
    const auto audio = rig.play({ { 0.0, 1.0, note, 0.9f } }, 1.3);
    const double expected = juce::MidiMessage::getMidiNoteInHertz(note);
    CHECK_THAT(Analysis::pitch(audio, 4800, sampleRate), WithinRel(expected, 0.02));

    const auto stats = analyse(audio);
    CHECK(stats.allFinite);
    CHECK(stats.peak < 1.5f);
    CHECK(Analysis::rms(audio, 0, audio.getNumSamples() - 2400, 2400) < 1.0e-3);
}

// ---------------------------------------------------------------------------- SampleData

TEST_CASE("Samples survive a round trip through a song file's encoding", "[samples]") {
    const auto original = sine(440.0, 0.25);
    SampleData decoded;
    REQUIRE(SampleData::fromTree(original.toTree(), decoded).wasOk());
    CHECK(decoded.name == original.name);
    CHECK(juce::exactlyEqual(decoded.sampleRate, original.sampleRate));
    REQUIRE(decoded.audio.getNumSamples() == original.audio.getNumSamples());
    float difference = 0.0f;
    for (int i = 0; i < original.audio.getNumSamples(); i++) {
        difference = std::max(difference, std::abs(decoded.audio.getSample(0, i) - original.audio.getSample(0, i)));
    }
    CHECK(difference < 1.0e-4f); // 16-bit
}

TEST_CASE("Samples load from audio files, truncated at the limit", "[samples]") {
    juce::AudioBuffer<float> audio(2, 40 * 1000); // 40 s at 1 kHz
    audio.clear();
    audio.setSample(1, 5, 0.5f);
    juce::MemoryOutputStream wav;
    REQUIRE(SongExport::writeWav(audio, 1000.0, 16, wav).wasOk());

    SampleData loaded;
    juce::MemoryInputStream in(wav.getData(), wav.getDataSize(), false);
    REQUIRE(SampleData::load(in, "long", loaded).wasOk());
    CHECK(loaded.audio.getNumChannels() == 2);
    CHECK(loaded.audio.getNumSamples() == static_cast<int>(SampleData::maxSeconds * 1000));
    CHECK(std::abs(loaded.audio.getSample(1, 5) - 0.5f) < 1.0e-3f);

    juce::MemoryInputStream junk("not audio", 9, false);
    CHECK(SampleData::load(junk, "junk", loaded).failed());
    juce::ValueTree corrupt("Sample");
    corrupt.setProperty("wav", "!!!", nullptr);
    CHECK(SampleData::fromTree(corrupt, loaded).failed());
}

// ---------------------------------------------------------------------------- PadSynth

TEST_CASE("PadSynth's table holds its harmonics, spread by the bandwidth", "[machines][4c]") {
    float harmonics[PadSynth::numHarmonics] = { 1.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    // The table loops seamlessly, so an FFT over the whole table has no leakage: bin k is exactly
    // k * tableSampleRate / tableSize Hz.
    auto power = [&](float bandwidth) {
        const auto table = PadSynth::buildTable(harmonics, bandwidth, 1.0f);
        juce::dsp::FFT fft(PadSynth::tableOrder);
        std::vector<float> data(table->samples.begin(), table->samples.end());
        data.resize(2 * data.size(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(data.data());
        return data;
    };
    const double binHz = PadSynth::tableSampleRate / (1 << PadSynth::tableOrder);
    auto energy = [&](const std::vector<float>& spectrum, double lowHz, double highHz) {
        double sum = 0.0;
        for (int k = static_cast<int>(lowHz / binHz); k <= static_cast<int>(highHz / binHz); k++) {
            sum += static_cast<double>(spectrum[static_cast<size_t>(k)]) * spectrum[static_cast<size_t>(k)];
        }
        return sum;
    };
    const double f = PadSynth::tableFrequency;

    const auto narrow = power(1.0f);
    const double fundamental = energy(narrow, f * 0.99, f * 1.01);
    CHECK(energy(narrow, 2 * f * 0.99, 2 * f * 1.01) < 1.0e-3 * fundamental); // harmonic 2 is off
    CHECK(energy(narrow, 3 * f * 0.99, 3 * f * 1.01) > 0.1 * fundamental);

    // A wide bandwidth (150 cents ~ +-20 Hz at the fundamental) spreads energy away from the centre.
    const auto wide = power(150.0f);
    auto spread = [&](const std::vector<float>& s) { return energy(s, f * 1.02, f * 1.12) / energy(s, f * 0.99, f * 1.01); };
    CHECK(spread(narrow) < 0.01);
    CHECK(spread(wide) > 0.5);
}

TEST_CASE("PadSynth harmonic changes reach the sound", "[machines][4c]") {
    auto octaveEnergy = [](float secondHarmonic) {
        MachineRig rig(PadSynth::typeName);
        for (int h = 1; h <= PadSynth::numHarmonics; h++) {
            rig.set("harmonic" + juce::String(h), h == 1 ? 1.0f : 0.0f);
        }
        rig.set("harmonic2", secondHarmonic);
        rig.set("bandwidth", 5.0f);
        rig.set("attack", 0.01f);
        const auto audio = rig.play({ { 0.0, 2.0, 57, 0.9f } }, 0.5);
        return Analysis::bandEnergy(audio, 9600, sampleRate, 420.0, 460.0) / Analysis::bandEnergy(audio, 9600, sampleRate, 210.0, 230.0);
    };
    CHECK(octaveEnergy(0.0f) < 0.01);
    CHECK(octaveEnergy(1.0f) > 0.3);
}

// ---------------------------------------------------------------------------- Bytebeat

TEST_CASE("Bytebeat formulas evaluate like C", "[bytebeat]") {
    struct Case {
        const char* formula;
        std::function<uint32_t(uint32_t)> reference;
    };
    const Case cases[] = {
        { "t", [](uint32_t t) { return t; } },
        { "t*(42&t>>10)", [](uint32_t t) { return t * (42 & t >> 10); } },
        { "t*5&t>>7|t*3&t>>10", [](uint32_t t) { return (t * 5 & t >> 7) | (t * 3 & t >> 10); } },
        { "(t>>6|t|t>>(t>>16))*10+((t>>11)&7)", [](uint32_t t) { return (t >> 6 | t | (t >> 16 >= 32 ? 0 : t >> (t >> 16))) * 10 + ((t >> 11) & 7); } },
        { "t%255 < 128 ? t*2 : t/3 - 1", [](uint32_t t) { return t % 255 < 128 ? t * 2 : t / 3 - 1; } },
        { "~t ^ -t + 0x1F", [](uint32_t t) { return ~t ^ (0u - t + 0x1F); } },
        { "t/(t%7) + !(t&1) + (t==100) + (t!=3)", [](uint32_t t) { return (t % 7 == 0 ? 0 : t / (t % 7)) + !(t & 1) + (t == 100) + (t != 3); } },
        { "t<<3 >= 800", [](uint32_t t) { return static_cast<uint32_t>((t << 3) >= 800); } },
    };
    for (const auto& c : cases) {
        CAPTURE(c.formula);
        BytebeatProgram program;
        REQUIRE(BytebeatProgram::compile(c.formula, program).wasOk());
        for (uint32_t t = 0; t < 100000; t += 37) {
            CAPTURE(t);
            REQUIRE(program.evaluate(t) == static_cast<uint8_t>(c.reference(t) & 0xff));
        }
    }
}

TEST_CASE("Bad bytebeat formulas are rejected with a position", "[bytebeat]") {
    BytebeatProgram program;
    REQUIRE(BytebeatProgram::compile("t", program).wasOk());
    for (const auto* bad : { "", "t*", "(t", "t)", "x+1", "t ? 1", "0x", "t &&& 3" }) {
        CAPTURE(bad);
        const auto result = BytebeatProgram::compile(bad, program);
        CHECK(result.failed());
    }
    CHECK(BytebeatProgram::compile("t +* 2", program).getErrorMessage().contains("position 4"));
    CHECK(program.getFormula() == "t"); // a failed compile leaves the program alone
    // Deep nesting beyond the fixed stack is refused rather than overflowing at play time.
    juce::String deep = "t";
    for (int i = 0; i < 80; i++) {
        deep = "t+(" + deep + ")";
    }
    CHECK(BytebeatProgram::compile(deep, program).failed());
}

TEST_CASE("Bytebeat plays its formula, transposed by the note", "[machines][4c]") {
    auto render = [](int note, const juce::String& formula) {
        MachineRig rig(Bytebeat::typeName);
        REQUIRE(rig.session.setMachineData(rig.machine, Bytebeat::dataFor(formula)).wasOk());
        rig.set("rate", 6000.0f);
        return rig.play({ { 0.0, 2.0, note, 0.9f } }, 0.5);
    };
    // "t" is a sawtooth at rate/256 Hz: 6000/256 = 23.4 Hz at middle C, an octave up at 72.
    const auto c4 = render(60, "t*8");
    const auto c5 = render(72, "t*8");
    CHECK_THAT(Analysis::pitch(c4, 4800, sampleRate, 8192, 100.0, 400.0), WithinRel(6000.0 * 8 / 256, 0.02));
    CHECK_THAT(Analysis::pitch(c5, 4800, sampleRate, 8192, 100.0, 800.0), WithinRel(2 * 6000.0 * 8 / 256, 0.02));

    MachineRig rig(Bytebeat::typeName);
    CHECK(rig.session.setMachineData(rig.machine, Bytebeat::dataFor("t*(")).failed());
    CHECK(rig.session.getMachineData(rig.machine).getProperty("formula").toString() == Bytebeat::defaultFormula);
}

// ---------------------------------------------------------------------------- Sampler

TEST_CASE("Sampler maps zones by key and repitches from the root", "[machines][4c]") {
    Sampler::Zone low;
    low.sample = sine(220.0, 1.0);
    low.rootNote = 57;
    low.lowKey = 0;
    low.highKey = 59;
    Sampler::Zone high;
    high.sample = sine(1000.0, 1.0);
    high.rootNote = 72;
    high.lowKey = 60;
    high.highKey = 127;

    MachineRig rig(Sampler::typeName);
    REQUIRE(rig.session.setMachineData(rig.machine, Sampler::dataFor({ low, high })).wasOk());
    const auto lowAudio = rig.play({ { 0.0, 1.0, 45, 0.9f } }, 0.3);   // an octave below the low root
    rig.render(48000);
    const auto highAudio = rig.play({ { 0.0, 1.0, 84, 0.9f } }, 0.3);  // an octave above the high root
    CHECK_THAT(Analysis::pitch(lowAudio, 2400, sampleRate), WithinRel(110.0, 0.01));
    CHECK_THAT(Analysis::pitch(highAudio, 2400, sampleRate, 4096, 500.0, 3000.0), WithinRel(2000.0, 0.01));
}

TEST_CASE("Sampler loops a zone while the note is held", "[machines][4c]") {
    Sampler::Zone zone;
    zone.sample = sine(440.0, 0.1); // a tenth of a second
    zone.rootNote = 69;
    zone.loop = true;
    zone.loopStart = 0.5;
    MachineRig rig(Sampler::typeName);
    REQUIRE(rig.session.setMachineData(rig.machine, Sampler::dataFor({ zone })).wasOk());
    const auto audio = rig.play({ { 0.0, 2.0, 69, 0.9f } }, 0.8);
    CHECK(Analysis::rms(audio, 0, 24000, 4800) > 0.05); // 0.5 s in: still sounding
}

TEST_CASE("Sampler's built-in sample is saved by name, imported ones as audio", "[machines][4c]") {
    Rig rig;
    const int m = rig.session.addMachine(Sampler::typeName);
    juce::MemoryOutputStream builtIn;
    REQUIRE(rig.session.saveToStream(builtIn).wasOk());
    CHECK(builtIn.toString().contains("builtIn=\"keys\""));
    CHECK_FALSE(builtIn.toString().contains("wav="));

    Sampler::Zone zone;
    zone.sample = sine(330.0, 0.2);
    REQUIRE(rig.session.setMachineData(m, Sampler::dataFor({ zone })).wasOk());
    juce::MemoryOutputStream imported;
    REQUIRE(rig.session.saveToStream(imported).wasOk());
    CHECK(imported.toString().contains("wav="));

    Rig other;
    juce::MemoryInputStream in(imported.getData(), imported.getDataSize(), false);
    REQUIRE(other.session.loadFromStream(in).wasOk());
    const auto data = other.session.getMachineData(0);
    REQUIRE(data.getNumChildren() == 1);
    CHECK(data.getChild(0).getChildWithName("Sample").getProperty("name").toString() == "sine 330");

    std::vector<Sampler::Zone> tooMany(Sampler::maxZones + 1, zone);
    CHECK(rig.session.setMachineData(m, Sampler::dataFor(tooMany)).failed());
}

// ---------------------------------------------------------------------------- Drums with samples

TEST_CASE("A drum slot can play an imported sample, and go back to the kit", "[machines][4c]") {
    MachineRig rig(DrumMachine::typeName);
    const auto kick = rig.play({ { 0.0, 0.25, 36, 1.0f } }, 0.3);
    CHECK(Analysis::centroid(kick, 0, sampleRate) < 500.0);

    auto data = DrumMachine::withSample(rig.session.getMachineData(rig.machine), 0, sine(2000.0, 0.3));
    REQUIRE(rig.session.setMachineData(rig.machine, data).wasOk());
    CHECK(rig.session.getMachineData(rig.machine).getNumChildren() == 1); // only the custom slot is stored
    rig.render(48000);
    const auto replaced = rig.play({ { 0.0, 0.25, 36, 1.0f } }, 0.3);
    CHECK_THAT(Analysis::pitch(replaced, 1200, sampleRate, 2048, 500.0, 4000.0), WithinRel(2000.0, 0.01));

    REQUIRE(rig.session.setMachineData(rig.machine, DrumMachine::withoutSample(data, 0)).wasOk());
    rig.render(48000);
    CHECK(Analysis::centroid(rig.play({ { 0.0, 0.25, 36, 1.0f } }, 0.3), 0, sampleRate) < 500.0);

    juce::ValueTree bad("Drums");
    juce::ValueTree slot("Slot");
    slot.setProperty("index", 9, nullptr);
    bad.appendChild(slot, nullptr);
    CHECK(rig.session.setMachineData(rig.machine, bad).failed());
}

// ---------------------------------------------------------------------------- Vocoder

TEST_CASE("Vocoder is silent without notes", "[machines][4c]") {
    MachineRig rig(Vocoder::typeName);
    rig.session.setPlaying(true);
    CHECK(analyse(rig.render(24000)).peak <= 0.0f);
}

TEST_CASE("Vocoder output follows the voice's spectrum", "[machines][4c]") {
    auto centroidWith = [](double voiceHz) {
        MachineRig rig(Vocoder::typeName);
        REQUIRE(rig.session.setMachineData(rig.machine, vocoderData(sine(voiceHz, 1.0))).wasOk());
        rig.set("noise", 0.0f);
        rig.set("sibilance", 0.0f);
        const auto audio = rig.play({ { 0.0, 2.0, 36, 0.9f } }, 0.5);
        return Analysis::centroid(audio, 9600, sampleRate);
    };
    const double low = centroidWith(250.0);
    const double high = centroidWith(3500.0);
    CAPTURE(low, high);
    CHECK(high > 4.0 * low);
}

TEST_CASE("Vocoder's built-in voice isn't saved, an imported one is", "[machines][4c]") {
    MachineRig rig(Vocoder::typeName);
    CHECK(rig.session.getMachineData(rig.machine).getNumChildren() == 0);
    REQUIRE(rig.session.setMachineData(rig.machine, vocoderData(sine(300.0, 0.5))).wasOk());
    CHECK(rig.session.getMachineData(rig.machine).getNumChildren() == 1);
}

// ---------------------------------------------------------------------------- Modular

TEST_CASE("Modular cables route sources to destinations", "[machines][4c]") {
    SECTION("envelope 2 to cutoff brightens the attack") {
        auto earlyCentroid = [](float amount) {
            MachineRig rig(Modular::typeName);
            rig.set("patch1Amount", amount);
            rig.set("patch2Amount", 0.0f);
            rig.set("env2Decay", 0.3f);
            return Analysis::centroid(rig.play({ { 0.0, 2.0, 45, 0.9f } }, 0.3), 480, sampleRate);
        };
        CHECK(earlyCentroid(1.0f) > 1.5 * earlyCentroid(0.0f));
    }
    SECTION("LFO 1 to pitch makes vibrato") {
        MachineRig rig(Modular::typeName);
        rig.set("osc1Shape", 3.0f);
        rig.set("oscMix", 0.0f);
        rig.set("cutoff", 20000.0f);
        rig.set("patch1Amount", 0.0f);
        rig.set("patch2Amount", 0.0f);
        rig.set("patch3Source", static_cast<float>(Modular::lfo1));
        rig.set("patch3Destination", static_cast<float>(Modular::osc1Pitch));
        rig.set("patch3Amount", 1.0f / 24.0f); // +-1 semitone
        rig.set("lfo1Rate", 1.0f);
        const auto audio = rig.play({ { 0.0, 4.0, 69, 0.9f } }, 1.2);
        const double up = Analysis::pitch(audio, 12000 - 1024, sampleRate, 2048);
        const double down = Analysis::pitch(audio, 36000 - 1024, sampleRate, 2048);
        CHECK(Analysis::semitonesBetween(up, 440.0) > 0.5);
        CHECK(Analysis::semitonesBetween(down, 440.0) < -0.5);
    }
    SECTION("amp at -1 silences") {
        MachineRig rig(Modular::typeName);
        rig.set("patch3Source", static_cast<float>(Modular::velocity));
        rig.set("patch3Destination", static_cast<float>(Modular::ampDestination));
        rig.set("patch3Amount", -1.0f);
        CHECK(analyse(rig.play({ { 0.0, 1.0, 57, 1.0f } }, 0.3)).peak < 1.0e-4f);
    }
}
