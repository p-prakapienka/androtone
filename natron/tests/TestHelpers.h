#pragma once

// Shared test helpers: event recording and headless renderers for the player and the engine.
// Tests change state between render() calls, so changes land on block boundaries exactly as they
// would from the UI thread.

#include "core/Engine.h"
#include "core/Session.h"
#include "core/PatternPlayer.h"
#include "core/Transport.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdint>
#include <vector>

struct RecordedEvent {
    int64_t sample;
    bool isNoteOn;
    int pitch;

    bool operator==(const RecordedEvent& other) const {
        return sample == other.sample && isNoteOn == other.isNoteOn && pitch == other.pitch;
    }
};

inline std::ostream& operator<<(std::ostream& os, const RecordedEvent& e) {
    return os << (e.isNoteOn ? "on " : "off ") << e.pitch << " @" << e.sample;
}

inline RecordedEvent on(int64_t sample, int pitch) { return { sample, true, pitch }; }
inline RecordedEvent off(int64_t sample, int pitch) { return { sample, false, pitch }; }

// Drives a PatternPlayer against a Transport the way the engine does: tempo changes apply at the
// start of the next block, then the transport advances.
class PlayerRenderer {
public:
    explicit PlayerRenderer(double sampleRate = 48000.0, double tempo = 120.0) {
        transport.prepare(sampleRate);
        transport.setTempo(tempo);
    }

    void render(const Pattern& pattern, int64_t numSamples, int blockSize = 512) {
        juce::MidiBuffer midi;
        while (numSamples > 0) {
            const int thisBlock = static_cast<int>(juce::jmin<int64_t>(blockSize, numSamples));
            midi.clear();
            const int64_t from = transport.getPosition();
            player.process(pattern, 0.0, transport, from, from + thisBlock, from, midi);
            record(midi);
            transport.advance(thisBlock);
            numSamples -= thisBlock;
        }
    }

    void setTempo(double bpm) { transport.setTempo(bpm); }

    void releaseAll() {
        juce::MidiBuffer midi;
        player.releaseAll(midi, 0);
        record(midi);
    }

    std::vector<int64_t> noteOnSamples() const {
        std::vector<int64_t> result;
        for (const auto& e : events) {
            if (e.isNoteOn) {
                result.push_back(e.sample);
            }
        }
        return result;
    }

    Transport transport;
    PatternPlayer player;
    std::vector<RecordedEvent> events;

private:
    void record(const juce::MidiBuffer& midi) {
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOnOrOff()) {
                events.push_back({ transport.getPosition() + metadata.samplePosition, message.isNoteOn(),
                                   message.getNoteNumber() });
            }
        }
    }
};

class EngineRenderer {
public:
    explicit EngineRenderer(Engine& engineToRender, double sampleRateToUse = 48000.0, int blockSizeToUse = 512) :
        engine(engineToRender), sampleRate(sampleRateToUse), blockSize(blockSizeToUse) {
        engine.prepare(sampleRate, blockSize);
    }

    juce::AudioBuffer<float> render(int64_t numSamples, int hostBlockSize = -1) {
        const int block = hostBlockSize > 0 ? hostBlockSize : blockSize;
        juce::AudioBuffer<float> out(2, static_cast<int>(numSamples));
        juce::AudioBuffer<float> chunk(2, block);
        for (int written = 0; written < numSamples;) {
            const int thisBlock = static_cast<int>(juce::jmin<int64_t>(block, numSamples - written));
            chunk.setSize(2, thisBlock, false, false, true);
            engine.process(chunk);
            for (int ch = 0; ch < 2; ch++) {
                out.copyFrom(ch, written, chunk, ch, 0, thisBlock);
            }
            written += thisBlock;
        }
        return out;
    }

    int64_t seconds(double s) const { return static_cast<int64_t>(s * sampleRate); }

private:
    Engine& engine;
    double sampleRate;
    int blockSize;
};

// An engine, a session driving it, and an offline renderer — the whole app minus the UI.
struct Rig {
    explicit Rig(double sampleRate = 48000.0, int blockSize = 512) :
        renderer(engine, sampleRate, blockSize) {}

    Engine engine;
    Session session { engine };
    EngineRenderer renderer; // declared last: its constructor prepares the engine

    juce::AudioBuffer<float> render(int64_t numSamples, int hostBlockSize = -1) {
        return renderer.render(numSamples, hostBlockSize);
    }
};

struct AudioStats {
    float peak = 0.0f;
    double rms = 0.0;
    bool allFinite = true;
    int subnormals = 0;
};

inline AudioStats analyse(const juce::AudioBuffer<float>& buffer, int start = 0, int length = -1) {
    if (length < 0) {
        length = buffer.getNumSamples() - start;
    }
    AudioStats stats;
    double sumSquares = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ch++) {
        const float* data = buffer.getReadPointer(ch, start);
        for (int i = 0; i < length; i++) {
            const float x = data[i];
            if (!std::isfinite(x)) {
                stats.allFinite = false;
                continue;
            }
            if (std::fpclassify(x) == FP_SUBNORMAL) {
                stats.subnormals++;
            }
            stats.peak = std::max(stats.peak, std::abs(x));
            sumSquares += static_cast<double>(x) * x;
        }
    }
    stats.rms = std::sqrt(sumSquares / juce::jmax(1, length * buffer.getNumChannels()));
    return stats;
}
