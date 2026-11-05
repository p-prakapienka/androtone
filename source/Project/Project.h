#pragma once

#include "../Sequencer/Clip.h"

#include <algorithm>
#include <cmath>
#include <vector>

struct ProjectNote {
    double start = 0.0;
    double length = 0.0;
    int noteNumber = 0;
    int velocity = 0;
};

// A clip in a project file. An entry with no notes and no length is an empty slot.
struct ProjectClip {
    std::vector<ProjectNote> notes; // start / length in beats
    juce::String name;
    double length = 0.0;            // beats; 0 = derive from the notes (rounded up to whole bars)
    bool looping = true;

    LaunchMode launchMode = LaunchMode::Trigger;

    // Follow actions (see FollowAction in Sequencer/Clip.h). Off by default.
    bool followEnabled = false;
    FollowAction followActionA = FollowAction::Next;
    FollowAction followActionB = FollowAction::None;
    int chanceA = 100;
    int chanceB = 0;
    bool followLinked = true;  // fire after followMultiplier passes through the clip...
    int followMultiplier = 1;
    int followTimeSteps = 16;  // ...or, when unlinked, this many 16th steps after launch

    bool isEmptySlot() const {
        return notes.empty() && length <= 0.0;
    }

    double getLength() const {
        if (length > 0.0) {
            return length;
        }
        double end = 0.0;
        for (const auto& note : notes) {
            end = std::max(end, note.start + note.length);
        }
        return std::max(4.0, std::ceil(end / 4.0) * 4.0);
    }
};

struct ProjectTrack {
    std::vector<ProjectClip> clips;
};

struct Project {
    std::vector<ProjectTrack> tracks;
    double tempo = 120.0;
};
