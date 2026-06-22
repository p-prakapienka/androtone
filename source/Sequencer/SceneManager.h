#pragma once

#include "Clip.h"
#include "Track.h"

#include <array>
#include <atomic>

template<std::size_t N>
class SceneManager {
public:
    explicit SceneManager(std::array<Track, N>& tracksRef) :
        tracks(tracksRef) {}

    void update() {
        if (!songMode.load()) {
            longestWasOnLastStep = false;
            return;
        }

        const Clip* longest = getLongestPlayingClip();
        const bool onLastStep = longest != nullptr && longest->isLastStep();

        if (!longestWasOnLastStep && onLastStep && numScenes > 0) {
            setCurrentScene((currentScene + 1) % numScenes);
        }
        longestWasOnLastStep = onLastStep;
    }

    // Clears the scene-end edge state. Called on the sequencer's stop transition so a later resume
    // starts clean.
    void reset() {
        longestWasOnLastStep = false;
    }

    void setNumScenes(int newNumScenes) {
        numScenes = newNumScenes;
    }

    void setSongMode(bool enabled) {
        songMode.store(enabled);
    }

    bool isSongMode() const {
        return songMode.load();
    }

    // The scene that is currently playing: the clip index the tracks have actually swapped to.
    // Derived from track state (not the advance counter) so it reflects what's audible, even while
    // a queued scene is pending.
    int getCurrentScene() const {
        return tracks[0].getCurrentClipIndex();
    }

    // The scene queued to start on the next bar boundary, or -1 if none is pending.
    int getNextScene() const {
        return tracks[0].getNextClipIndex();
    }

    void setCurrentScene(int sceneIndex) {
        currentScene = sceneIndex;
        updateTracks();
    }

private:
    std::array<Track, N>& tracks;
    std::atomic<bool> songMode { false };
    std::atomic<int> currentScene = 0;
    int numScenes = 1;
    bool longestWasOnLastStep = false;

    void updateTracks() {
        for (auto& track : tracks) {
            track.setCurrentClip(currentScene);
        }
    }

    const Clip* getLongestPlayingClip() const {
        const Clip* longest = nullptr;
        int mostSteps = 0;

        for (const auto& track : tracks) {
            const Clip& clip = track.getCurrentClip();

            if (clip.isEmpty()) {
                continue;
            }

            if (clip.getNumSteps() > mostSteps) {
                mostSteps = clip.getNumSteps();
                longest = &clip;
            }
        }

        return longest;
    }
};
