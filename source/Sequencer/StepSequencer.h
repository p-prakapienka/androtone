#pragma once

#include "Clip.h"
#include "Track.h"
#include "SceneManager.h"
#include "RecordEvents.h"
#include "Arrangement.h"
#include "../Constants.h"
#include "../Project/Projects.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>


// Drives all tracks from one global step grid (16th notes, counted from transport start).
//
// Launches and stops requested from the UI are queued and take effect on the next launch
// quantization boundary of that grid, so clips started at different moments stay in time with each
// other. Follow actions and song-mode scene chaining fire exactly on their own step instead.
class StepSequencer {
public:
    // Launch quantization choices, in steps of the global grid. 0 means "none": queued launches apply
    // at the start of the next audio block.
    static constexpr std::array<int, 9> QUANTIZATION_STEPS { 0, 1, 2, 4, 8, 16, 32, 64, 128 };
    static constexpr int DEFAULT_QUANTIZATION_INDEX = 5; // 1 bar
    static constexpr int MIN_SCENES = 8;                 // rows shown even if the project has fewer

    StepSequencer() {
        for (int trackIndex = 0; trackIndex < NUM_TRACKS; trackIndex++) {
            tracks[trackIndex].setChannel(trackIndex + 1);
        }

        loadProject(ProjectPresets::defaultProject);
    }

    void prepareToPlay(double sampleRate) {
        currentSampleRate = sampleRate;
    }

    // Loads clip content and settings, then arms scene 1 so the transport starts with it.
    // Must not run concurrently with processBlock.
    void loadProject(const Project& project) {
        setTempo(project.tempo);

        numActiveTracks = juce::jmin(static_cast<int>(project.tracks.size()), NUM_TRACKS);
        numActiveClips = 0;

        for (int trackIndex = 0; trackIndex < NUM_TRACKS; trackIndex++) {
            Track& track = tracks[static_cast<size_t>(trackIndex)];
            for (int clipIndex = 0; clipIndex < MAX_CLIPS; clipIndex++) {
                track.getClip(clipIndex).clear();
                track.getClip(clipIndex).settings.reset();
            }
        }

        for (int trackIndex = 0; trackIndex < numActiveTracks; trackIndex++) {
            const auto& projectTrack = project.tracks[static_cast<size_t>(trackIndex)];
            const int numClips = juce::jmin(static_cast<int>(projectTrack.clips.size()), MAX_CLIPS);
            numActiveClips = juce::jmax(numActiveClips, numClips);
            Track& track = tracks[static_cast<size_t>(trackIndex)];

            for (int clipIndex = 0; clipIndex < numClips; clipIndex++) {
                const auto& projectClip = projectTrack.clips[static_cast<size_t>(clipIndex)];
                if (projectClip.isEmptySlot()) {
                    continue;
                }

                ClipData::Ptr data = ClipData::createEmpty(projectClip.getLength());
                data->name = projectClip.name;
                data->looping = projectClip.looping;
                for (const auto& projectNote : projectClip.notes) {
                    if (projectNote.velocity > 0) {
                        data->notes.push_back({ projectNote.start, projectNote.length, projectNote.noteNumber,
                                                projectNote.velocity, false });
                    }
                }
                data->sanitise();
                track.getClip(clipIndex).setData(data);

                ClipSettings& settings = track.getClip(clipIndex).settings;
                settings.launchMode.store(projectClip.launchMode);
                settings.followEnabled.store(projectClip.followEnabled);
                settings.followActionA.store(projectClip.followActionA);
                settings.followActionB.store(projectClip.followActionB);
                settings.chanceA.store(projectClip.chanceA);
                settings.chanceB.store(projectClip.chanceB);
                settings.followLinked.store(projectClip.followLinked);
                settings.followMultiplier.store(projectClip.followMultiplier);
                settings.followTimeSteps.store(projectClip.followTimeSteps);
            }
        }

        numActiveClips = juce::jmax(numActiveClips, MIN_SCENES);
        sceneManager.setNumScenes(numActiveClips);
        sceneManager.cancelPending();
        sceneManager.setCurrentScene(0);
        for (auto& track : tracks) {
            track.armClip(0);
        }
    }

    // Clears everything before a saved set is restored: no clips, nothing playing (tracks follow
    // the arrangement, as when a set is opened), empty arrangement. Call with processing suspended.
    void resetForSetLoad(int tracksInSet, int scenesInSet) {
        playing.store(false);
        numActiveTracks = juce::jlimit(1, NUM_TRACKS, tracksInSet);
        numActiveClips = juce::jlimit(MIN_SCENES, MAX_CLIPS, scenesInSet);
        for (auto& track : tracks) {
            for (int clipIndex = 0; clipIndex < MAX_CLIPS; clipIndex++) {
                track.getClip(clipIndex).clear();
                track.getClip(clipIndex).settings.reset();
            }
            track.armClip(-1);
        }
        for (auto& armed : trackArmed) {
            armed.store(false);
        }
        sceneManager.setNumScenes(numActiveClips);
        sceneManager.cancelPending();
        sceneManager.setCurrentScene(SceneManager::NO_SCENE);
        setArrangement(new ArrangementData());
        startPosition.store(0.0);
        songBeatForUi.store(0.0);
    }

    // ---- audio thread ----------------------------------------------------------------------------

    // Metronome ticks produced by the last processBlock (sample offset + downbeat accent).
    struct Click {
        int sample = 0;
        bool accent = false;
    };
    static constexpr int MAX_CLICKS = 64;

    int getNumClicks() const { return numClicks; }
    const Click& getClick(int index) const { return clicks[static_cast<size_t>(index)]; }

    // Host transport (plugin builds), set before each processBlock. While the host plays, its
    // position drives the song position and the transport.
    void setHostPosition(bool hostIsPlaying, double hostPpq) {
        hostActive = true;
        hostPlaying = hostIsPlaying;
        hostPosition = hostPpq;
    }

    void clearHostPosition() {
        hostActive = false;
    }

    // `input` is the MIDI arriving from the host / devices: it is routed to armed tracks (live
    // monitoring) and recorded into clips while recording. Output goes to `midi`.
    void processBlock(juce::MidiBuffer& midi, int numSamples, const juce::MidiBuffer* input = nullptr) {
        numClicks = 0;
        collectInput(input, numSamples);
        const bool followHost = hostActive && hostPlaying;
        const bool shouldPlay = playing.load() || followHost;

        if (!shouldPlay) {
            if (wasPlaying) {
                for (int t = 0; t < NUM_TRACKS; t++) {
                    finishRecording(t, songBeat, false, midi, 0);
                    tracks[static_cast<size_t>(t)].onTransportStop(midi, 0);
                }
                if (loggingArrangement) {
                    pushArrangementEvent(RecordEvent::Type::LogEnded, 0, -1, 0.0);
                    loggingArrangement = false;
                    arrangementRecord.store(false);
                }
                wasPlaying = false;
            }
            // Stopped: input still reaches armed tracks so they can be played live.
            routeInput(midi, 0, numSamples, 0.0, 0.0);
            return;
        }

        const double samplesPerBeat = currentSampleRate * 60.0 / bpm.load();
        const double halfSample = 0.5 / samplesPerBeat;

        if (!wasPlaying) {
            // Transport start: song position 0 (or the host's position) begins the grids.
            const double countIn = countInBeats.exchange(0.0);
            songBeat = followHost ? hostPosition : (countIn > 0.0 ? -countIn : startPosition.load());
            nextQuantizationBeat = juce::jmax(songBeat, 0.0);
            for (auto& track : tracks) {
                track.requestChase();
            }
            nextClickBeat = std::ceil(songBeat - halfSample);
            for (auto& track : tracks) {
                track.restart(0.0);
            }
            wasPlaying = true;
        } else if (followHost && std::abs(hostPosition - songBeat) > 2.0 * halfSample) {
            // The host jumped (loop, scrub): follow it; grids restart from the new position.
            songBeat = hostPosition;
            nextQuantizationBeat = std::ceil(songBeat / Clip::STEP_LENGTH - 1e-9) * Clip::STEP_LENGTH;
            nextClickBeat = std::ceil(songBeat - halfSample);
        }
        const bool clickOn = metronome.load();
        arrangementNow = arrangement.get();
        const bool loopOn = loopEnabled.load() && loopEnd.load() > loopStart.load() + Clip::STEP_LENGTH;
        const double loopFrom = loopStart.load();
        const double loopTo = loopEnd.load();
        if (backToArrangement.exchange(false)) {
            for (auto& track : tracks) {
                track.returnToArrangement(midi, 0);
            }
        }
        updateArrangementLog(0, true);
        const double quantization = getQuantizationSteps() * Clip::STEP_LENGTH;
        const Groove groove { swingTiming.load(), swingVelocity.load() };

        int position = 0;
        while (position < numSamples) {
            // Arrangement loop: jump back when the song position reaches the loop end.
            if (loopOn && !followHost && songBeat >= loopTo - halfSample && songBeat < loopTo + 1.0) {
                songBeat = loopFrom + juce::jmax(0.0, songBeat - loopTo);
                nextQuantizationBeat = std::ceil(songBeat / Clip::STEP_LENGTH - 1e-9) * Clip::STEP_LENGTH;
                nextClickBeat = std::ceil(songBeat - halfSample);
                for (auto& track : tracks) {
                    if (track.followsArrangement()) {
                        track.releaseAllNotes(midi, position);
                    }
                    track.requestChase();
                }
            }
            const bool countingIn = songBeat < -halfSample;
            startPendingOverdubs(position);

            // Launches waiting for this moment. With quantization off they apply immediately and
            // Repeat-mode clips retrigger every 16th.
            const double grid = quantization > 0.0 ? quantization : Clip::STEP_LENGTH;
            if (quantization <= 0.0 && !countingIn) {
                applyPendingLaunches(midi, position, 0.0);
            }
            if (!countingIn && songBeat >= nextQuantizationBeat - halfSample) {
                const double lateBy = juce::jmax(0.0, songBeat - nextQuantizationBeat);
                if (quantization > 0.0) {
                    applyPendingLaunches(midi, position, lateBy);
                }
                applyRepeats(midi, position, lateBy);
                nextQuantizationBeat = (std::floor((songBeat + halfSample) / grid) + 1.0) * grid;
            }

            chainSongIfDue(midi, position);

            if (songBeat >= nextClickBeat - halfSample) {
                if ((clickOn || countingIn) && numClicks < MAX_CLICKS) {
                    const auto beatIndex = static_cast<juce::int64>(std::llround(nextClickBeat));
                    const auto beatsPerBar = static_cast<juce::int64>(Clip::BEATS_PER_BAR);
                    clicks[static_cast<size_t>(numClicks++)] = { position, ((beatIndex % beatsPerBar) + beatsPerBar) % beatsPerBar == 0 };
                }
                nextClickBeat = std::floor(songBeat + halfSample) + 1.0;
            }

            // Split the block at the next thing any track or the grid needs to react to.
            double untilEvent = juce::jmin(nextQuantizationBeat, nextClickBeat) - songBeat;
            if (loopOn && !followHost && songBeat < loopTo) {
                untilEvent = juce::jmin(untilEvent, loopTo - songBeat);
            }
            if (countingIn) {
                untilEvent = juce::jmin(untilEvent, -songBeat);
            } else {
                for (auto& track : tracks) {
                    untilEvent = juce::jmin(untilEvent, track.beatsToNextEvent());
                }
            }
            const int chunk = juce::jlimit(1, numSamples - position,
                                           static_cast<int>(std::ceil(juce::jmax(untilEvent, 0.0) * samplesPerBeat - 1e-6)));

            // Clip positions at the start of this chunk, for timing overdubbed notes.
            for (int t = 0; t < NUM_TRACKS; t++) {
                chunkClipPosition[static_cast<size_t>(t)] = tracks[static_cast<size_t>(t)].clipPositionAfter(0.0);
            }
            if (!countingIn) {
                for (int t = 0; t < NUM_TRACKS; t++) {
                    const auto* lane = arrangementNow != nullptr ? &arrangementNow->rendered[static_cast<size_t>(t)] : nullptr;
                    tracks[static_cast<size_t>(t)].process(midi, position, chunk, samplesPerBeat, groove, random, songBeat, lane);
                }
                updateArrangementLog(position, false);
            }
            routeInput(midi, position, chunk, songBeat, samplesPerBeat);

            songBeat += chunk / samplesPerBeat;
            position += chunk;
        }
        songBeatForUi.store(songBeat);
        arrangementNow = nullptr;
    }

    // ---- UI thread: launching ----------------------------------------------------------------------

    // A clip slot was pressed. What happens depends on the clip's launch mode.
    void pressClip(int trackIndex, int clipIndex) {
        if (!isValid(trackIndex, clipIndex)) {
            return;
        }
        Track& track = tracks[trackIndex];
        const Clip& clip = track.getClip(clipIndex);

        if (!clip.hasClip()) {
            track.requestStop();
            return;
        }

        switch (clip.settings.launchMode.load()) {
            case LaunchMode::Toggle:
                if (isClipActiveOrQueued(trackIndex, clipIndex)) {
                    track.requestStop();
                    return;
                }
                track.requestLaunch(clipIndex);
                break;
            case LaunchMode::Repeat:
                track.setRepeatClip(clipIndex);
                track.requestLaunch(clipIndex);
                break;
            case LaunchMode::Trigger:
            case LaunchMode::Gate:
                track.requestLaunch(clipIndex);
                break;
        }
        playing.store(true); // launching a clip starts the transport
    }

    // A clip slot was released.
    void releaseClip(int trackIndex, int clipIndex) {
        if (!isValid(trackIndex, clipIndex)) {
            return;
        }
        Track& track = tracks[trackIndex];

        switch (track.getClip(clipIndex).settings.launchMode.load()) {
            case LaunchMode::Gate:
                if (isClipActiveOrQueued(trackIndex, clipIndex)) {
                    track.requestStop();
                }
                break;
            case LaunchMode::Repeat:
                track.clearRepeatClip(clipIndex);
                break;
            case LaunchMode::Trigger:
            case LaunchMode::Toggle:
                break;
        }
    }

    void stopTrack(int trackIndex) {
        if (trackIndex >= 0 && trackIndex < NUM_TRACKS) {
            tracks[trackIndex].requestStop();
        }
    }

    void launchScene(int sceneIndex) {
        sceneManager.requestScene(sceneIndex);
        playing.store(true);
    }

    void stopAllClips() {
        sceneManager.requestStopAll();
    }

    // ---- UI thread: state for display --------------------------------------------------------------

    // The clip playing (or armed to play when the transport starts) on a track, or -1.
    int getPlayingClip(int trackIndex) const {
        return tracks[trackIndex].getPlayingClipIndex();
    }

    // The clip queued to launch on the next boundary on this track, or -1.
    int getQueuedClip(int trackIndex) const {
        const int request = tracks[trackIndex].getPendingRequest();
        if (request >= 0) {
            return request;
        }
        if (request == Track::NO_REQUEST) {
            const int scene = sceneManager.getPendingScene();
            if (scene >= 0 && tracks[trackIndex].getClip(scene).hasClip()) {
                return scene;
            }
        }
        return -1;
    }

    // True while a stop is queued for a track that is currently playing.
    bool isTrackStopQueued(int trackIndex) const {
        const Track& track = tracks[trackIndex];
        if (!track.isPlayingClip()) {
            return false;
        }
        const int request = track.getPendingRequest();
        if (request == Track::STOP_REQUEST) {
            return true;
        }
        if (request != Track::NO_REQUEST) {
            return false;
        }
        const int scene = sceneManager.getPendingScene();
        return scene == SceneManager::STOP_ALL || (scene >= 0 && !track.getClip(scene).hasClip());
    }

    bool hasClip(int trackIndex, int clipIndex) const {
        return isValid(trackIndex, clipIndex) && tracks[static_cast<size_t>(trackIndex)].getClip(clipIndex).hasClip();
    }

    // ---- UI thread: clip content ---------------------------------------------------------------

    ClipData::Ptr getClipData(int trackIndex, int clipIndex) const {
        return isValid(trackIndex, clipIndex) ? tracks[static_cast<size_t>(trackIndex)].getClip(clipIndex).getData() : nullptr;
    }

    // Replaces a slot's content (nullptr empties it). The audio thread picks it up on its next block.
    // `data` must be a fresh, already sanitised snapshot: once handed over it is never modified.
    void setClipData(int trackIndex, int clipIndex, ClipData::Ptr data) {
        if (isValid(trackIndex, clipIndex)) {
            tracks[static_cast<size_t>(trackIndex)].getClip(clipIndex).setData(data);
        }
    }

    // Position of the playhead inside the clip a track is playing, in beats.
    double getClipPlayPosition(int trackIndex) const {
        return tracks[static_cast<size_t>(trackIndex)].getClipPosition();
    }

    double getSongPosition() const {
        return songBeatForUi.load();
    }

    void setSwing(double timing, double velocity) {
        swingTiming.store(juce::jlimit(0.0, 1.0, timing));
        swingVelocity.store(juce::jlimit(0.0, 1.0, velocity));
    }

    double getSwingTiming() const { return swingTiming.load(); }
    double getSwingVelocity() const { return swingVelocity.load(); }

    ClipSettings& getClipSettings(int trackIndex, int clipIndex) {
        return tracks[trackIndex].getClip(clipIndex).settings;
    }

    int getCurrentScene() const {
        return sceneManager.getCurrentScene();
    }

    int getQueuedScene() const {
        const int scene = sceneManager.getPendingScene();
        return scene >= 0 ? scene : -1;
    }

    bool isStopAllQueued() const {
        return sceneManager.getPendingScene() == SceneManager::STOP_ALL;
    }

    bool isSongMode() const {
        return sceneManager.isSongMode();
    }

    void setSongMode(bool enabled) {
        sceneManager.setSongMode(enabled);
    }

    // ---- transport -------------------------------------------------------------------------------

    bool isPlaying() const {
        return playing.load();
    }

    void setPlaying(bool shouldPlay) {
        playing.store(shouldPlay);
    }

    double getTempo() const {
        return bpm.load();
    }

    static constexpr double MIN_TEMPO = 20.0;
    static constexpr double MAX_TEMPO = 999.0;

    void setTempo(double newBpm) {
        bpm.store(juce::jlimit(MIN_TEMPO, MAX_TEMPO, newBpm));
    }

    void setMetronome(bool enabled) { metronome.store(enabled); }
    bool isMetronomeOn() const { return metronome.load(); }

    int getQuantizationIndex() const {
        return quantizationIndex.load();
    }

    void setQuantizationIndex(int index) {
        quantizationIndex.store(juce::jlimit(0, static_cast<int>(QUANTIZATION_STEPS.size()) - 1, index));
    }

    int getQuantizationSteps() const {
        return QUANTIZATION_STEPS[static_cast<size_t>(quantizationIndex.load())];
    }

    // ---- recording (UI thread) ---------------------------------------------------------------------

    static constexpr int NOT_RECORDING = -1;

    // Records into `slot` on `track`. Overdub (`slot` is the clip playing there) starts right away;
    // a new clip starts on the next launch-quantization boundary. The slot must already hold a
    // (placeholder) clip — the Recorder creates it.
    void requestRecord(int trackIndex, int slot, bool overdub) {
        if (trackIndex < 0 || trackIndex >= NUM_TRACKS) {
            return;
        }
        auto& state = recordState[static_cast<size_t>(trackIndex)];
        state.pendingOverdub.store(overdub);
        state.pendingStop.store(false);
        state.pendingSlot.store(slot);
        if (!playing.load()) {
            countInBeats.store(overdub ? 0.0 : countInBars.load() * Clip::BEATS_PER_BAR);
            playing.store(true);
        }
    }

    // Ends a recording: overdubs stop at once; a new clip ends on the next quantization boundary and
    // starts looping there.
    void requestStopRecord(int trackIndex) {
        if (trackIndex >= 0 && trackIndex < NUM_TRACKS) {
            recordState[static_cast<size_t>(trackIndex)].pendingStop.store(true);
        }
    }

    int getRecordingSlot(int trackIndex) const {
        const auto& state = recordState[static_cast<size_t>(trackIndex)];
        const int pending = state.pendingSlot.load();
        return pending >= 0 ? pending : state.activeSlot.load();
    }

    bool isRecordingOrPending(int trackIndex) const {
        return getRecordingSlot(trackIndex) >= 0;
    }

    void setCountInBars(int bars) { countInBars.store(juce::jlimit(0, 4, bars)); }
    int getCountInBars() const { return countInBars.load(); }

    RecordEventQueue& getRecordEvents() { return recordEvents; }

    // ---- arrangement (UI thread) ----------------------------------------------------------------

    ArrangementData::Ptr getArrangement() const {
        const juce::SpinLock::ScopedLockType lock(arrangementLock);
        return arrangement;
    }

    // `data` must be fresh and already rendered; it is never modified after this call.
    void setArrangement(ArrangementData::Ptr data) {
        ArrangementData::Ptr old;
        {
            const juce::SpinLock::ScopedLockType lock(arrangementLock);
            old = arrangement;
            arrangement = data;
        }
        ReleasePool::get().retain(old.get());
    }

    // Where playback starts (the insert marker), in beats.
    void setStartPosition(double beats) { startPosition.store(juce::jmax(0.0, beats)); }
    double getStartPosition() const { return startPosition.load(); }

    // Control-bar stop: stops the transport; stopping while already stopped returns the start
    // position to 1.1.1 (§7.1).
    void pressStop() {
        if (!playing.load()) {
            startPosition.store(0.0);
            songBeatForUi.store(0.0);
        }
        playing.store(false);
    }

    void setLoop(bool enabled, double start, double end) {
        loopStart.store(juce::jmax(0.0, start));
        loopEnd.store(juce::jmax(start + Clip::STEP_LENGTH, end));
        loopEnabled.store(enabled);
    }
    bool isLoopEnabled() const { return loopEnabled.load(); }
    double getLoopStart() const { return loopStart.load(); }
    double getLoopEnd() const { return loopEnd.load(); }

    // §7.5 Back to Arrangement: every overridden track drops its session clip and follows the
    // arrangement again from the current position.
    void requestBackToArrangement() { backToArrangement.store(true); }

    // Lit while some track's output differs from its arrangement.
    bool isArrangementOverridden() const {
        const auto data = getArrangement();
        for (int t = 0; t < NUM_TRACKS; t++) {
            if (tracks[static_cast<size_t>(t)].isOverridden() && data != nullptr && data->hasContent(t)) {
                return true;
            }
        }
        return false;
    }

    bool followsArrangement(int trackIndex) const {
        return tracks[static_cast<size_t>(trackIndex)].followsArrangement();
    }

    // Arrangement record: while on (and playing), session launches and stops are logged so the
    // Recorder can write them into the arrangement. Turning it on starts the transport.
    void setArrangementRecord(bool enabled) {
        arrangementRecord.store(enabled);
        if (enabled && !playing.load()) {
            playing.store(true);
        }
    }
    bool isArrangementRecording() const { return arrangementRecord.load(); }
    RecordEventQueue& getArrangementEvents() { return arrangementEvents; }

    // Record-arm state per track (used by MIDI recording and live input monitoring).
    void setArmed(int trackIndex, bool armed) {
        if (trackIndex >= 0 && trackIndex < NUM_TRACKS) {
            trackArmed[static_cast<size_t>(trackIndex)].store(armed);
        }
    }

    bool isArmed(int trackIndex) const {
        return trackIndex >= 0 && trackIndex < NUM_TRACKS && trackArmed[static_cast<size_t>(trackIndex)].load();
    }

    int getNumActiveTracks() const {
        return numActiveTracks;
    }

    int getNumActiveClips() const {
        return numActiveClips;
    }

    // Test / host access to a track. Do not mutate playback state from outside the audio thread.
    Track& getTrack(int trackIndex) {
        return tracks[trackIndex];
    }

private:
    std::array<Track, NUM_TRACKS> tracks;
    SceneManager sceneManager;
    juce::Random random;

    std::atomic<double> bpm { 120.0 };
    std::atomic<bool> playing { false };
    std::atomic<int> quantizationIndex { DEFAULT_QUANTIZATION_INDEX };
    double currentSampleRate = 44100.0;
    bool wasPlaying = false;

    double songBeat = 0.0;             // song position in beats (0 = 1.1.1)
    double nextClickBeat = 0.0;
    std::atomic<bool> metronome { false };
    std::array<Click, MAX_CLICKS> clicks {};
    int numClicks = 0;
    bool hostActive = false;
    bool hostPlaying = false;
    double hostPosition = 0.0;
    double nextQuantizationBeat = 0.0; // next launch-quantization boundary
    std::atomic<double> songBeatForUi { 0.0 };
    std::array<std::atomic<bool>, NUM_TRACKS> trackArmed {};

    mutable juce::SpinLock arrangementLock;
    ArrangementData::Ptr arrangement { new ArrangementData() };
    ArrangementData* arrangementNow = nullptr; // snapshot pinned for the current block
    std::atomic<double> startPosition { 0.0 };
    std::atomic<bool> loopEnabled { false };
    std::atomic<double> loopStart { 0.0 };
    std::atomic<double> loopEnd { 16.0 };
    std::atomic<bool> backToArrangement { false };
    std::atomic<bool> arrangementRecord { false };
    bool loggingArrangement = false;
    std::array<int, NUM_TRACKS> loggedLaunchCount {};
    std::array<int, NUM_TRACKS> loggedClip {};
    RecordEventQueue arrangementEvents;

    struct RecordState {
        std::atomic<int> pendingSlot { NOT_RECORDING };
        std::atomic<bool> pendingOverdub { false };
        std::atomic<bool> pendingStop { false };
        std::atomic<int> activeSlot { NOT_RECORDING };
        bool overdub = false;
        double startBeat = 0.0; // song beat where a new clip's recording began
    };
    std::array<RecordState, NUM_TRACKS> recordState;
    std::array<double, NUM_TRACKS> chunkClipPosition {};
    RecordEventQueue recordEvents;
    std::atomic<int> countInBars { 0 };
    std::atomic<double> countInBeats { 0.0 };

    struct InputEvent {
        int sample = 0;
        juce::MidiMessage message;
    };
    static constexpr int MAX_INPUT_EVENTS = 256;
    std::array<InputEvent, MAX_INPUT_EVENTS> inputEvents;
    int numInputEvents = 0;
    std::atomic<double> swingTiming { 0.0 };
    std::atomic<double> swingVelocity { 0.0 };

    int numActiveTracks = 0;
    int numActiveClips = 0;

    static bool isValid(int trackIndex, int clipIndex) {
        return trackIndex >= 0 && trackIndex < NUM_TRACKS && clipIndex >= 0 && clipIndex < MAX_CLIPS;
    }

    bool isClipActiveOrQueued(int trackIndex, int clipIndex) const {
        const Track& track = tracks[trackIndex];
        const int request = track.getPendingRequest();
        if (request == clipIndex) {
            return true;
        }
        return track.getPlayingClipIndex() == clipIndex && request != Track::STOP_REQUEST;
    }

    // ---- arrangement record (audio thread) ----

    // Compares each track's session state with what was last logged and reports changes.
    // `starting` is the block start: logging begins / ends there.
    void updateArrangementLog(int position, bool starting) {
        juce::ignoreUnused(position);
        const bool want = arrangementRecord.load() && wasPlaying;
        if (starting && want != loggingArrangement) {
            if (!want) {
                pushArrangementEvent(RecordEvent::Type::LogEnded, 0, -1, 0.0);
                loggingArrangement = false;
                return;
            }
            loggingArrangement = true;
            // Clips already playing enter the log mid-way.
            for (int t = 0; t < NUM_TRACKS; t++) {
                const auto& track = tracks[static_cast<size_t>(t)];
                loggedLaunchCount[static_cast<size_t>(t)] = track.getLaunchCount();
                loggedClip[static_cast<size_t>(t)] = track.getPlayingClipIndex();
                if (track.getPlayingClipIndex() >= 0) {
                    pushArrangementEvent(RecordEvent::Type::ClipLaunched, t, track.getPlayingClipIndex(),
                                         track.getElapsedSinceLaunch());
                }
            }
            return;
        }
        if (!loggingArrangement) {
            return;
        }
        for (int t = 0; t < NUM_TRACKS; t++) {
            const auto& track = tracks[static_cast<size_t>(t)];
            const int clip = track.getPlayingClipIndex();
            const bool relaunched = track.getLaunchCount() != loggedLaunchCount[static_cast<size_t>(t)];
            if (clip == loggedClip[static_cast<size_t>(t)] && !relaunched) {
                continue;
            }
            if (clip >= 0) {
                pushArrangementEvent(RecordEvent::Type::ClipLaunched, t, clip, track.getElapsedSinceLaunch());
            } else {
                pushArrangementEvent(RecordEvent::Type::ClipStopped, t, -1, 0.0);
            }
            loggedLaunchCount[static_cast<size_t>(t)] = track.getLaunchCount();
            loggedClip[static_cast<size_t>(t)] = clip;
        }
    }

    void pushArrangementEvent(RecordEvent::Type type, int t, int slot, double offset) {
        RecordEvent event;
        event.type = type;
        event.track = static_cast<juce::int8>(t);
        event.slot = static_cast<juce::int16>(slot);
        event.beat = songBeat;
        event.offset = offset;
        arrangementEvents.push(event);
    }

    // ---- recording / input (audio thread) ----

    void collectInput(const juce::MidiBuffer* input, int numSamples) {
        numInputEvents = 0;
        if (input == nullptr) {
            return;
        }
        for (const auto metadata : *input) {
            if (numInputEvents == MAX_INPUT_EVENTS) {
                break;
            }
            const auto message = metadata.getMessage();
            if (message.isNoteOnOrOff() || message.isController() || message.isPitchWheel()) {
                inputEvents[static_cast<size_t>(numInputEvents++)] = { juce::jlimit(0, numSamples - 1, metadata.samplePosition), message };
            }
        }
    }

    // Input in [position, position + numSamples): monitored on every armed track and recorded into
    // tracks that are recording. `beatAtPosition` is the song beat at `position`.
    void routeInput(juce::MidiBuffer& midi, int position, int numSamples, double beatAtPosition, double samplesPerBeat) {
        for (int i = 0; i < numInputEvents; i++) {
            const auto& event = inputEvents[static_cast<size_t>(i)];
            if (event.sample < position || event.sample >= position + numSamples) {
                continue;
            }
            const double offsetBeats = samplesPerBeat > 0.0 ? (event.sample - position) / samplesPerBeat : 0.0;
            for (int t = 0; t < NUM_TRACKS; t++) {
                if (!trackArmed[static_cast<size_t>(t)].load()) {
                    continue;
                }
                auto routed = event.message;
                routed.setChannel(tracks[static_cast<size_t>(t)].getChannel());
                midi.addEvent(routed, event.sample);

                auto& state = recordState[static_cast<size_t>(t)];
                const int slot = state.activeSlot.load();
                if (slot < 0 || !event.message.isNoteOnOrOff()) {
                    continue;
                }
                RecordEvent recorded;
                recorded.type = event.message.isNoteOn() ? RecordEvent::Type::NoteOn : RecordEvent::Type::NoteOff;
                recorded.track = static_cast<juce::int8>(t);
                recorded.slot = static_cast<juce::int16>(slot);
                recorded.pitch = static_cast<juce::uint8>(event.message.getNoteNumber());
                recorded.velocity = event.message.getVelocity();
                recorded.overdub = state.overdub;
                recorded.beat = state.overdub ? overdubBeat(t, offsetBeats)
                                              : juce::jmax(0.0, beatAtPosition + offsetBeats - state.startBeat);
                recordEvents.push(recorded);
            }
        }
    }

    // Clip position of an overdubbed event `offset` beats into the current chunk.
    double overdubBeat(int t, double offset) const {
        const auto& track = tracks[static_cast<size_t>(t)];
        const auto data = track.getClip(track.getPlayingClipIndex() >= 0 ? track.getPlayingClipIndex() : 0).getData();
        double position = chunkClipPosition[static_cast<size_t>(t)] + offset;
        if (data != nullptr && data->looping && position >= data->getPlayEnd()) {
            position = data->loopStart + std::fmod(position - data->getPlayEnd(), data->getLoopLength());
        }
        return position;
    }

    // Overdubs start (and stop) immediately — at the current chunk.
    void startPendingOverdubs(int position) {
        juce::ignoreUnused(position);
        for (int t = 0; t < NUM_TRACKS; t++) {
            auto& state = recordState[static_cast<size_t>(t)];
            const int pending = state.pendingSlot.load();
            if (pending >= 0 && state.pendingOverdub.load()) {
                state.pendingSlot.store(NOT_RECORDING);
                if (tracks[static_cast<size_t>(t)].getPlayingClipIndex() == pending) {
                    state.overdub = true;
                    state.activeSlot.store(pending);
                    pushMarker(RecordEvent::Type::Started, t, pending, true, 0.0);
                }
            }
            if (state.overdub && state.activeSlot.load() >= 0 && state.pendingStop.exchange(false)) {
                pushMarker(RecordEvent::Type::Finished, t, state.activeSlot.load(), true, 0.0);
                state.activeSlot.store(NOT_RECORDING);
                state.overdub = false;
            }
        }
    }

    // New-clip recordings start and end on quantization boundaries (or immediately without one).
    void startOrStopNewClipRecordings(juce::MidiBuffer& midi, int position, double lateBy) {
        for (int t = 0; t < NUM_TRACKS; t++) {
            auto& state = recordState[static_cast<size_t>(t)];
            if (!state.overdub && state.activeSlot.load() >= 0 && state.pendingStop.exchange(false)) {
                finishRecording(t, songBeat - lateBy, true, midi, position);
            }
            const int pending = state.pendingSlot.load();
            if (pending >= 0 && !state.pendingOverdub.load()) {
                state.pendingSlot.store(NOT_RECORDING);
                finishRecording(t, songBeat - lateBy, false, midi, position);
                tracks[static_cast<size_t>(t)].stop(midi, position);
                state.overdub = false;
                state.startBeat = songBeat - lateBy;
                state.activeSlot.store(pending);
                pushMarker(RecordEvent::Type::Started, t, pending, false, 0.0);
            }
        }
    }

    // Ends an active recording at song beat `endBeat`. A new clip gets its length and, when
    // `launch` is set, starts looping right away.
    void finishRecording(int t, double endBeat, bool launch, juce::MidiBuffer& midi, int position) {
        juce::ignoreUnused(midi, position);
        auto& state = recordState[static_cast<size_t>(t)];
        const int slot = state.activeSlot.load();
        if (slot < 0) {
            return;
        }
        const double length = state.overdub ? 0.0 : juce::jmax(1.0, std::ceil(endBeat - state.startBeat - 1e-6));
        pushMarker(RecordEvent::Type::Finished, t, slot, state.overdub, length);
        state.activeSlot.store(NOT_RECORDING);
        if (launch && !state.overdub) {
            tracks[static_cast<size_t>(t)].launch(slot, 0.0);
        }
        state.overdub = false;
    }

    void pushMarker(RecordEvent::Type type, int t, int slot, bool overdub, double beat) {
        RecordEvent event;
        event.type = type;
        event.track = static_cast<juce::int8>(t);
        event.slot = static_cast<juce::int16>(slot);
        event.overdub = overdub;
        event.beat = beat;
        recordEvents.push(event);
    }

    // Applies the queued scene first, then per-track requests (which, being newer or more specific,
    // override that track's slot of the scene).
    void applyPendingLaunches(juce::MidiBuffer& midi, int position, double lateBy) {
        startOrStopNewClipRecordings(midi, position, lateBy);
        const int scene = sceneManager.takePending();
        if (scene != SceneManager::NO_SCENE) {
            applyScene(scene, midi, position, lateBy);
        }

        for (auto& track : tracks) {
            track.apply(track.takePendingRequest(), midi, position, lateBy);
        }
    }

    void applyScene(int scene, juce::MidiBuffer& midi, int position, double lateBy) {
        for (auto& track : tracks) {
            track.apply(scene == SceneManager::STOP_ALL ? Track::STOP_REQUEST : scene, midi, position, lateBy);
        }
        sceneManager.setCurrentScene(scene == SceneManager::STOP_ALL ? SceneManager::NO_SCENE : scene);
    }

    // Repeat launch mode: relaunch held clips (called on every repeat-grid boundary).
    void applyRepeats(juce::MidiBuffer& midi, int position, double lateBy) {
        for (auto& track : tracks) {
            const int repeat = track.getRepeatClip();
            if (repeat >= 0 && track.getPendingRequest() == Track::NO_REQUEST) {
                track.apply(repeat, midi, position, lateBy);
            }
        }
    }

    // Song mode: when the longest playing clip finishes a pass, launch the next scene right there
    // (unquantized, so the chain is seamless).
    void chainSongIfDue(juce::MidiBuffer& midi, int position) {
        if (!sceneManager.isSongMode() || sceneManager.getPendingScene() != SceneManager::NO_SCENE) {
            return;
        }

        const Track* longest = nullptr;
        for (const auto& track : tracks) {
            if (track.isPlayingClip() && (longest == nullptr || track.getPassLength() > longest->getPassLength())) {
                longest = &track;
            }
        }

        if (longest != nullptr && longest->justCompletedPass()) {
            const int next = sceneManager.getNextSceneInSong();
            if (next != SceneManager::NO_SCENE) {
                applyScene(next, midi, position, 0.0);
            }
        }
    }
};
