#pragma once

#include <algorithm>
#include <atomic>
#include <vector>

struct Note {
    int number;
    double length; // note duration in beats: from the step start it sounds this long, then noteOff
    int velocity;
};

// How a clip slot reacts to press / release (from the touch screen, mouse or a controller).
enum class LaunchMode {
    Trigger, // press launches; release is ignored
    Gate,    // press launches; release stops the track
    Toggle,  // press launches; pressing again while it plays (or is queued) stops it
    Repeat   // while held, the clip restarts on every launch-quantization boundary
};

// What a clip does once its follow time has elapsed. "Group" means the run of consecutive non-empty
// clips on the same track that contains the playing clip (empty slots split groups).
enum class FollowAction {
    None,     // keep playing as if nothing happened
    Stop,     // stop the track
    Again,    // restart this clip from its first step
    Previous, // launch the clip above (wraps from the top of the group to the bottom)
    Next,     // launch the clip below (wraps from the bottom of the group to the top)
    First,    // launch the top clip of the group
    Last,     // launch the bottom clip of the group
    Any,      // launch a random clip of the group, possibly this one
    Other     // launch a random clip of the group other than this one (this one if it's alone)
};

// Per-clip launch / follow-action settings. Edited from the UI thread and read from the audio
// thread, so every field is atomic.
struct ClipSettings {
    std::atomic<LaunchMode> launchMode { LaunchMode::Trigger };

    std::atomic<bool> followEnabled { false };
    std::atomic<FollowAction> followActionA { FollowAction::Next };
    std::atomic<FollowAction> followActionB { FollowAction::None };
    std::atomic<int> chanceA { 100 }; // relative weights, 0..100 each
    std::atomic<int> chanceB { 0 };

    // Linked: the follow action fires after the clip has played through `followMultiplier` times.
    // Unlinked: it fires `followTimeSteps` steps after launch, regardless of the clip's length.
    std::atomic<bool> followLinked { true };
    std::atomic<int> followMultiplier { 1 };
    std::atomic<int> followTimeSteps { 16 };
};

// A clip is a looped row of steps. Playback position lives in the owning Track; the clip only holds
// its content and settings.
class Clip {
public:
    // A bar is a fixed 16 steps. Launch quantization is expressed in steps on the global grid.
    static constexpr int STEPS_PER_BAR = 16;

    // The fixed grid resolution: every step lasts this many beats (a 16th note), independent of the
    // notes on it. A bar is STEPS_PER_BAR of these (16 × 0.25 = 4 beats). A note's own duration comes
    // from Note.length and may span several steps.
    static constexpr double STEP_LENGTH = 0.25;

    template<typename Container>
    void setNotes(const Container& newNotes) {
        notes.assign(newNotes.begin(), newNotes.end());
        empty = true;
        for (const auto& note : notes) {
            if (note.velocity > 0) {
                empty = false;
                break;
            }
        }
    }

    const Note& getStep(int step) const {
        return notes[static_cast<size_t>(step)];
    }

    int getNumSteps() const {
        return static_cast<int>(notes.size());
    }

    // True when the clip produces no audible output — no notes, or every step a zero-velocity rest.
    // Launching an empty slot stops its track, and the UI renders empty slots distinctly.
    bool isEmpty() const {
        return empty;
    }

    // Number of steps after launch at which the follow action fires.
    int getFollowSteps() const {
        if (settings.followLinked.load()) {
            return getNumSteps() * std::max(1, settings.followMultiplier.load());
        }
        return std::max(1, settings.followTimeSteps.load());
    }

    ClipSettings settings;

private:
    std::vector<Note> notes;
    std::atomic<bool> empty { true };
};
