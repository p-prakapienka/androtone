#pragma once

#include "../Sequencer/Clip.h"

#include <vector>

struct ProjectNote {
    double start = 0.0;
    double length = 0.0;
    int noteNumber = 0;
    int velocity = 0;
};

// A clip with no notes (or only zero-velocity rests) is an empty slot: launching it stops the track.
struct ProjectClip {
    std::vector<ProjectNote> notes;

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
};

struct ProjectTrack {
    std::vector<ProjectClip> clips;
};

struct Project {
    std::vector<ProjectTrack> tracks;
    double tempo = 120.0;
};
