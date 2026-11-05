#pragma once

#include "Clip.h"
#include "../Constants.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>

// One sequencer lane: a column of clip slots, at most one of which plays at a time.
//
// Threading: the request* / setRepeat* calls come from the UI thread and only write atomics. Every
// other mutating call runs on the audio thread, driven by StepSequencer::processBlock, which owns
// the timing (global grid, launch quantization) and tells the track when to launch, stop and step.
class Track {
public:
    static constexpr int NO_REQUEST = -1;
    static constexpr int STOP_REQUEST = -2;
    static constexpr int NOT_PLAYING = -1;

    // ---- UI thread -------------------------------------------------------------------------------

    // Queue a launch of `clipIndex`. Launching an empty slot queues a stop instead, like an empty
    // slot's stop button.
    void requestLaunch(int clipIndex) {
        if (clipIndex < 0 || clipIndex >= MAX_CLIPS) {
            return;
        }
        pendingRequest.store(clips[clipIndex].isEmpty() ? STOP_REQUEST : clipIndex);
    }

    void requestStop() {
        pendingRequest.store(STOP_REQUEST);
    }

    // Repeat launch mode: while set, the sequencer relaunches this clip on every quantization boundary.
    void setRepeatClip(int clipIndex) {
        repeatClip.store(clipIndex);
    }

    void clearRepeatClip(int clipIndex) {
        int expected = clipIndex;
        repeatClip.compare_exchange_strong(expected, NO_REQUEST);
    }

    int getPendingRequest() const {
        return pendingRequest.load();
    }

    int getPlayingClipIndex() const {
        return playingClip.load();
    }

    bool isPlayingClip() const {
        return playingClip.load() != NOT_PLAYING;
    }

    Clip& getClip(int index) {
        return clips[static_cast<size_t>(index)];
    }

    const Clip& getClip(int index) const {
        return clips[static_cast<size_t>(index)];
    }

    // ---- set-up (before playback) ----------------------------------------------------------------

    void setChannel(int newChannel) {
        channel = newChannel;
    }

    template<typename Container>
    void updateClip(const Container& notes, int clipIndex) {
        if (clipIndex < 0 || clipIndex >= MAX_CLIPS) {
            return;
        }
        clips[clipIndex].setNotes(notes);
    }

    // Selects the clip that plays when the transport starts, without any quantization.
    void armClip(int clipIndex) {
        const bool valid = clipIndex >= 0 && clipIndex < MAX_CLIPS && !clips[clipIndex].isEmpty();
        playingClip.store(valid ? clipIndex : NOT_PLAYING);
        pendingRequest.store(NO_REQUEST);
        restart(0.0);
    }

    // ---- audio thread ----------------------------------------------------------------------------

    int takePendingRequest() {
        return pendingRequest.exchange(NO_REQUEST);
    }

    int getRepeatClip() const {
        return repeatClip.load();
    }

    // Applies a request taken from takePendingRequest() (or a scene / follow-action target):
    // a clip index launches that clip, STOP_REQUEST stops the track, NO_REQUEST does nothing.
    void apply(int request, juce::MidiBuffer& midi, int samplePosition, double phase) {
        if (request == STOP_REQUEST) {
            stop(midi, samplePosition);
        } else if (request >= 0 && request < MAX_CLIPS) {
            if (clips[request].isEmpty()) {
                stop(midi, samplePosition);
            } else {
                launch(request, phase);
            }
        }
    }

    // Starts `clipIndex` from its first step. `phase` is the number of samples until that first
    // step fires: 0 (or a small negative fraction carried over from the global grid) means now.
    // Notes still sounding from the previous clip ring out to their natural length.
    void launch(int clipIndex, double phase) {
        playingClip.store(clipIndex);
        restart(phase);
    }

    void stop(juce::MidiBuffer& midi, int samplePosition) {
        releaseAllNotes(midi, samplePosition);
        playingClip.store(NOT_PLAYING);
    }

    // Transport stopped: silence everything and rewind, but keep the clip selected so it starts
    // again with the transport.
    void onTransportStop(juce::MidiBuffer& midi, int samplePosition) {
        releaseAllNotes(midi, samplePosition);
        restart(0.0);
    }

    void restart(double phase) {
        stepIndex = 0;
        stepsSinceLaunch = 0;
        followFired = false;
        samplesToNextStep = phase;
    }

    bool isStepDue() const {
        return isPlayingClip() && samplesToNextStep <= 0.0;
    }

    double getSamplesToNextStep() const {
        return samplesToNextStep;
    }

    // True when the next step to fire wraps the clip back to its start (one full pass completed).
    bool isAboutToLoop() const {
        const int clipIndex = playingClip.load();
        return clipIndex != NOT_PLAYING && stepsSinceLaunch > 0 && stepIndex >= clips[clipIndex].getNumSteps();
    }

    int getPlayingClipLength() const {
        const int clipIndex = playingClip.load();
        return clipIndex == NOT_PLAYING ? 0 : clips[clipIndex].getNumSteps();
    }

    // Fires the step that is due at `samplePosition`: runs the follow action if its time has come,
    // then plays the current step of whatever clip is now playing.
    void fireStep(juce::MidiBuffer& midi, int samplePosition, double samplesPerBeat, double samplesPerStep,
                  juce::Random& random) {
        flushDueNoteOffs(midi, samplePosition, samplesPerBeat);

        int clipIndex = playingClip.load();
        if (clipIndex == NOT_PLAYING) {
            return;
        }

        const ClipSettings& settings = clips[clipIndex].settings;
        if (settings.followEnabled.load() && !followFired && stepsSinceLaunch >= clips[clipIndex].getFollowSteps()) {
            followFired = true;
            const int target = resolveFollowAction(clipIndex, random);

            if (target == STOP_REQUEST) {
                stop(midi, samplePosition);
                return;
            }
            if (target >= 0) {
                // Follow actions fire exactly on their time, bypassing launch quantization.
                launch(target, samplesToNextStep);
                clipIndex = target;
            }
        }

        const Clip& clip = clips[clipIndex];
        if (clip.getNumSteps() == 0) {
            stop(midi, samplePosition);
            return;
        }
        if (stepIndex >= clip.getNumSteps()) {
            stepIndex = 0;
        }

        const Note& note = clip.getStep(stepIndex);
        if (note.velocity > 0) {
            noteOn(midi, samplePosition, note, samplesPerBeat);
        }

        stepIndex++;
        stepsSinceLaunch++;
        samplesToNextStep += samplesPerStep;
    }

    // Moves time forward by `numSamples` from `samplePosition`, emitting the noteOffs that fall
    // inside that span (rounded to the nearest sample). A noteOff landing on the span's end is left
    // for the next span, so that at a step boundary it is flushed before that step's noteOn.
    void advance(juce::MidiBuffer& midi, int samplePosition, int numSamples, double samplesPerBeat) {
        for (int i = 0; i < numSounding;) {
            const double remainingSamples = sounding[i].remainingBeats * samplesPerBeat;

            if (remainingSamples + 0.5 < static_cast<double>(numSamples)) {
                const int offset = juce::jlimit(0, numSamples - 1, static_cast<int>(remainingSamples + 0.5));
                midi.addEvent(juce::MidiMessage::noteOff(channel, sounding[i].number), samplePosition + offset);
                removeSounding(i);
            } else {
                sounding[i].remainingBeats -= numSamples / samplesPerBeat;
                i++;
            }
        }

        if (isPlayingClip()) {
            samplesToNextStep -= numSamples;
        }
    }

    void releaseAllNotes(juce::MidiBuffer& midi, int samplePosition) {
        for (int i = 0; i < numSounding; i++) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, sounding[i].number), samplePosition);
        }
        numSounding = 0;
    }

private:
    struct SoundingNote {
        int number = 0;
        double remainingBeats = 0.0;
    };

    static constexpr int MAX_SOUNDING = 32;

    int channel = 1;
    std::array<Clip, MAX_CLIPS> clips;

    std::atomic<int> playingClip { NOT_PLAYING };
    std::atomic<int> pendingRequest { NO_REQUEST };
    std::atomic<int> repeatClip { NO_REQUEST };

    int stepIndex = 0;        // next step of the playing clip to fire
    int stepsSinceLaunch = 0; // steps fired since the clip was (re)launched; drives follow actions
    bool followFired = false;
    double samplesToNextStep = 0.0;

    std::array<SoundingNote, MAX_SOUNDING> sounding;
    int numSounding = 0;

    void noteOn(juce::MidiBuffer& midi, int samplePosition, const Note& note, double samplesPerBeat) {
        // Retriggering a pitch that is still held: end the old note first so the noteOff doesn't
        // land after (and cut) the new one.
        for (int i = 0; i < numSounding; i++) {
            if (sounding[i].number == note.number) {
                midi.addEvent(juce::MidiMessage::noteOff(channel, note.number), samplePosition);
                removeSounding(i);
                break;
            }
        }
        if (numSounding == MAX_SOUNDING) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, sounding[0].number), samplePosition);
            removeSounding(0);
        }

        midi.addEvent(
            juce::MidiMessage::noteOn(channel, note.number, static_cast<juce::uint8>(note.velocity)),
            samplePosition
        );
        sounding[numSounding++] = { note.number, juce::jmax(note.length, 1.0 / samplesPerBeat) };
    }

    // NoteOffs whose time rounds to now, emitted before a step's noteOns.
    void flushDueNoteOffs(juce::MidiBuffer& midi, int samplePosition, double samplesPerBeat) {
        for (int i = 0; i < numSounding;) {
            if (sounding[i].remainingBeats * samplesPerBeat < 0.5) {
                midi.addEvent(juce::MidiMessage::noteOff(channel, sounding[i].number), samplePosition);
                removeSounding(i);
            } else {
                i++;
            }
        }
    }

    void removeSounding(int index) {
        sounding[index] = sounding[numSounding - 1];
        numSounding--;
    }

    // Picks action A or B by their relative chances and turns it into a launch target: a clip
    // index, STOP_REQUEST, or NO_REQUEST for "keep playing".
    int resolveFollowAction(int current, juce::Random& random) const {
        const ClipSettings& settings = clips[current].settings;
        const int chanceA = juce::jmax(0, settings.chanceA.load());
        const int chanceB = juce::jmax(0, settings.chanceB.load());
        const int total = chanceA + chanceB;
        if (total <= 0) {
            return NO_REQUEST;
        }

        const FollowAction action = random.nextInt(total) < chanceA ? settings.followActionA.load()
                                                                    : settings.followActionB.load();

        // The group is the run of non-empty slots around the current clip.
        int first = current;
        while (first > 0 && !clips[first - 1].isEmpty()) {
            first--;
        }
        int last = current;
        while (last < MAX_CLIPS - 1 && !clips[last + 1].isEmpty()) {
            last++;
        }
        const int size = last - first + 1;

        switch (action) {
            case FollowAction::None:
                return NO_REQUEST;
            case FollowAction::Stop:
                return STOP_REQUEST;
            case FollowAction::Again:
                return current;
            case FollowAction::Previous:
                return current == first ? last : current - 1;
            case FollowAction::Next:
                return current == last ? first : current + 1;
            case FollowAction::First:
                return first;
            case FollowAction::Last:
                return last;
            case FollowAction::Any:
                return first + random.nextInt(size);
            case FollowAction::Other:
                if (size == 1) {
                    return current;
                }
                {
                    const int pick = first + random.nextInt(size - 1);
                    return pick >= current ? pick + 1 : pick;
                }
        }
        return NO_REQUEST;
    }
};
