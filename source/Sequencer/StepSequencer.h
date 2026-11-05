#pragma once

#include "Clip.h"
#include "Track.h"
#include "SceneManager.h"
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

    void processBlock(juce::MidiBuffer& midi, int numSamples) {
        numClicks = 0;
        const bool followHost = hostActive && hostPlaying;
        const bool shouldPlay = playing.load() || followHost;

        if (!shouldPlay) {
            if (wasPlaying) {
                for (auto& track : tracks) {
                    track.onTransportStop(midi, 0);
                }
                wasPlaying = false;
            }
            return;
        }

        const double samplesPerBeat = currentSampleRate * 60.0 / bpm.load();
        const double halfSample = 0.5 / samplesPerBeat;

        if (!wasPlaying) {
            // Transport start: song position 0 (or the host's position) begins the grids.
            songBeat = followHost ? hostPosition : 0.0;
            nextQuantizationBeat = songBeat;
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
        const double quantization = getQuantizationSteps() * Clip::STEP_LENGTH;
        const Groove groove { swingTiming.load(), swingVelocity.load() };

        int position = 0;
        while (position < numSamples) {
            // Launches waiting for this moment. With quantization off they apply immediately and
            // Repeat-mode clips retrigger every 16th.
            const double grid = quantization > 0.0 ? quantization : Clip::STEP_LENGTH;
            if (quantization <= 0.0) {
                applyPendingLaunches(midi, position, 0.0);
            }
            if (songBeat >= nextQuantizationBeat - halfSample) {
                const double lateBy = juce::jmax(0.0, songBeat - nextQuantizationBeat);
                if (quantization > 0.0) {
                    applyPendingLaunches(midi, position, lateBy);
                }
                applyRepeats(midi, position, lateBy);
                nextQuantizationBeat = (std::floor((songBeat + halfSample) / grid) + 1.0) * grid;
            }

            chainSongIfDue(midi, position);

            if (songBeat >= nextClickBeat - halfSample) {
                if (clickOn && numClicks < MAX_CLICKS) {
                    const auto beatIndex = static_cast<juce::int64>(std::llround(nextClickBeat));
                    clicks[static_cast<size_t>(numClicks++)] = { position, beatIndex % static_cast<juce::int64>(Clip::BEATS_PER_BAR) == 0 };
                }
                nextClickBeat = std::floor(songBeat + halfSample) + 1.0;
            }

            // Split the block at the next thing any track or the grid needs to react to.
            double untilEvent = juce::jmin(nextQuantizationBeat, nextClickBeat) - songBeat;
            for (auto& track : tracks) {
                untilEvent = juce::jmin(untilEvent, track.beatsToNextEvent());
            }
            const int chunk = juce::jlimit(1, numSamples - position,
                                           static_cast<int>(std::ceil(juce::jmax(untilEvent, 0.0) * samplesPerBeat - 1e-6)));

            for (auto& track : tracks) {
                track.process(midi, position, chunk, samplesPerBeat, groove, random);
            }

            songBeat += chunk / samplesPerBeat;
            position += chunk;
        }
        songBeatForUi.store(songBeat);
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

    // Applies the queued scene first, then per-track requests (which, being newer or more specific,
    // override that track's slot of the scene).
    void applyPendingLaunches(juce::MidiBuffer& midi, int position, double lateBy) {
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
