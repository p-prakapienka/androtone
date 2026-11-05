#pragma once

#include "Clip.h"
#include "../Constants.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

// Global groove applied at playback: off-beat 16ths are delayed by `timing` (0..1, where 1 puts
// them on the triplet position) and their velocity scaled down by up to 30 % × `velocity` (0..1).
// Notes on the 8th-note grid are never moved.
struct Groove {
    double timing = 0.0;
    double velocity = 0.0;

    static constexpr double MAX_DELAY = Clip::STEP_LENGTH / 3.0;

    static bool isOffBeat16th(double start) {
        const double phase = std::fmod(start, 2.0 * Clip::STEP_LENGTH);
        return std::abs(phase - Clip::STEP_LENGTH) < 1.0 / 128.0;
    }

    double delayFor(double start) const {
        return isOffBeat16th(start) ? timing * MAX_DELAY : 0.0;
    }

    int velocityFor(double start, int velocity) const {
        if (!isOffBeat16th(start) || this->velocity <= 0.0) {
            return velocity;
        }
        return juce::jlimit(1, 127, static_cast<int>(std::round(velocity * (1.0 - 0.3 * this->velocity))));
    }
};

// One sequencer lane: a column of clip slots, at most one of which plays at a time.
//
// Threading: request* / setRepeat* come from the UI thread and only write atomics. Every other
// mutating call runs on the audio thread, driven by StepSequencer::processBlock, which owns the
// timing (song position, launch quantization) and splits blocks at the events tracks report.
class Track {
public:
    static constexpr int NO_REQUEST = -1;
    static constexpr int STOP_REQUEST = -2;
    static constexpr int NOT_PLAYING = -1;

    // ---- UI thread -------------------------------------------------------------------------------

    // Queue a launch of `clipIndex`. Launching an empty slot queues a stop instead.
    void requestLaunch(int clipIndex) {
        if (clipIndex < 0 || clipIndex >= MAX_CLIPS) {
            return;
        }
        pendingRequest.store(clips[clipIndex].hasClip() ? clipIndex : STOP_REQUEST);
    }

    void requestStop() {
        pendingRequest.store(STOP_REQUEST);
    }

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

    // Playback position inside the playing clip, in beats (for the UI playhead).
    double getClipPosition() const {
        return clipPositionForUi.load();
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

    int getChannel() const {
        return channel;
    }

    // Selects the clip that plays when the transport starts, without quantization.
    void armClip(int clipIndex) {
        const bool valid = clipIndex >= 0 && clipIndex < MAX_CLIPS && clips[clipIndex].hasClip();
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

    // Applies a launch target: a clip index launches it (an empty slot stops), STOP_REQUEST stops,
    // NO_REQUEST does nothing. `lateBy` is how far (beats) past the launch point we already are.
    void apply(int request, juce::MidiBuffer& midi, int samplePosition, double lateBy) {
        if (request == STOP_REQUEST) {
            stop(midi, samplePosition);
        } else if (request >= 0 && request < MAX_CLIPS) {
            if (clips[request].hasClip()) {
                launch(request, lateBy);
            } else {
                stop(midi, samplePosition);
            }
        }
    }

    // Starts `clipIndex` from its start marker. Notes still sounding from the previous clip ring
    // out to their own length.
    void launch(int clipIndex, double lateBy) {
        playingClip.store(clipIndex);
        restart(lateBy);
    }

    void stop(juce::MidiBuffer& midi, int samplePosition) {
        releaseAllNotes(midi, samplePosition);
        playingClip.store(NOT_PLAYING);
        playingData = nullptr;
    }

    // Transport stopped: silence and rewind, but keep the clip selected so it starts again with
    // the transport.
    void onTransportStop(juce::MidiBuffer& midi, int samplePosition) {
        releaseAllNotes(midi, samplePosition);
        restart(0.0);
    }

    void restart(double lateBy) {
        elapsed = juce::jmax(0.0, lateBy);
        followFired = false;
        needsStart = true;
    }

    // Beats from now until this track needs the block split: its follow action or the end of
    // its current pass. Large when there's nothing pending.
    double beatsToNextEvent() {
        refreshData();
        if (playingData == nullptr) {
            return NO_EVENT;
        }
        ensureStarted();
        double until = playingData->getPlayEnd() - clipPos;
        const ClipSettings& settings = clips[playingClip.load()].settings;
        if (settings.followEnabled.load() && !followFired) {
            until = juce::jmin(until, Clip::getFollowBeats(*playingData, settings) - elapsed);
        }
        return juce::jmax(0.0, until);
    }

    // True right after a looping clip wrapped (or a one-shot clip ended) at the end of the
    // previous chunk — used by song mode to chain scenes on the longest clip's pass.
    bool justCompletedPass() const {
        return completedPass;
    }

    // Clip position `offset` beats after the current one, wrapped into the loop (for overdubs).
    double clipPositionAfter(double offset) const {
        if (playingData == nullptr) {
            return 0.0;
        }
        double position = clipPos + offset;
        const double end = playingData->getPlayEnd();
        if (position >= end && playingData->looping) {
            position = playingData->loopStart + std::fmod(position - end, playingData->getLoopLength());
        }
        return position;
    }

    double getPassLength() const {
        return playingData == nullptr ? 0.0 : playingData->getLoopLength();
    }

    // Renders `numSamples` from `samplePosition`: follow action (if due now), noteOns of the clip
    // window, noteOffs of sounding notes, loop wraps and one-shot ends.
    void process(juce::MidiBuffer& midi, int samplePosition, int numSamples, double samplesPerBeat,
                 const Groove& groove, juce::Random& random) {
        const double halfSample = 0.5 / samplesPerBeat;
        const double chunkBeats = numSamples / samplesPerBeat;
        completedPass = false;

        refreshData();
        if (playingData != nullptr) {
            ensureStarted();
            runFollowActionIfDue(midi, samplePosition, halfSample, random);
        }

        // 1. noteOffs of notes already sounding that end inside this chunk.
        emitNoteOffs(midi, samplePosition, numSamples, samplesPerBeat, halfSample, chunkBeats);

        // 2. noteOns from the clip, walking the window and wrapping at the loop end.
        if (playingData != nullptr) {
            double offset = 0.0;
            double remaining = chunkBeats;
            int guard = 0;

            while (playingData != nullptr && remaining > 1e-12 && guard++ < 64) {
                const double segmentEnd = playingData->getPlayEnd();
                const double segment = juce::jmax(0.0, juce::jmin(remaining, segmentEnd - clipPos));

                emitNoteOns(midi, samplePosition, numSamples, samplesPerBeat, halfSample, offset,
                            clipPos, clipPos + segment, groove);

                clipPos += segment;
                offset += segment;
                remaining -= segment;

                if (clipPos >= segmentEnd - 1e-9) {
                    completedPass = remaining <= 1e-9;
                    if (playingData->looping) {
                        clipPos = playingData->loopStart;
                    } else {
                        // One-shot clip finished: stop, but let its notes ring to their own end.
                        playingClip.store(NOT_PLAYING);
                        playingData = nullptr;
                    }
                }
            }
        }

        // 3. noteOffs of notes started in this chunk that also end inside it.
        emitNoteOffs(midi, samplePosition, numSamples, samplesPerBeat, halfSample, chunkBeats);

        elapsed += chunkBeats;
        timeline += chunkBeats;
        clipPositionForUi.store(clipPos);
    }

    void releaseAllNotes(juce::MidiBuffer& midi, int samplePosition) {
        for (int i = 0; i < numSounding; i++) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, sounding[static_cast<size_t>(i)].pitch), samplePosition);
        }
        numSounding = 0;
        timeline = 0.0;
    }

    static constexpr double NO_EVENT = 1.0e9;

private:
    struct SoundingNote {
        int pitch = 0;
        double endBeat = 0.0; // on this track's `timeline`
    };

    static constexpr int MAX_SOUNDING = 64;

    int channel = 1;
    std::array<Clip, MAX_CLIPS> clips;

    std::atomic<int> playingClip { NOT_PLAYING };
    std::atomic<int> pendingRequest { NO_REQUEST };
    std::atomic<int> repeatClip { NO_REQUEST };
    std::atomic<double> clipPositionForUi { 0.0 };

    ClipData::Ptr playingData; // snapshot of the playing clip; refreshed every chunk
    double clipPos = 0.0;      // position inside the clip (beats)
    double elapsed = 0.0;      // beats since the clip was launched (drives follow actions)
    double timeline = 0.0;     // monotonic beats while the transport runs (drives noteOffs)
    bool followFired = false;
    bool needsStart = true;
    bool completedPass = false;

    std::array<SoundingNote, MAX_SOUNDING> sounding;
    int numSounding = 0;

    void refreshData() {
        const int index = playingClip.load();
        if (index == NOT_PLAYING) {
            playingData = nullptr;
            return;
        }
        playingData = clips[static_cast<size_t>(index)].getData();
        if (playingData == nullptr) {
            playingClip.store(NOT_PLAYING); // the clip was deleted while playing
        }
    }

    void ensureStarted() {
        if (needsStart && playingData != nullptr) {
            clipPos = playingData->startMarker + elapsed;
            needsStart = false;
            // A launch that's late by more than one pass is not expected; keep inside the clip.
            if (clipPos >= playingData->getPlayEnd()) {
                clipPos = playingData->looping ? playingData->loopStart : playingData->startMarker;
            }
        }
        if (playingData != nullptr && clipPos > playingData->getPlayEnd()) {
            // The clip was shortened under the playhead.
            clipPos = playingData->looping ? playingData->loopStart : playingData->getPlayEnd();
        }
    }

    void runFollowActionIfDue(juce::MidiBuffer& midi, int samplePosition, double halfSample, juce::Random& random) {
        const int index = playingClip.load();
        const ClipSettings& settings = clips[static_cast<size_t>(index)].settings;
        if (!settings.followEnabled.load() || followFired) {
            return;
        }
        if (elapsed < Clip::getFollowBeats(*playingData, settings) - halfSample) {
            return;
        }

        followFired = true;
        const int target = resolveFollowAction(index, random);
        if (target == STOP_REQUEST) {
            stop(midi, samplePosition);
        } else if (target >= 0) {
            // Follow actions fire exactly on time, bypassing launch quantization.
            launch(target, 0.0);
            refreshData();
            ensureStarted();
        }
    }

    void emitNoteOns(juce::MidiBuffer& midi, int samplePosition, int numSamples, double samplesPerBeat,
                     double halfSample, double chunkOffset, double from, double to, const Groove& groove) {
        // Window shifted by half a sample so an event lands in exactly one chunk (rounding).
        const double lo = from - halfSample;
        const double hi = to - halfSample;
        const double regionEnd = playingData->getPlayEnd();

        for (const auto& note : playingData->notes) {
            if (note.muted || note.velocity <= 0 || note.start >= regionEnd) {
                continue;
            }
            const double start = note.start + groove.delayFor(note.start);
            if (start < lo || start >= hi) {
                continue;
            }
            const double beatInChunk = chunkOffset + (start - from);
            const int offset = juce::jlimit(0, numSamples - 1, static_cast<int>(std::lround(beatInChunk * samplesPerBeat)));
            noteOn(midi, samplePosition + offset, note.pitch, groove.velocityFor(note.start, note.velocity),
                   timeline + beatInChunk + juce::jmax(note.length, 2.0 * halfSample));
        }
    }

    void emitNoteOffs(juce::MidiBuffer& midi, int samplePosition, int numSamples, double samplesPerBeat,
                      double halfSample, double chunkBeats) {
        for (int i = 0; i < numSounding;) {
            auto& note = sounding[static_cast<size_t>(i)];
            const double fromChunkStart = note.endBeat - timeline;
            if (fromChunkStart < chunkBeats - halfSample) {
                const int offset = juce::jlimit(0, numSamples - 1,
                                                static_cast<int>(std::lround(juce::jmax(0.0, fromChunkStart) * samplesPerBeat)));
                midi.addEvent(juce::MidiMessage::noteOff(channel, note.pitch), samplePosition + offset);
                removeSounding(i);
            } else {
                i++;
            }
        }
    }

    void noteOn(juce::MidiBuffer& midi, int samplePosition, int pitch, int velocity, double endBeat) {
        // Retriggering a pitch that is still held: end the old note first.
        for (int i = 0; i < numSounding; i++) {
            if (sounding[static_cast<size_t>(i)].pitch == pitch) {
                midi.addEvent(juce::MidiMessage::noteOff(channel, pitch), samplePosition);
                removeSounding(i);
                break;
            }
        }
        if (numSounding == MAX_SOUNDING) {
            midi.addEvent(juce::MidiMessage::noteOff(channel, sounding[0].pitch), samplePosition);
            removeSounding(0);
        }
        midi.addEvent(juce::MidiMessage::noteOn(channel, pitch, static_cast<juce::uint8>(juce::jlimit(1, 127, velocity))),
                      samplePosition);
        sounding[static_cast<size_t>(numSounding++)] = { pitch, endBeat };
    }

    void removeSounding(int index) {
        sounding[static_cast<size_t>(index)] = sounding[static_cast<size_t>(numSounding - 1)];
        numSounding--;
    }

    // Picks action A or B by their relative chances and turns it into a launch target: a clip
    // index, STOP_REQUEST, or NO_REQUEST for "keep playing".
    int resolveFollowAction(int current, juce::Random& random) const {
        const ClipSettings& settings = clips[static_cast<size_t>(current)].settings;
        const int chanceA = juce::jmax(0, settings.chanceA.load());
        const int chanceB = juce::jmax(0, settings.chanceB.load());
        const int total = chanceA + chanceB;
        if (total <= 0) {
            return NO_REQUEST;
        }

        const FollowAction action = random.nextInt(total) < chanceA ? settings.followActionA.load()
                                                                    : settings.followActionB.load();

        int first = current;
        while (first > 0 && clips[static_cast<size_t>(first - 1)].hasClip()) {
            first--;
        }
        int last = current;
        while (last < MAX_CLIPS - 1 && clips[static_cast<size_t>(last + 1)].hasClip()) {
            last++;
        }
        const int size = last - first + 1;

        switch (action) {
            case FollowAction::None: return NO_REQUEST;
            case FollowAction::Stop: return STOP_REQUEST;
            case FollowAction::Again: return current;
            case FollowAction::Previous: return current == first ? last : current - 1;
            case FollowAction::Next: return current == last ? first : current + 1;
            case FollowAction::First: return first;
            case FollowAction::Last: return last;
            case FollowAction::Any: return first + random.nextInt(size);
            case FollowAction::Other: {
                if (size == 1) {
                    return current;
                }
                const int pick = first + random.nextInt(size - 1);
                return pick >= current ? pick + 1 : pick;
            }
        }
        return NO_REQUEST;
    }
};
