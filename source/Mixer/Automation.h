#pragma once

#include "Mixer.h"
#include "../Sequencer/Arrangement.h"

#include <array>
#include <atomic>

// Plays arrangement automation into the mixer. A parameter the user moved by hand is
// "overridden": its automation stops applying until re-enabled (§25.4).
class AutomationPlayer {
public:
    // Audio thread, once per block while the transport runs.
    void apply(const ArrangementData& arrangement, double songBeat, Mixer& mixer) const {
        for (int t = 0; t < NUM_TRACKS; t++) {
            for (int p = 0; p < NUM_AUTOMATED; p++) {
                if (!arrangement.hasAutomation(t, p) || isOverridden(t, p)) {
                    continue;
                }
                const float value = ArrangementData::valueAt(arrangement.automation[static_cast<size_t>(t)][static_cast<size_t>(p)], songBeat);
                if (p == AUTOMATE_VOLUME) {
                    mixer.setTrackVolume(t, value);
                } else {
                    mixer.setTrackPan(t, value);
                }
            }
        }
    }

    // UI thread: the user changed a control; if it is automated, automation yields to the hand.
    void touched(const ArrangementData* arrangement, int t, int p) {
        if (arrangement != nullptr && arrangement->hasAutomation(t, p)) {
            overridden[static_cast<size_t>(t)][static_cast<size_t>(p)].store(true);
        }
    }

    bool isOverridden(int t, int p) const {
        return overridden[static_cast<size_t>(t)][static_cast<size_t>(p)].load();
    }

    bool anyOverridden() const {
        for (const auto& track : overridden) {
            for (const auto& flag : track) {
                if (flag.load()) {
                    return true;
                }
            }
        }
        return false;
    }

    // Re-Enable Automation: everything follows its envelope again.
    void reenableAll() {
        for (auto& track : overridden) {
            for (auto& flag : track) {
                flag.store(false);
            }
        }
    }

private:
    std::array<std::array<std::atomic<bool>, NUM_AUTOMATED>, NUM_TRACKS> overridden {};
};
