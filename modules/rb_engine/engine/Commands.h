#pragma once

namespace rb
{

// Message thread -> audio thread. Plain data so it can go through a lock-free FIFO.
struct Command
{
    enum class Type : juce::uint8
    {
        play,               // start the transport; resumes the clips that were playing
        stop,               // stop the transport
        launchClip,         // trackId, slot: start at the next downbeat (CLIP-2)
        stopClip,           // trackId: stop at the next downbeat (CLIP-3)
        launchScene,        // slot = scene row (SCN-2)
        stopAllClips,
        switchClipSeamless, // trackId, slot: swap the playing clip keeping its position (INS-4)
        noteOn,             // trackId, pitch, velocity, pitchOffset: live input
        noteOff,            // trackId, pitch
        allLiveNotesOff,    // trackId (0 = every track)
        restartClip         // trackId: restart the playing clip from its loop start now
    };

    Type type = Type::play;
    int trackId = 0;
    int slot = 0;
    int pitch = 0;
    float velocity = 0.0f;
    float pitchOffset = 0.0f;
};

// Audio thread -> message thread: every note played live (after Repeat), for capture (CAP-1).
struct CaptureEvent
{
    int trackId = 0;
    int pitch = 0;
    float velocity = 0.0f; // 0 = note-off
    float pitchOffset = 0.0f;
    double songBeat = 0.0;     // transport position (valid when transportRunning)
    double timeSeconds = 0.0;  // engine running time, always valid
    bool transportRunning = false;
    int playingSlot = -1;      // clip playing on the track at that moment, or -1
    double clipBeat = 0.0;     // position inside that clip (loop-wrapped)
};

} // namespace rb
