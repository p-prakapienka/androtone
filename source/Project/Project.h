#pragma once

#include <vector>

struct ProjectNote {
    double start = 0.0;
    double length = 0.0;
    int noteNumber = 0;
    int velocity = 0;
};

struct ProjectClip {
    std::vector<ProjectNote> notes;
};

struct ProjectTrack {
    std::vector<ProjectClip> clips;
};

struct Project {
    std::vector<ProjectTrack> tracks;
    double tempo = 120.0;
};
