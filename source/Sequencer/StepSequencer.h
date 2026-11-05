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

        for (int trackIndex = 0; trackIndex < numActiveTracks; trackIndex++) {
            const auto& projectTrack = project.tracks[trackIndex];
            const int numClips = juce::jmin(static_cast<int>(projectTrack.clips.size()), MAX_CLIPS);
            numActiveClips = juce::jmax(numActiveClips, numClips);

            for (int clipIndex = 0; clipIndex < numClips; clipIndex++) {
                const auto& projectClip = projectTrack.clips[clipIndex];
                std::vector<Note> notes;
                notes.reserve(projectClip.notes.size());

                for (const auto& projectNote : projectClip.notes) {
                    notes.push_back({ projectNote.noteNumber, projectNote.length, projectNote.velocity });
                }

                Track& track = tracks[trackIndex];
                track.updateClip(notes, clipIndex);

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

        sceneManager.setNumScenes(numActiveClips);
        sceneManager.cancelPending();
        sceneManager.setCurrentScene(0);
        for (auto& track : tracks) {
            track.armClip(0);
        }
    }

    // ---- audio thread ----------------------------------------------------------------------------

    void processBlock(juce::MidiBuffer& midi, int numSamples) {
        const bool shouldPlay = playing.load();

        if (!shouldPlay) {
            if (wasPlaying) {
                for (auto& track : tracks) {
                    track.onTransportStop(midi, 0);
                }
                wasPlaying = false;
            }
            return;
        }

        if (!wasPlaying) {
            // Transport start: the grid restarts at step 0 and armed clips play from their top.
            nextGlobalStep = 0;
            samplesToGlobalStep = 0.0;
            for (auto& track : tracks) {
                track.restart(0.0);
            }
            wasPlaying = true;
        }

        const double samplesPerBeat = currentSampleRate * 60.0 / bpm.load();
        const double samplesPerStep = samplesPerBeat * Clip::STEP_LENGTH;
        const int quantization = getQuantizationSteps();

        int position = 0;
        while (position < numSamples) {
            if (quantization == 0) {
                applyPendingLaunches(midi, position, 0.0);
            }

            if (samplesToGlobalStep <= 0.0) {
                const auto globalStep = nextGlobalStep++;
                const int repeatRate = quantization == 0 ? 1 : quantization;

                if (globalStep % repeatRate == 0) {
                    if (quantization > 0) {
                        applyPendingLaunches(midi, position, samplesToGlobalStep);
                    }
                    applyRepeats(midi, position, samplesToGlobalStep);
                }
                samplesToGlobalStep += samplesPerStep;
            }

            chainSongIfDue(midi, position);

            for (auto& track : tracks) {
                if (track.isStepDue()) {
                    track.fireStep(midi, position, samplesPerBeat, samplesPerStep, random);
                }
            }

            double untilNextEvent = samplesToGlobalStep;
            for (const auto& track : tracks) {
                if (track.isPlayingClip()) {
                    untilNextEvent = juce::jmin(untilNextEvent, track.getSamplesToNextStep());
                }
            }
            const int chunk = juce::jlimit(1, numSamples - position, static_cast<int>(std::ceil(untilNextEvent)));

            for (auto& track : tracks) {
                track.advance(midi, position, chunk, samplesPerBeat);
            }
            samplesToGlobalStep -= chunk;
            position += chunk;
        }
    }

    // ---- UI thread: launching ----------------------------------------------------------------------

    // A clip slot was pressed. What happens depends on the clip's launch mode.
    void pressClip(int trackIndex, int clipIndex) {
        if (!isValid(trackIndex, clipIndex)) {
            return;
        }
        Track& track = tracks[trackIndex];
        const Clip& clip = track.getClip(clipIndex);

        if (clip.isEmpty()) {
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
            if (scene >= 0 && !tracks[trackIndex].getClip(scene).isEmpty()) {
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
        return scene == SceneManager::STOP_ALL || (scene >= 0 && track.getClip(scene).isEmpty());
    }

    bool isClipEmpty(int trackIndex, int clipIndex) const {
        return tracks[trackIndex].getClip(clipIndex).isEmpty();
    }

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

    void setTempo(double newBpm) {
        bpm.store(newBpm);
    }

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

    juce::int64 nextGlobalStep = 0;    // index of the next global grid step
    double samplesToGlobalStep = 0.0; // samples until it starts

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
    void applyPendingLaunches(juce::MidiBuffer& midi, int position, double phase) {
        const int scene = sceneManager.takePending();
        if (scene != SceneManager::NO_SCENE) {
            applyScene(scene, midi, position, phase);
        }

        for (auto& track : tracks) {
            track.apply(track.takePendingRequest(), midi, position, phase);
        }
    }

    void applyScene(int scene, juce::MidiBuffer& midi, int position, double phase) {
        for (auto& track : tracks) {
            track.apply(scene == SceneManager::STOP_ALL ? Track::STOP_REQUEST : scene, midi, position, phase);
        }
        sceneManager.setCurrentScene(scene == SceneManager::STOP_ALL ? SceneManager::NO_SCENE : scene);
    }

    void applyRepeats(juce::MidiBuffer& midi, int position, double phase) {
        for (auto& track : tracks) {
            const int repeat = track.getRepeatClip();
            if (repeat >= 0 && track.getPendingRequest() == Track::NO_REQUEST) {
                track.apply(repeat, midi, position, phase);
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
            if (track.isPlayingClip() && (longest == nullptr || track.getPlayingClipLength() > longest->getPlayingClipLength())) {
                longest = &track;
            }
        }

        if (longest != nullptr && longest->isStepDue() && longest->isAboutToLoop()) {
            const int next = sceneManager.getNextSceneInSong();
            if (next != SceneManager::NO_SCENE) {
                applyScene(next, midi, position, longest->getSamplesToNextStep());
            }
        }
    }
};
