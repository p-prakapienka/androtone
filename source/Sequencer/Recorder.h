#pragma once

#include "StepSequencer.h"

#include <array>
#include <bitset>
#include <functional>

// Message-thread side of MIDI recording. It drains the sequencer's RecordEventQueue, turns note
// on/off pairs into notes (applying record quantization), and updates the clip being recorded so
// it can be seen — and, for overdubs, heard on the next pass — while recording. Each finished take
// is committed as one undo step.
class Recorder {
public:
    using Commit = std::function<void(int track, int slot, ClipData::Ptr before, ClipData::Ptr after)>;

    Recorder(StepSequencer& s, Commit commitTake) : sequencer(s), commit(std::move(commitTake)) {}

    // Record quantization in beats (0 = off).
    void setQuantization(double beats) { quantization = beats; }
    double getQuantization() const { return quantization; }

    // Session Record button: stops every recording, or starts one on each armed track — an
    // overdub into the clip it is playing, otherwise a new clip in the current scene's row (or the
    // next empty slot below).
    void toggleSessionRecord(int currentScene) {
        bool anyRecording = false;
        for (int t = 0; t < NUM_TRACKS; t++) {
            if (sequencer.isRecordingOrPending(t)) {
                sequencer.requestStopRecord(t);
                anyRecording = true;
            }
        }
        if (anyRecording) {
            return;
        }
        for (int t = 0; t < NUM_TRACKS; t++) {
            if (!sequencer.isArmed(t)) {
                continue;
            }
            const int playingClip = sequencer.getPlayingClip(t);
            if (playingClip >= 0 && sequencer.isPlaying()) {
                sequencer.requestRecord(t, playingClip, true);
                continue;
            }
            const int slot = findEmptySlot(t, juce::jmax(0, currentScene));
            if (slot >= 0) {
                recordIntoSlot(t, slot);
            }
        }
    }

    // Records a new clip into an empty slot (the slot's record button on an armed track).
    void recordIntoSlot(int track, int slot) {
        auto placeholder = ClipData::createEmpty(Clip::BEATS_PER_BAR);
        sequencer.getClipSettings(track, slot).reset();
        sequencer.setClipData(track, slot, placeholder);
        sequencer.requestRecord(track, slot, false);
    }

    // Drains pending events. Call regularly from the message thread.
    void drain() {
        RecordEvent event;
        while (sequencer.getRecordEvents().pop(event)) {
            handle(event);
        }
    }

private:
    struct Take {
        bool active = false;
        bool overdub = false;
        int slot = -1;
        ClipData::Ptr before; // slot content before the take (nullptr for a new clip)
        ClipData::Ptr working;
        std::array<double, 128> start {};
        std::array<int, 128> velocity {};
        std::bitset<128> held;
    };

    StepSequencer& sequencer;
    Commit commit;
    double quantization = 0.0;
    std::array<Take, NUM_TRACKS> takes;

    int findEmptySlot(int track, int from) const {
        for (int slot = from; slot < MAX_CLIPS; slot++) {
            if (!sequencer.hasClip(track, slot)) {
                return slot;
            }
        }
        return -1;
    }

    void handle(const RecordEvent& event) {
        const int t = event.track;
        if (t < 0 || t >= NUM_TRACKS) {
            return;
        }
        auto& take = takes[static_cast<size_t>(t)];

        switch (event.type) {
            case RecordEvent::Type::Started: {
                take = Take {};
                take.active = true;
                take.overdub = event.overdub;
                take.slot = event.slot;
                const auto current = sequencer.getClipData(t, event.slot);
                take.before = event.overdub ? current : nullptr;
                take.working = current != nullptr ? current : ClipData::createEmpty();
                break;
            }
            case RecordEvent::Type::NoteOn:
                if (take.active && event.velocity > 0) {
                    take.held.set(event.pitch);
                    take.start[event.pitch] = event.beat;
                    take.velocity[event.pitch] = event.velocity;
                    break;
                }
                [[fallthrough]]; // a noteOn with velocity 0 is a noteOff
            case RecordEvent::Type::NoteOff:
                if (take.active && take.held.test(event.pitch)) {
                    addNote(take, event.pitch, event.beat);
                    publish(t, take);
                }
                break;
            case RecordEvent::Type::Finished:
                if (!take.active) {
                    break;
                }
                for (int pitch = 0; pitch < 128; pitch++) {
                    if (take.held.test(static_cast<size_t>(pitch))) {
                        const double end = take.overdub ? take.start[static_cast<size_t>(pitch)] + Clip::STEP_LENGTH : event.beat;
                        addNote(take, pitch, end);
                    }
                }
                if (!take.overdub) {
                    auto finished = take.working->copy();
                    std::vector<MidiNote> kept;
                    for (const auto& note : finished->notes) {
                        if (note.start < event.beat - 1e-9) {
                            kept.push_back(note);
                        }
                    }
                    finished->notes = kept;
                    finished->setLength(event.beat);
                    finished->sanitise();
                    take.working = finished;
                }
                commit(t, take.slot, take.before, take.working);
                take = Take {};
                break;
            case RecordEvent::Type::ClipLaunched:
            case RecordEvent::Type::ClipStopped:
            case RecordEvent::Type::LogEnded:
                break; // arrangement events go through ArrangementLogger
        }
    }

    void addNote(Take& take, int pitch, double endBeat) {
        auto data = take.working->copy();
        double start = take.start[static_cast<size_t>(pitch)];
        double length = endBeat - start;
        if (length <= 0.0 && take.overdub) {
            length += data->getLoopLength(); // the note was held across the loop end
        }
        length = juce::jmax(1.0 / 64.0, length);
        if (quantization > 0.0) {
            start = std::round(start / quantization) * quantization;
            if (take.overdub && data->looping && start >= data->loopEnd - 1e-9) {
                start = data->loopStart;
            }
        }
        data->notes.push_back({ juce::jmax(0.0, start), length, pitch, take.velocity[static_cast<size_t>(pitch)], false });
        if (!take.overdub) {
            // Grow the new clip's markers (whole bars) so the take stays visible while recording.
            const double needed = std::ceil((start + length) / Clip::BEATS_PER_BAR) * Clip::BEATS_PER_BAR;
            if (needed > data->endMarker) {
                data->setLength(needed);
            }
        }
        data->sanitise();
        take.working = data;
        take.held.reset(static_cast<size_t>(pitch));
    }

    void publish(int track, Take& take) {
        sequencer.setClipData(track, take.slot, take.working);
    }
};

// Message-thread side of Arrangement Record: turns the session activity the sequencer logs
// (launch / stop at song beats) into arrangement clips. Clips appear as they close; the whole pass
// is committed as one undo step when logging ends.
class ArrangementLogger {
public:
    using Commit = std::function<void(ArrangementData::Ptr before, ArrangementData::Ptr after)>;

    ArrangementLogger(StepSequencer& s, Commit commitPass) : sequencer(s), commit(std::move(commitPass)) {}

    void drain() {
        RecordEvent event;
        while (sequencer.getArrangementEvents().pop(event)) {
            handle(event);
        }
    }

private:
    struct Open {
        bool active = false;
        int slot = -1;
        double start = 0.0;
        double offset = 0.0;
        ClipData::Ptr content;
    };

    StepSequencer& sequencer;
    Commit commit;
    ArrangementData::Ptr before;
    ArrangementData::Ptr working;
    std::array<Open, NUM_TRACKS> open;

    void handle(const RecordEvent& event) {
        if (working == nullptr) {
            before = sequencer.getArrangement();
            working = before != nullptr ? before : ArrangementData::Ptr(new ArrangementData());
        }
        const int t = event.track;
        switch (event.type) {
            case RecordEvent::Type::ClipLaunched:
                close(t, event.beat);
                open[static_cast<size_t>(t)] = { true, event.slot, event.beat, event.offset,
                                                 sequencer.getClipData(t, event.slot) };
                break;
            case RecordEvent::Type::ClipStopped:
                close(t, event.beat);
                break;
            case RecordEvent::Type::LogEnded:
                for (int track = 0; track < NUM_TRACKS; track++) {
                    close(track, event.beat);
                }
                if (working != before) {
                    commit(before, working);
                }
                before = nullptr;
                working = nullptr;
                break;
            default:
                break;
        }
    }

    void close(int t, double endBeat) {
        auto& entry = open[static_cast<size_t>(t)];
        if (!entry.active) {
            return;
        }
        entry.active = false;
        if (entry.content == nullptr || endBeat - entry.start < 1e-6) {
            return;
        }
        auto next = working->copy();
        next->place(t, { entry.start, endBeat - entry.start, entry.content, entry.offset, entry.slot });
        next->render();
        sequencer.setArrangement(next); // shows up while recording; heard after Back to Arrangement
        working = next;
    }
};
