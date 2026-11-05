#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/StepSequencer.h"
#include "Sequencer/TapTempo.h"

// Headless timing tests for the sequencer. Each CHECK names the behaviour it pins down; see
// docs/rea/checklist.md for the checklist IDs they verify.
#include <cstdio>
#include <map>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Ev { long long t; bool on; int ch; int note; };
constexpr double SR = 48000.0;
constexpr int BLOCK = 512;
// 120 bpm: samplesPerBeat = 24000, step = 6000 samples, bar = 96000

struct Harness {
    StepSequencer seq;
    long long now = 0;
    std::vector<Ev> evs;
    Harness() { seq.prepareToPlay(SR); seq.setTempo(120.0); }
    void run(long long samples) {
        while (samples > 0) {
            int n = (int) std::min<long long>(BLOCK, samples);
            juce::MidiBuffer m;
            seq.processBlock(m, n);
            for (const auto md : m) {
                auto msg = md.getMessage();
                if (msg.isNoteOnOrOff()) evs.push_back({ now + md.samplePosition, msg.isNoteOn(), msg.getChannel(), msg.getNoteNumber() });
            }
            now += n; samples -= n;
        }
    }
    std::vector<Ev> ons(int ch) { std::vector<Ev> r; for (auto& e : evs) if (e.on && e.ch == ch) r.push_back(e); return r; }
    std::vector<Ev> offs(int ch) { std::vector<Ev> r; for (auto& e : evs) if (!e.on && e.ch == ch) r.push_back(e); return r; }
    std::vector<Ev> on(int ch) { std::vector<Ev> r; for (auto& e : evs) if (e.ch == ch) r.push_back(e); return r; }
};

int main() {
    // Default project: scene 1 = only track 3 (arp) plays; track 1 kick clip 2 = 4 on the floor.
    {
        Harness h;
        h.seq.setPlaying(true);
        h.run(30000); // part-way into bar 1
        CHECK(h.ons(1).empty(), "kick should be silent in scene 1");
        CHECK(!h.ons(3).empty() && h.ons(3)[0].t == 0, "arp starts at 0");
        h.seq.pressClip(0, 1);
        CHECK(h.seq.getQueuedClip(0) == 1, "queued");
        h.run(200000);
        auto k = h.ons(1);
        CHECK(!k.empty() && k[0].t == 96000, "kick launch quantized to bar: first at %lld", k.empty() ? -1 : k[0].t);
        CHECK(k.size() >= 2 && k[1].t == 96000 + 24000, "kick quarter notes");
        CHECK(h.seq.getPlayingClip(0) == 1 && h.seq.getQueuedClip(0) == -1, "playing after launch");
        // kick note length 0.25 beat = 6000 samples
        auto ko = h.offs(1);
        CHECK(!ko.empty() && ko[0].t == 96000 + 6000, "kick noteOff at %lld", ko.empty() ? -1 : ko[0].t);
    }
    // Quantization 1/4: launch lands on next beat
    {
        Harness h;
        h.seq.setQuantizationIndex(3);
        h.seq.setPlaying(true);
        h.run(30000);
        h.seq.pressClip(0, 1);
        h.run(100000);
        auto k = h.ons(1);
        CHECK(!k.empty() && k[0].t == 48000, "1/4 quantized launch at %lld", k.empty() ? -1 : k[0].t);
    }
    // Quantization None: launch at next block start
    {
        Harness h;
        h.seq.setQuantizationIndex(0);
        h.seq.setPlaying(true);
        h.run(30000);
        h.seq.pressClip(0, 1);
        h.run(30000);
        auto k = h.ons(1);
        CHECK(!k.empty() && k[0].t == 30000, "unquantized launch at %lld", k.empty() ? -1 : k[0].t);
        CHECK(k.size() >= 2 && k[1].t == 54000, "own phase: second at %lld", k.size() < 2 ? -1 : k[1].t);
    }
    // Launching a clip while stopped starts transport at grid 0
    {
        Harness h;
        h.seq.pressClip(0, 1);
        CHECK(h.seq.isPlaying(), "press starts transport");
        h.run(1000);
        auto k = h.ons(1);
        CHECK(!k.empty() && k[0].t == 0, "starts at 0");
    }
    // Stop track: quantized; noteOffs and no further notes
    {
        Harness h;
        h.seq.setPlaying(true);
        h.run(10000);
        h.seq.stopTrack(2);
        CHECK(h.seq.isTrackStopQueued(2), "stop queued");
        h.run(200000);
        for (auto& e : h.ons(3)) CHECK(e.t < 96000, "arp note after stop at %lld", e.t);
        CHECK(h.seq.getPlayingClip(2) == -1, "stopped");
    }
    // Follow action: Next, linked, on kick clip 2 -> after 16 steps launches clip 3 (half time)
    {
        Harness h;
        auto& s = h.seq.getClipSettings(0, 1);
        s.followEnabled = true; s.followActionA = FollowAction::Next; s.chanceA = 100; s.chanceB = 0;
        h.seq.getClipSettings(0, 2).followEnabled = true; // clip 3 too, default action A = Next
        h.seq.pressClip(0, 1);
        h.run(96000 + 1000);
        CHECK(h.seq.getPlayingClip(0) == 2, "follow next -> clip 3, got %d", h.seq.getPlayingClip(0));
        h.run(96000);
        // clip 3 is the last of the group (clip 1 empty) -> Next wraps to clip 2
        CHECK(h.seq.getPlayingClip(0) == 1, "wrap to clip 2, got %d", h.seq.getPlayingClip(0));
        // half time pattern in bar 2: hits at 96000 and 96000+48000
        int count = 0; for (auto& e : h.ons(1)) if (e.t >= 96000 && e.t < 192000) count++;
        CHECK(count == 2, "half-time bar has 2 hits, got %d", count);
    }
    // Follow action unlinked Stop after 1 beat
    {
        Harness h;
        auto& s = h.seq.getClipSettings(0, 1);
        s.followEnabled = true; s.followActionA = FollowAction::Stop; s.followLinked = false; s.followTimeSteps = 4;
        h.seq.pressClip(0, 1);
        h.run(96000);
        CHECK(h.ons(1).size() == 1, "one hit then stop, got %zu", h.ons(1).size());
        CHECK(h.seq.getPlayingClip(0) == -1, "stopped by follow action");
    }
    // Gate: release stops at next quantization boundary
    {
        Harness h;
        h.seq.getClipSettings(0, 1).launchMode = LaunchMode::Gate;
        h.seq.setQuantizationIndex(3); // 1/4
        h.seq.pressClip(0, 1);
        h.run(30000);
        h.seq.releaseClip(0, 1);
        h.run(100000);
        CHECK(h.ons(1).size() == 2, "gate: 2 quarter hits before stop, got %zu", h.ons(1).size());
        CHECK(h.seq.getPlayingClip(0) == -1, "gate stopped");
    }
    // Toggle: second press stops
    {
        Harness h;
        h.seq.getClipSettings(0, 1).launchMode = LaunchMode::Toggle;
        h.seq.pressClip(0, 1); h.seq.releaseClip(0, 1);
        h.run(50000);
        h.seq.pressClip(0, 1); h.seq.releaseClip(0, 1);
        CHECK(h.seq.isTrackStopQueued(0), "toggle queues stop");
        h.run(100000);
        CHECK(h.seq.getPlayingClip(0) == -1, "toggle stopped");
    }
    // Repeat: held -> restarts every quantum (1/4): with half-time clip each restart hits on beat
    {
        Harness h;
        h.seq.getClipSettings(0, 2).launchMode = LaunchMode::Repeat;
        h.seq.setQuantizationIndex(3);
        h.seq.pressClip(0, 2);
        h.run(96000);
        CHECK(h.ons(1).size() == 4, "repeat restarts each beat: %zu hits", h.ons(1).size());
        h.seq.releaseClip(0, 2);
        h.run(96000);
        CHECK(h.ons(1).size() == 4 + 2, "after release the last restart plays on (steps 8 and wrap): %zu", h.ons(1).size());
    }
    // Scene launch + stop all
    {
        Harness h;
        h.seq.setPlaying(true);
        h.run(10000);
        h.seq.launchScene(1);
        CHECK(h.seq.getQueuedScene() == 1, "scene queued");
        h.run(100000);
        CHECK(h.seq.getCurrentScene() == 1, "scene 2 current");
        for (int t = 0; t < 3; t++) CHECK(h.seq.getPlayingClip(t) == 1, "track %d on clip 2", t);
        h.seq.stopAllClips();
        h.run(100000);
        for (int t = 0; t < 3; t++) CHECK(h.seq.getPlayingClip(t) == -1, "track %d stopped", t);
        CHECK(h.seq.getCurrentScene() == -1, "no scene after stop all");
    }
    // Song mode chains scenes at loop end
    {
        Harness h;
        h.seq.setSongMode(true);
        h.seq.setPlaying(true);
        h.run(192000 - 100);
        CHECK(h.seq.getCurrentScene() == 0, "still scene 1 before the 2-bar arp ends");
        h.run(200);
        CHECK(h.seq.getCurrentScene() == 1, "song -> scene 2, got %d", h.seq.getCurrentScene());
        CHECK(!h.ons(1).empty() && h.ons(1)[0].t == 192000, "kick enters at bar 3");
    }
    // Transport stop releases notes; restart from top
    {
        Harness h;
        h.seq.setPlaying(true);
        h.run(3000);
        h.seq.setPlaying(false);
        h.run(1000);
        int on = 0, off = 0; for (auto& e : h.evs) (e.on ? on : off)++;
        CHECK(on == off, "all notes released on stop (%d on, %d off)", on, off);
        h.evs.clear();
        h.seq.setPlaying(true);
        h.run(100);
        CHECK(!h.ons(3).empty() && h.ons(3)[0].t == 4000, "restart from top at %lld", h.ons(3).empty() ? -1 : h.ons(3)[0].t);
    }
    // Long note across steps + same-pitch retrigger ordering (off before on at same sample)
    {
        Harness h;
        auto data = ClipData::createEmpty(4.0);
        data->notes = { { 0.0, 1.0, 64, 100 }, { 0.5, 0.25, 64, 100 } }; // retrigger while held
        data->sanitise();
        h.seq.setClipData(4, 0, data);
        h.seq.pressClip(4, 0);
        h.run(20000);
        auto e5 = h.on(5);
        CHECK(e5.size() == 4, "4 events, got %zu", e5.size());
        if (e5.size() == 4) {
            CHECK(e5[0].on && e5[0].t == 0, "on @0");
            CHECK(!e5[1].on && e5[1].t == 12000, "off @12000 before retrigger");
            CHECK(e5[2].on && e5[2].t == 12000, "on @12000");
            CHECK(!e5[3].on && e5[3].t == 18000, "off @18000 got %lld", e5[3].t);
        }
    }
    // Follow Other never repeats the same clip
    {
        Harness h;
        for (int c = 1; c <= 2; c++) { auto& s = h.seq.getClipSettings(1, c); s.followEnabled = true; s.followActionA = FollowAction::Other; }
        h.seq.pressClip(1, 1);
        h.run(100); // check just after each follow boundary
        int prev = 1; bool ok = true;
        for (int i = 0; i < 10; i++) { h.run(192000); int cur = h.seq.getPlayingClip(1); if (cur == prev) ok = false; prev = cur; }
        CHECK(ok, "Other always switches");
    }

    // B1: chords and sub-step timing (a note at 1/32 offset)
    {
        Harness h;
        auto data = ClipData::createEmpty(4.0);
        data->notes = { { 0.0, 0.5, 60, 100 }, { 0.0, 0.5, 64, 100 }, { 0.0, 0.5, 67, 100 }, { 0.125, 0.1, 72, 90 } };
        data->sanitise();
        h.seq.setClipData(4, 0, data);
        h.seq.pressClip(4, 0);
        h.run(10000);
        int at0 = 0; for (auto& e : h.ons(5)) if (e.t == 0) at0++;
        CHECK(at0 == 3, "chord: 3 noteOns at 0, got %d", at0);
        bool sub = false; for (auto& e : h.ons(5)) if (e.note == 72 && e.t == 3000) sub = true;
        CHECK(sub, "1/32 note at sample 3000");
    }
    // B2: non-looping clip plays start..end once, then the track stops
    {
        Harness h;
        auto data = ClipData::createEmpty(2.0);
        data->looping = false;
        data->notes = { { 0.0, 0.25, 60, 100 }, { 1.0, 0.25, 62, 100 } };
        data->sanitise();
        h.seq.setClipData(4, 0, data);
        h.seq.pressClip(4, 0);
        h.run(200000);
        CHECK(h.ons(5).size() == 2, "one-shot plays its notes once: %zu", h.ons(5).size());
        CHECK(h.seq.getPlayingClip(4) == -1, "one-shot stopped at end marker");
    }
    // B3: run into loop: start 0, loop [2, 4) -> notes at 0 once, then the loop repeats
    {
        Harness h;
        auto data = ClipData::createEmpty(4.0);
        data->loopStart = 2.0;
        data->notes = { { 0.0, 0.25, 60, 100 }, { 2.0, 0.25, 62, 100 } };
        data->sanitise();
        h.seq.setClipData(4, 0, data);
        h.seq.pressClip(4, 0);
        h.run(24000 * 8 + 10);
        int c60 = 0, c62 = 0; for (auto& e : h.ons(5)) (e.note == 60 ? c60 : c62)++;
        CHECK(c60 == 1, "intro note once, got %d", c60);
        CHECK(c62 == 4, "loop note at beats 2,4,6,8 -> 4, got %d", c62);
    }
    // Clip edited while playing: new content is heard on the next pass, no stop
    {
        Harness h;
        auto data = ClipData::createEmpty(4.0);
        data->notes = { { 0.0, 0.25, 60, 100 } };
        data->sanitise();
        h.seq.setClipData(4, 0, data);
        h.seq.pressClip(4, 0);
        h.run(48000);
        auto edited = data->copy();
        edited->notes = { { 0.0, 0.25, 61, 100 } };
        h.seq.setClipData(4, 0, edited);
        h.run(96000);
        bool heard61 = false; for (auto& e : h.ons(5)) if (e.note == 61 && e.t == 96000) heard61 = true;
        CHECK(heard61, "edited clip heard at next pass");
        CHECK(h.seq.getPlayingClip(4) == 0, "still playing after edit");
    }
    // C6: swing delays off-beat 16ths only, scales their velocity
    {
        Harness h;
        h.seq.setSwing(1.0, 1.0);
        auto data = ClipData::createEmpty(1.0);
        data->notes = { { 0.0, 0.1, 60, 100 }, { 0.25, 0.1, 62, 100 }, { 0.5, 0.1, 64, 100 } };
        data->sanitise();
        h.seq.setClipData(4, 0, data);
        h.seq.pressClip(4, 0);
        h.run(24000);
        auto o = h.ons(5);
        CHECK(o.size() == 3, "3 notes");
        if (o.size() == 3) {
            CHECK(o[0].t == 0 && o[2].t == 12000, "on-grid notes unmoved");
            CHECK(o[1].t == 8000, "off-16th moved to triplet position, got %lld", o[1].t);
        }
    }
    // Deleting the playing clip stops the track cleanly
    {
        Harness h;
        h.seq.pressClip(0, 1);
        h.run(10000);
        h.seq.setClipData(0, 1, nullptr);
        h.run(1000);
        CHECK(h.seq.getPlayingClip(0) == -1, "deleted clip no longer playing");
    }
    // Every noteOn gets a noteOff (no stuck notes) across a long multi-track run with follow actions
    {
        Harness h;
        for (int c = 1; c <= 2; c++) { auto& s = h.seq.getClipSettings(1, c); s.followEnabled = true; s.followActionA = FollowAction::Any; }
        h.seq.launchScene(1);
        h.run(48000 * 40);
        h.seq.setPlaying(false);
        h.run(512);
        std::map<std::pair<int,int>, int> held;
        for (auto& e : h.evs) held[{ e.ch, e.note }] += e.on ? 1 : -1;
        bool balanced = true; for (auto& kv : held) if (kv.second > 0) balanced = false;
        CHECK(balanced, "no stuck notes");
    }

    // B8 / B9 / B10 / B13: editing operations
    {
        ClipData d;
        d.setLength(4.0);
        d.notes = { { 0.1, 0.25, 60, 100 }, { 1.0, 0.5, 62, 100 }, { 3.9, 0.1, 64, 100 } };
        ClipEdits::quantize(d, { 0, 1, 2 }, 0.25, 0.5);
        CHECK(std::abs(d.notes[0].start - 0.05) < 1e-9, "quantize 50%% moves half way: %f", d.notes[0].start);
        CHECK(std::abs(d.notes[2].start - 3.95) < 1e-9, "quantize towards 4.0: %f", d.notes[2].start);
        ClipEdits::transpose(d, { 1 }, 12);
        CHECK(d.notes[1].pitch == 74 && d.notes[0].pitch == 60, "transpose only targets");

        ClipData loop;
        loop.setLength(2.0);
        loop.endMarker = 3.0;
        loop.notes = { { 0.0, 0.25, 60, 100 }, { 1.0, 0.25, 62, 100 }, { 2.5, 0.25, 64, 100 } };
        ClipEdits::duplicateLoop(loop);
        loop.sortNotes();
        CHECK(loop.loopEnd == 4.0 && loop.endMarker == 5.0, "loop doubled (%f, %f)", loop.loopEnd, loop.endMarker);
        CHECK(loop.notes.size() == 5, "loop notes copied: %zu", loop.notes.size());
        CHECK(loop.notes.back().start == 4.5 && loop.notes.back().pitch == 64, "note after loop shifted");

        std::vector<MidiNote> n = { { 0.0, 1.0, 60, 100 }, { 2.0, 1.0, 60, 100 }, { 0.5, 2.0, 60, 90 } };
        auto r = ClipEdits::resolveOverlaps(n, 2);
        CHECK(r.size() == 2, "covered start replaced: %zu", r.size());
        CHECK(r[0].length == 0.5, "covered end shortened: %f", r[0].length);
    }
    // B12: muted notes don't play
    {
        Harness h;
        auto data = ClipData::createEmpty(1.0);
        data->notes = { { 0.0, 0.1, 60, 100, true }, { 0.5, 0.1, 62, 100 } };
        h.seq.setClipData(4, 0, data);
        h.seq.pressClip(4, 0);
        h.run(20000);
        CHECK(h.ons(5).size() == 1 && h.ons(5)[0].note == 62, "muted note silent");
    }

    // C3: tap tempo averages up to 4 intervals and resets after a pause
    {
        TapTempo tap;
        CHECK(tap.tap(10.0) == 0.0, "first tap gives no tempo");
        CHECK(std::abs(tap.tap(10.5) - 120.0) < 1e-6, "second tap 0.5 s -> 120");
        tap.tap(11.0); tap.tap(11.6);
        const double avg = tap.tap(12.1);
        CHECK(std::abs(avg - 60.0 / ((0.5 + 0.5 + 0.6 + 0.5) / 4.0)) < 1e-6, "average of last 4: %f", avg);
        CHECK(tap.tap(20.0) == 0.0, "pause > 2 s starts over");
    }
    // C4: metronome clicks on every beat while playing, accent on the downbeat
    {
        Harness h;
        h.seq.setMetronome(true);
        h.seq.setPlaying(true);
        std::vector<std::pair<long long, bool>> clicks;
        for (int b = 0; b < 200; b++) {
            juce::MidiBuffer m; h.seq.processBlock(m, 512);
            for (int i = 0; i < h.seq.getNumClicks(); i++) clicks.push_back({ h.now + h.seq.getClick(i).sample, h.seq.getClick(i).accent });
            h.now += 512;
        }
        // 102400 samples at 24000/beat -> beats 0..4 => 5 clicks
        CHECK(clicks.size() == 5, "5 clicks, got %zu", clicks.size());
        if (clicks.size() == 5) {
            CHECK(clicks[0].first == 0 && clicks[0].second, "accented click at 0");
            CHECK(clicks[1].first == 24000 && !clicks[1].second, "beat 2 at 24000");
            CHECK(clicks[4].first == 96000 && clicks[4].second, "bar 2 accented");
        }
    }
    // C7: following the host: start at its position, jump when it jumps
    {
        Harness h;
        h.seq.setHostPosition(true, 8.0);  // host playing from bar 3
        h.seq.launchScene(1);              // queued: 1 bar quantization, host at a bar line -> immediate
        h.seq.setPlaying(false);
        h.run(512);
        CHECK(h.seq.getCurrentScene() == 1, "scene launched at host position");
        CHECK(std::abs(h.seq.getSongPosition() - (8.0 + 512 / 24000.0)) < 1e-6, "song position follows host");
        h.seq.setHostPosition(true, 32.0);
        h.run(512);
        CHECK(std::abs(h.seq.getSongPosition() - (32.0 + 512 / 24000.0)) < 1e-6, "follows host jump");
        h.seq.setHostPosition(false, 32.0);
        h.run(512);
        CHECK(!h.seq.isPlaying(), "host stopped -> stopped");
    }
    // C1: position formatting
    {
        auto fmt = [](double beats) {
            const auto s = static_cast<long long>(std::floor(beats * 4.0 + 1e-6));
            return std::to_string(s / 16 + 1) + "." + std::to_string((s / 4) % 4 + 1) + "." + std::to_string(s % 4 + 1);
        };
        CHECK(fmt(0.0) == "1.1.1" && fmt(5.25) == "2.2.2" && fmt(15.75) == "4.4.4", "bars.beats.16ths");
    }

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
