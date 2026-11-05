#pragma once

#include <juce_core/juce_core.h>
#include <array>

// What the audio thread reports about a recording. Positions are in clip beats.
struct RecordEvent {
    enum class Type : juce::uint8 { Started, NoteOn, NoteOff, Finished };

    Type type = Type::NoteOn;
    juce::int8 track = 0;
    juce::int16 slot = 0;
    juce::uint8 pitch = 0;
    juce::uint8 velocity = 0;
    bool overdub = false;
    double beat = 0.0; // note time; for Finished on a new clip: the recorded length
};

// Single-producer (audio thread) / single-consumer (message thread) queue of RecordEvents.
// push() never allocates or blocks; a full queue drops the event.
class RecordEventQueue {
public:
    bool push(const RecordEvent& event) {
        int start1, size1, start2, size2;
        fifo.prepareToWrite(1, start1, size1, start2, size2);
        if (size1 + size2 < 1) {
            return false;
        }
        buffer[static_cast<size_t>(size1 > 0 ? start1 : start2)] = event;
        fifo.finishedWrite(1);
        return true;
    }

    bool pop(RecordEvent& event) {
        int start1, size1, start2, size2;
        fifo.prepareToRead(1, start1, size1, start2, size2);
        if (size1 + size2 < 1) {
            return false;
        }
        event = buffer[static_cast<size_t>(size1 > 0 ? start1 : start2)];
        fifo.finishedRead(1);
        return true;
    }

private:
    static constexpr int CAPACITY = 2048;
    juce::AbstractFifo fifo { CAPACITY };
    std::array<RecordEvent, CAPACITY> buffer {};
};
