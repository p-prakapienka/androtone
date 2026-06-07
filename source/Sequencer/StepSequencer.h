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
    static constexpr int numTracks = 3;

    StepSequencer() {
        playing = false;
        currentSampleRate = 44100.0;
        lastPlayingState = false;

        tracks[0].setChannel(1);
        tracks[1].setChannel(2);
        tracks[2].setChannel(3);

        loadProject(ProjectPresets::createDefaultProject());
    }

    void prepareToPlay(double sampleRate) {
        currentSampleRate = sampleRate;
    }

    void loadProject(const Project& project) {
        setTempo(project.tempo);

        for (int trackIndex = 0; trackIndex < numTracks && trackIndex < static_cast<int>(project.tracks.size()); ++trackIndex) {
            const auto& projectTrack = project.tracks[trackIndex];

            for (int clipIndex = 0; clipIndex < static_cast<int>(projectTrack.clips.size()); ++clipIndex) {
                const auto& projectClip = projectTrack.clips[clipIndex];
                std::vector<Note> notes;
                notes.reserve(projectClip.notes.size());

                for (const auto& projectNote : projectClip.notes) {
                    notes.push_back({ projectNote.noteNumber, projectNote.length, projectNote.velocity });
                }

                tracks[trackIndex].updateClip(notes, clipIndex);
            }
        }
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

private:
    std::array<Track, numTracks> tracks;
    SceneManager<numTracks> sceneManager { tracks };
    std::atomic<double> bpm;
    std::atomic<bool> playing;
    double currentSampleRate;
    bool lastPlayingState;
};
