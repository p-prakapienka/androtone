#pragma once

#include "Clip.h"
#include "Track.h"
#include "SceneManager.h"
#include "../Project/Projects.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <vector>

class StepSequencer {
public:
    static constexpr int numTracks = 8;

    StepSequencer() {
        playing = false;
        currentSampleRate = 44100.0;
        lastPlayingState = false;

        for (int trackIndex = 0; trackIndex < numTracks; trackIndex++) {
            tracks[trackIndex].setChannel(trackIndex + 1);
        }

        loadProject(ProjectPresets::defaultProject);
    }

    void prepareToPlay(double sampleRate) {
        currentSampleRate = sampleRate;
    }

    void loadProject(const Project& project) {
        setTempo(project.tempo);

        numActiveTracks = juce::jmin(static_cast<int>(project.tracks.size()), numTracks);
        numActiveClips = 0;

        for (int trackIndex = 0; trackIndex < numTracks && trackIndex < static_cast<int>(project.tracks.size()); trackIndex++) {
            const auto& projectTrack = project.tracks[trackIndex];
            numActiveClips = juce::jmax(numActiveClips, juce::jmin(static_cast<int>(projectTrack.clips.size()), Track::maxClips));

            for (int clipIndex = 0; clipIndex < static_cast<int>(projectTrack.clips.size()); clipIndex++) {
                const auto& projectClip = projectTrack.clips[clipIndex];
                std::vector<Note> notes;

                if (projectClip.notes.empty()) {
                    // An empty project clip maps to one bar of zero-velocity rests so it still
                    // carries timing and stays bar-quantized on scene switches. A zero-note clip
                    // never advances, so it can't align its switch to a bar boundary.
                    notes.assign(Clip::stepsPerBar, Note { 60, Clip::stepLength, 0 });
                } else {
                    notes.reserve(projectClip.notes.size());

                    for (const auto& projectNote : projectClip.notes) {
                        notes.push_back({ projectNote.noteNumber, projectNote.length, projectNote.velocity });
                    }
                }

                tracks[trackIndex].updateClip(notes, clipIndex);
            }
        }

        sceneManager.setNumScenes(numActiveClips);
    }

    void processBlock(juce::MidiBuffer& midi, int numSamples) {
        const bool shouldPlay = playing.load();

        if (!shouldPlay && lastPlayingState) {
            for (auto& track : tracks) {
                track.stop(midi, 0);
            }
            sceneManager.reset();
            lastPlayingState = false;
        }
        lastPlayingState = shouldPlay;

        if (!shouldPlay) {
            return;
        }

        const double samplesPerBeat = currentSampleRate * 60.0 / bpm.load();

        for (auto& track : tracks) {
            track.processBlock(midi, numSamples, samplesPerBeat);
        }

        sceneManager.update();
    }

    int getCurrentClip(int trackIndex) const {
        return tracks[trackIndex].getCurrentClipIndex();
    }

    bool isClipEmpty(int trackIndex, int clipIndex) const {
        return tracks[trackIndex].getClip(clipIndex).isEmpty();
    }

    void setCurrentClip(int trackIndex, int clipIndex) {
        tracks[trackIndex].setCurrentClip(clipIndex);
    }

    int getNextClip(int trackIndex) const {
        return tracks[trackIndex].getNextClipIndex();
    }

    int getCurrentScene() const {
        return sceneManager.getCurrentScene();
    }

    int getNextScene() const {
        return sceneManager.getNextScene();
    }

    void setCurrentScene(int sceneIndex) {
        sceneManager.setCurrentScene(sceneIndex);
    }

    bool isSongMode() const {
        return sceneManager.isSongMode();
    }

    void setSongMode(bool enabled) {
        sceneManager.setSongMode(enabled);
    }

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

    Track& getTrack(int trackIndex) {
        return tracks[trackIndex];
    }

    int getNumActiveTracks() const {
        return numActiveTracks;
    }

    int getNumActiveClips() const {
        return numActiveClips;
    }

private:
    std::array<Track, numTracks> tracks;
    SceneManager<numTracks> sceneManager { tracks };
    std::atomic<double> bpm;
    std::atomic<bool> playing;
    double currentSampleRate;
    bool lastPlayingState;
    int numActiveTracks = 0;
    int numActiveClips = 0;
};
