#pragma once

#include <atomic>

// Scene state shared between the UI and audio threads. A scene is a row of clip slots: launching it
// launches every track's clip in that row (stopping tracks whose slot is empty). The timing of when
// a queued scene takes effect is handled by StepSequencer.
class SceneManager {
public:
    static constexpr int NO_SCENE = -1;
    static constexpr int STOP_ALL = -2;

    // ---- UI thread ----

    void requestScene(int sceneIndex) {
        if (sceneIndex >= 0 && sceneIndex < numScenes.load()) {
            pendingScene.store(sceneIndex);
        }
    }

    void requestStopAll() {
        pendingScene.store(STOP_ALL);
    }

    void cancelPending() {
        pendingScene.store(NO_SCENE);
    }

    // ---- audio thread ----

    int takePending() {
        return pendingScene.exchange(NO_SCENE);
    }

    void setCurrentScene(int sceneIndex) {
        currentScene.store(sceneIndex);
    }

    // ---- either ----

    int getPendingScene() const {
        return pendingScene.load();
    }

    // The scene launched last (what is audible), or NO_SCENE once all clips were stopped.
    int getCurrentScene() const {
        return currentScene.load();
    }

    int getNextSceneInSong() const {
        const int scenes = numScenes.load();
        const int current = currentScene.load();
        return scenes <= 0 ? NO_SCENE : (current < 0 ? 0 : (current + 1) % scenes);
    }

    void setNumScenes(int newNumScenes) {
        numScenes.store(newNumScenes);
    }

    int getNumScenes() const {
        return numScenes.load();
    }

    void setSongMode(bool enabled) {
        songMode.store(enabled);
    }

    bool isSongMode() const {
        return songMode.load();
    }

private:
    std::atomic<bool> songMode { false };
    std::atomic<int> currentScene { 0 };
    std::atomic<int> pendingScene { NO_SCENE };
    std::atomic<int> numScenes { 1 };
};
