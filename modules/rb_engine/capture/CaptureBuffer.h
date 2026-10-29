#pragma once

namespace rb
{

// Collects everything played live (CAP-1) and turns it into clips or overdubs.
// Message thread only: feed it with SessionProcessor::readCaptureEvents.
class CaptureBuffer
{
public:
    struct PlayedNote
    {
        int pitch = 60;
        float velocity = 0.8f;
        float pitchOffset = 0.0f;
        bool open = true;          // note-off not received yet
        bool transportRunning = false;
        double onBeat = 0.0, offBeat = 0.0;       // song beats
        double onSeconds = 0.0, offSeconds = 0.0; // engine time
        int slot = -1;                            // clip playing at note-on
        double clipOn = 0.0, clipOff = 0.0;       // positions in that clip
    };

    void add (const CaptureEvent& event);

    // Drops notes after a pause when the capture timeout is on (CAP-7). Call regularly.
    void update (double nowSeconds, bool timeoutEnabled);
    void setTimeoutSeconds (double s) { timeoutSeconds = s; }

    void clear (int trackId);
    void clearAll();

    bool hasFreeNotes (int trackId) const;
    const std::vector<PlayedNote>& getNotes (int trackId) const;

    struct FreeCapture
    {
        ClipPtr clip;
        double tempo = 0.0; // > 0 when the tempo should be set from the performance
    };

    // CAP-3 / CAP-4: a clip from the free (uncaptured) notes of a track.
    //  - transport stopped: positions from wall-clock time; tempo detected if detectTempo
    //  - transport running: positions from song beats, aligned to the bar grid, at least
    //    minBars long
    FreeCapture captureFree (int trackId, double tempo, bool detectTempo, int minBars) const;

    // CAP-2 / CAP-5: notes played while `slot` was playing, as clip notes to merge.
    std::vector<Note> pendingForClip (int trackId, int slot, const Clip& clip) const;
    void clearPendingForClip (int trackId, int slot);

private:
    std::map<int, std::vector<PlayedNote>> tracks;
    double lastEventSeconds = 0.0;
    double timeoutSeconds = 4.0;
    static constexpr size_t maxNotesPerTrack = 4096;
};

} // namespace rb
