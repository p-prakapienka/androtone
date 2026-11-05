#pragma once

#include "ClipData.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <atomic>

// How a clip slot reacts to press / release (from the touch screen, mouse or a controller).
enum class LaunchMode {
    Trigger, // press launches; release is ignored
    Gate,    // press launches; release stops the track
    Toggle,  // press launches; pressing again while it plays (or is queued) stops it
    Repeat   // while held, the clip restarts on every launch-quantization boundary
};

// What a clip does once its follow time has elapsed. "Group" means the run of consecutive filled
// slots on the same track that contains the playing clip (empty slots split groups).
enum class FollowAction {
    None,     // keep playing as if nothing happened
    Stop,     // stop the track
    Again,    // restart this clip from its start marker
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
    // Unlinked: it fires `followTimeSteps` 16th steps after launch, regardless of the clip's length.
    std::atomic<bool> followLinked { true };
    std::atomic<int> followMultiplier { 1 };
    std::atomic<int> followTimeSteps { 16 };

    void copyFrom(const ClipSettings& other) {
        launchMode.store(other.launchMode.load());
        followEnabled.store(other.followEnabled.load());
        followActionA.store(other.followActionA.load());
        followActionB.store(other.followActionB.load());
        chanceA.store(other.chanceA.load());
        chanceB.store(other.chanceB.load());
        followLinked.store(other.followLinked.load());
        followMultiplier.store(other.followMultiplier.load());
        followTimeSteps.store(other.followTimeSteps.load());
    }

    void reset() {
        copyFrom(ClipSettings {});
    }
};

// A clip slot. It is empty (no data) or holds an immutable ClipData snapshot, plus the slot's
// launch settings. The UI replaces the snapshot with setData(); the audio thread picks up the new
// one on its next block via getData().
class Clip {
public:
    // A bar is a fixed 16 steps of a 16th note each (4/4).
    static constexpr int STEPS_PER_BAR = 16;
    static constexpr double STEP_LENGTH = 0.25;          // beats
    static constexpr double BEATS_PER_BAR = 4.0;

    ClipData::Ptr getData() const {
        const juce::SpinLock::ScopedLockType lock(dataLock);
        return data;
    }

    // UI thread. The old snapshot is parked in the ReleasePool so it is never freed on the audio
    // thread.
    void setData(ClipData::Ptr newData) {
        ClipData::Ptr old;
        {
            const juce::SpinLock::ScopedLockType lock(dataLock);
            old = data;
            data = newData;
        }
        ReleasePool::get().retain(old.get());
    }

    void clear() {
        setData(nullptr);
    }

    // True when the slot holds a clip (even one without notes).
    bool hasClip() const {
        return getData() != nullptr;
    }

    // Beats after launch at which the follow action fires.
    static double getFollowBeats(const ClipData& clipData, const ClipSettings& clipSettings) {
        if (clipSettings.followLinked.load()) {
            const int passes = std::max(1, clipSettings.followMultiplier.load());
            if (!clipData.looping) {
                return clipData.getFirstPassLength();
            }
            return clipData.getFirstPassLength() + (passes - 1) * clipData.getLoopLength();
        }
        return std::max(1, clipSettings.followTimeSteps.load()) * STEP_LENGTH;
    }

    ClipSettings settings;

private:
    mutable juce::SpinLock dataLock;
    ClipData::Ptr data;
};
