#include <juce_audio_processors/juce_audio_processors.h>
#include "Sequencer/StepSequencer.h"
#include "Sequencer/TapTempo.h"
#include "Mixer/Mixer.h"
#include "Sequencer/Recorder.h"
#include "Project/SetSerializer.h"
#include "Engine/SoundSources/AndroSynth.h"
#include "Engine/SoundSources/DrumKit.h"
#include "Engine/Processors/TrackEffects.h"
#include "Mixer/Automation.h"

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
    // Input MIDI to inject: absolute sample time -> message.
    std::vector<std::pair<long long, juce::MidiMessage>> input;

    void run(long long samples) {
        while (samples > 0) {
            int n = (int) std::min<long long>(BLOCK, samples);
            juce::MidiBuffer m, in;
            for (auto& [time, message] : input)
                if (time >= now && time < now + n) in.addEvent(message, (int) (time - now));
            seq.processBlock(m, n, &in);
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

    // D1-D6: mixer gains, pan, activator, solo, pre/post sends
    {
        auto runMixer = [](Mixer& mixer, std::array<float, NUM_TRACKS> inputs, int blocks = 3) {
            std::array<juce::AudioBuffer<float>, NUM_TRACKS> tracks;
            for (int t = 0; t < NUM_TRACKS; t++) {
                tracks[(size_t) t].setSize(2, 256);
                for (int ch = 0; ch < 2; ch++) for (int i = 0; i < 256; i++) tracks[(size_t) t].setSample(ch, i, inputs[(size_t) t]);
            }
            juce::AudioBuffer<float> out(2, 256);
            for (int b = 0; b < blocks; b++) mixer.mix(out, tracks); // let gain ramps settle
            return std::make_pair(out.getSample(0, 255), out.getSample(1, 255));
        };
        Mixer mixer; mixer.prepare(48000.0); mixer.setMasterVolume(1.0f);
        mixer.setReturnLevel(0, 0.0f); mixer.setReturnLevel(1, 0.0f);
        std::array<float, NUM_TRACKS> in {}; in[0] = 0.5f; in[1] = 0.25f;
        auto lr = runMixer(mixer, in);
        CHECK(std::abs(lr.first - 0.75f) < 1e-4 && std::abs(lr.second - 0.75f) < 1e-4, "unity sum %f %f", lr.first, lr.second);
        mixer.setTrackPan(0, 1.0f);
        lr = runMixer(mixer, in);
        CHECK(std::abs(lr.first - 0.25f) < 1e-4 && std::abs(lr.second - 0.75f) < 1e-4, "hard right pan %f %f", lr.first, lr.second);
        mixer.setTrackPan(0, 0.0f);
        mixer.setTrackActive(1, false);
        lr = runMixer(mixer, in);
        CHECK(std::abs(lr.first - 0.5f) < 1e-4, "activator off mutes track 2: %f", lr.first);
        mixer.setTrackActive(1, true);
        mixer.setTrackSolo(1, true);
        lr = runMixer(mixer, in);
        CHECK(std::abs(lr.first - 0.25f) < 1e-4, "solo leaves only track 2: %f", lr.first);
        mixer.setTrackSolo(1, false);
        CHECK(mixer.takeMasterPeak() > 0.0f && mixer.takeMasterPeak() == 0.0f, "peak meter reads and resets");
    }
    // Sends: post follows mute, pre doesn't (delay return measured through its tail)
    {
        auto energyWithMute = [](bool pre) {
            Mixer mixer; mixer.prepare(48000.0); mixer.setMasterVolume(1.0f);
            mixer.setReturnLevel(Mixer::REVERB_RETURN, 0.0f);
            mixer.setReturnPre(Mixer::DELAY_RETURN, pre);
            mixer.setTrackDelaySend(0, 1.0f);
            mixer.setTrackActive(0, false);
            std::array<juce::AudioBuffer<float>, NUM_TRACKS> tracks;
            for (auto& b : tracks) { b.setSize(2, 512); b.clear(); }
            for (int i = 0; i < 512; i++) { tracks[0].setSample(0, i, 0.5f); tracks[0].setSample(1, i, 0.5f); }
            juce::AudioBuffer<float> out(2, 512);
            double energy = 0.0;
            for (int b = 0; b < 200; b++) {
                mixer.mix(out, tracks);
                for (int i = 0; i < 512; i++) energy += std::abs(out.getSample(0, i));
            }
            return energy;
        };
        CHECK(energyWithMute(false) < 1e-3, "muted track: post send silent");
        CHECK(energyWithMute(true) > 1.0, "muted track: pre send still feeds the return");
    }

    // E1/E2/E5: record a new clip into an armed track's empty slot; stop -> it loops
    {
        Harness h;
        Recorder rec(h.seq, [&](int t, int slot, ClipData::Ptr, ClipData::Ptr after) { h.seq.setClipData(t, slot, after); });
        h.seq.setArmed(4, true);
        rec.recordIntoSlot(4, 0);                         // starts transport; records from 1.1.1
        h.input.push_back({ 6000, juce::MidiMessage::noteOn(1, 64, (juce::uint8) 90) });  // beat 0.25
        h.input.push_back({ 18000, juce::MidiMessage::noteOff(1, 64) });                 // beat 0.75
        h.run(48000);
        rec.drain();
        CHECK(h.seq.getRecordingSlot(4) == 0, "recording");
        bool monitored = false; for (auto& e : h.ons(5)) if (e.note == 64 && e.t == 6000) monitored = true;
        CHECK(monitored, "input monitored on the armed track (channel 5)");
        h.seq.requestStopRecord(4);
        h.run(96000 + 6500 - 48000 + 100);                  // past the bar line + the first note again
        rec.drain();
        auto d = h.seq.getClipData(4, 0);
        CHECK(d != nullptr && d->notes.size() == 1, "one note recorded");
        if (d != nullptr && d->notes.size() == 1) {
            CHECK(std::abs(d->notes[0].start - 0.25) < 1e-3 && std::abs(d->notes[0].length - 0.5) < 1e-3, "note timing %f %f", d->notes[0].start, d->notes[0].length);
            CHECK(std::abs(d->loopEnd - 4.0) < 1e-9, "clip length = 1 bar (stopped at the bar line): %f", d->loopEnd);
        }
        CHECK(h.seq.getPlayingClip(4) == 0 && h.seq.getRecordingSlot(4) == -1, "recorded clip now looping");
        int played = 0; for (auto& e : h.ons(5)) if (e.note == 64 && e.t > 96000) played++;
        CHECK(played == 1, "recorded note plays back on the next pass: %d", played);
    }
    // E3/E4: overdub into a playing clip with record quantization
    {
        Harness h;
        Recorder rec(h.seq, [&](int t, int slot, ClipData::Ptr, ClipData::Ptr after) { h.seq.setClipData(t, slot, after); });
        rec.setQuantization(0.25);
        h.seq.setArmed(0, true);
        h.seq.pressClip(0, 1);                            // kick, 1 bar
        h.run(1000);
        rec.toggleSessionRecord(0);                       // playing clip -> overdub
        h.input.push_back({ 13000, juce::MidiMessage::noteOn(1, 50, (juce::uint8) 100) });  // beat ~0.54 -> 0.5
        h.input.push_back({ 16000, juce::MidiMessage::noteOff(1, 50) });
        h.run(30000);
        rec.toggleSessionRecord(0);                       // stop overdub
        h.run(1000);
        rec.drain();
        auto d = h.seq.getClipData(0, 1);
        bool found = false; for (auto& n : d->notes) if (n.pitch == 50 && std::abs(n.start - 0.5) < 1e-9) found = true;
        CHECK(found, "overdubbed note quantized to 0.5");
        CHECK(d->notes.size() == 5, "kick notes kept + 1: %zu", d->notes.size());
        CHECK(h.seq.getRecordingSlot(0) == -1, "overdub stopped");
    }
    // C5: count-in delays recording and shows a negative position
    {
        Harness h;
        Recorder rec(h.seq, [&](int t, int slot, ClipData::Ptr, ClipData::Ptr after) { h.seq.setClipData(t, slot, after); });
        h.seq.setCountInBars(1);
        h.seq.setArmed(4, true);
        rec.recordIntoSlot(4, 0);
        h.run(512);
        CHECK(h.seq.getSongPosition() < 0.0, "negative position during count-in: %f", h.seq.getSongPosition());
        int clicks = 0;
        for (int b = 0; b < 100; b++) { juce::MidiBuffer m; h.seq.processBlock(m, 512); clicks += h.seq.getNumClicks(); h.now += 512; }
        CHECK(clicks >= 2, "count-in clicks even with the metronome off: %d", clicks);
        h.run(96000);
        CHECK(h.seq.getSongPosition() > 0.0 && h.seq.getRecordingSlot(4) == 0, "recording after count-in");
        bool early = false; for (auto& e : h.ons(3)) if (e.t < 96000) early = true;
        CHECK(!early && !h.ons(3).empty(), "clips start at 1.1.1 after the 1-bar count-in");
    }

    // F1: arrangement playback on a track with no session clip; render loops content, cuts at end
    {
        auto content = ClipData::createEmpty(1.0);
        content->notes = { { 0.0, 0.25, 60, 100 }, { 0.5, 0.25, 62, 100 } };
        auto arr = ArrangementData::Ptr(new ArrangementData());
        arr->place(4, { 4.0, 2.25, content, 0.0, 0 });
        arr->render();
        CHECK(arr->rendered[4].size() == 5, "looped 1-beat clip over 2.25 beats -> 5 notes, got %zu", arr->rendered[4].size());
        CHECK(std::abs(arr->rendered[4].back().start - 6.0) < 1e-9 && std::abs(arr->rendered[4].back().length - 0.25) < 1e-9, "last note cut at clip end");

        Harness h;
        h.seq.setArrangement(arr);
        h.seq.setPlaying(true);
        h.run(24000 * 7);
        auto o = h.ons(5);
        CHECK(o.size() == 5 && o[0].t == 96000, "arrangement notes from beat 4: %zu, first %lld", o.size(), o.empty() ? -1 : o[0].t);
    }
    // F4: session launch overrides the arrangement; Back to Arrangement resumes with chase
    {
        auto content = ClipData::createEmpty(8.0);
        content->notes = { { 0.0, 8.0, 48, 100 } };                // one long note
        auto arr = ArrangementData::Ptr(new ArrangementData());
        arr->place(0, { 0.0, 8.0, content, 0.0, 0 });
        arr->render();
        Harness h;
        h.seq.setArrangement(arr);
        h.seq.setQuantizationIndex(0);
        h.seq.setPlaying(true);
        h.run(1000);
        CHECK(!h.ons(1).empty() && h.ons(1)[0].note == 48, "arrangement plays");
        h.seq.pressClip(0, 1);                                    // session kick
        h.run(2000);
        CHECK(!h.seq.followsArrangement(0) && h.seq.isArrangementOverridden(), "track overridden");
        bool off48 = false; for (auto& e : h.offs(1)) if (e.note == 48) off48 = true;
        CHECK(off48, "arrangement note released when the session takes over");
        h.seq.requestBackToArrangement();
        h.run(1000);
        CHECK(h.seq.followsArrangement(0) && !h.seq.isArrangementOverridden(), "back to arrangement");
        int ons48 = 0; for (auto& e : h.ons(1)) if (e.note == 48) ons48++;
        CHECK(ons48 == 2, "long note chased on return: %d", ons48);
    }
    // F2: arrangement loop brace
    {
        auto content = ClipData::createEmpty(4.0);
        content->notes = { { 1.0, 0.25, 70, 100 } };
        auto arr = ArrangementData::Ptr(new ArrangementData());
        arr->place(4, { 0.0, 16.0, content, 0.0, 0 });
        arr->render();
        Harness h;
        h.seq.setArrangement(arr);
        h.seq.setLoop(true, 0.0, 2.0);
        h.seq.setPlaying(true);
        h.run(24000 * 8);
        auto o = h.ons(5);
        CHECK(o.size() == 4, "beat 1 note every 2-beat loop: %zu", o.size());
        CHECK(h.seq.getSongPosition() <= 2.0 + 1e-9, "position stays inside the loop: %f", h.seq.getSongPosition());
    }
    // F3: arrangement record logs session launches
    {
        Harness h;
        ArrangementData::Ptr committed;
        ArrangementLogger logger(h.seq, [&](ArrangementData::Ptr, ArrangementData::Ptr after) { committed = after; h.seq.setArrangement(after); });
        h.seq.setArrangementRecord(true);                         // starts the transport; scene 1 arp playing
        h.run(96000);                                             // 4 beats
        h.seq.pressClip(0, 1);                                    // kick from bar 2 (1 bar quantization)
        h.run(96000 + 48000);
        h.seq.setPlaying(false);
        h.run(512);
        logger.drain();
        CHECK(committed != nullptr, "pass committed");
        if (committed != nullptr) {
            CHECK(committed->clips[2].size() == 1 && committed->clips[2][0].start == 0.0, "arp logged from 1.1.1");
            CHECK(committed->clips[0].size() == 1 && std::abs(committed->clips[0][0].start - 4.0) < 1e-6, "kick logged at bar 2: %f",
                  committed->clips[0].empty() ? -1.0 : committed->clips[0][0].start);
            CHECK(!committed->clips[0].empty() && std::abs(committed->clips[0][0].length - 6.0) < 0.05, "kick length up to stop: %f",
                  committed->clips[0].empty() ? -1.0 : committed->clips[0][0].length);
        }
        CHECK(!h.seq.isArrangementRecording(), "arrangement record ends with the transport");
    }
    // F1 / C8: playback from the insert marker; stop twice returns to 1.1.1
    {
        Harness h;
        h.seq.setStartPosition(8.0);
        h.seq.setPlaying(true);
        h.run(512);
        CHECK(h.seq.getSongPosition() > 8.0 && h.seq.getSongPosition() < 8.1, "starts at the insert marker");
        h.seq.pressStop();
        h.run(512);
        CHECK(h.seq.getStartPosition() == 8.0, "first stop keeps the start position");
        h.seq.pressStop();
        CHECK(h.seq.getStartPosition() == 0.0, "second stop -> 1.1.1");
    }
    // Arrangement placement trims what it covers
    {
        auto content = ClipData::createEmpty(4.0);
        ArrangementData arr;
        arr.place(0, { 0.0, 8.0, content, 0.0, 0 });
        arr.place(0, { 2.0, 2.0, content, 0.0, 0 });
        CHECK(arr.clips[0].size() == 3, "split around the new clip: %zu", arr.clips[0].size());
        if (arr.clips[0].size() == 3) {
            CHECK(arr.clips[0][0].length == 2.0 && arr.clips[0][2].start == 4.0 && arr.clips[0][2].offset == 4.0, "head / tail trimmed with offset");
        }
    }

    // G1/G2: a set survives save -> XML -> load (clips, settings, arrangement, mixer, transport)
    {
        StepSequencer a; Mixer ma;
        a.setTempo(133.5); a.setQuantizationIndex(3); a.setSwing(0.4, 0.2); a.setLoop(true, 4.0, 12.0);
        a.setStartPosition(8.0); a.setCountInBars(2); a.setMetronome(true);
        auto clip = ClipData::createEmpty(8.0);
        clip->name = "Lead"; clip->looping = false; clip->colour = 0xFF112233;
        clip->notes = { { 0.5, 1.25, 72, 99, false }, { 2.0, 0.25, 74, 64, true } };
        clip->sanitise();
        a.setClipData(1, 5, clip);
        a.getClipSettings(1, 5).launchMode = LaunchMode::Gate;
        a.getClipSettings(1, 5).followEnabled = true;
        a.getClipSettings(1, 5).followActionA = FollowAction::Other;
        auto arr = ArrangementData::Ptr(new ArrangementData());
        arr->place(2, { 16.0, 6.0, clip, 1.0, 5 });
        arr->render();
        a.setArrangement(arr);
        ma.setTrackPan(1, -0.5f); ma.setTrackActive(2, false); ma.setReturnPre(1, true); ma.setTrackDelaySend(0, 0.3f);
        SetSerializer::Extras extras { 0.8f, false, true, 0.25 };

        const auto xml = SetSerializer::save(a, ma, extras).createXml();
        CHECK(xml != nullptr, "set serialises to XML");
        StepSequencer b; Mixer mb; SetSerializer::Extras loaded;
        CHECK(SetSerializer::load(juce::ValueTree::fromXml(*juce::XmlDocument::parse(xml->toString())), b, mb, loaded), "set loads");
        CHECK(std::abs(b.getTempo() - 133.5) < 1e-9 && b.getQuantizationIndex() == 3 && b.getCountInBars() == 2, "transport settings");
        CHECK(b.isLoopEnabled() && b.getLoopStart() == 4.0 && b.getLoopEnd() == 12.0 && b.getStartPosition() == 8.0, "loop + insert marker");
        auto c = b.getClipData(1, 5);
        CHECK(c != nullptr && c->name == "Lead" && !c->looping && c->colour == 0xFF112233 && c->notes.size() == 2, "clip content");
        if (c != nullptr && c->notes.size() == 2) {
            CHECK(c->notes[0].start == 0.5 && c->notes[0].length == 1.25 && c->notes[0].velocity == 99 && c->notes[1].muted, "notes exact");
        }
        CHECK(b.getClipSettings(1, 5).launchMode == LaunchMode::Gate && b.getClipSettings(1, 5).followActionA == FollowAction::Other, "launch settings");
        auto ba = b.getArrangement();
        CHECK(ba->clips[2].size() == 1 && ba->clips[2][0].start == 16.0 && ba->clips[2][0].offset == 1.0, "arrangement clip");
        CHECK(ba->rendered[2].size() == arr->rendered[2].size(), "arrangement re-rendered");
        CHECK(mb.getTrackPan(1) == -0.5f && !mb.isTrackActive(2) && mb.isReturnPre(1) && std::abs(mb.getTrackDelaySend(0) - 0.3f) < 1e-6, "mixer");
        CHECK(loaded.metronomeVolume == 0.8f && !loaded.exclusiveSolo && loaded.recordQuantization == 0.25, "extras");
        CHECK(b.getPlayingClip(0) == -1 && b.followsArrangement(0), "loaded sets open with tracks following the arrangement");
        CHECK(!SetSerializer::load(juce::ValueTree("SOMETHING_ELSE"), b, mb, loaded), "rejects non-sets");
    }
    // G4: MIDI export
    {
        auto clip = ClipData::createEmpty(4.0);
        clip->notes = { { 1.0, 0.5, 60, 100 }, { 2.0, 0.5, 62, 90, true } };
        auto file = juce::File::createTempFile(".mid");
        CHECK(SetSerializer::exportMidiClip(*clip, 120.0, file), "midi written");
        juce::FileInputStream in(file);
        juce::MidiFile midi;
        CHECK(midi.readFrom(in), "midi readable");
        int noteOns = 0; double firstOn = -1;
        for (const auto* e : *midi.getTrack(0)) if (e->message.isNoteOn()) { if (noteOns++ == 0) firstOn = e->message.getTimeStamp(); }
        CHECK(noteOns == 1 && firstOn == 960.0, "one unmuted note at beat 1 (960 ticks): %d %f", noteOns, firstOn);
        file.deleteFile();
    }

    // A8: per-clip quantization overrides the global one
    {
        Harness h;
        h.seq.getClipSettings(0, 1).quantization = 3;            // 1/4 (global stays 1 bar)
        h.seq.setPlaying(true);
        h.run(30000);
        h.seq.pressClip(0, 1);
        h.run(40000);
        CHECK(!h.ons(1).empty() && h.ons(1)[0].t == 48000, "clip quantized to the next beat: %lld", h.ons(1).empty() ? -1 : h.ons(1)[0].t);
    }
    // A8: legato takes over the play position
    {
        Harness h;
        h.seq.setQuantizationIndex(0);
        h.seq.getClipSettings(1, 2).legato = true;
        h.seq.pressClip(1, 1);                                    // 2-bar bass
        h.run(24000 * 5);                                         // 5 beats in
        h.seq.pressClip(1, 2);
        h.run(512);
        CHECK(std::abs(h.seq.getClipPlayPosition(1) - 5.0) < 0.05, "legato launch continues at beat 5: %f", h.seq.getClipPlayPosition(1));
    }
    // A8: jump follow action + global follow switch
    {
        Harness h;
        auto& s = h.seq.getClipSettings(0, 1);
        s.followEnabled = true; s.followActionA = FollowAction::Jump; s.jumpTarget = 2;
        h.seq.pressClip(0, 1);
        h.run(96000 + 100);
        CHECK(h.seq.getPlayingClip(0) == 2, "jump to slot 3: %d", h.seq.getPlayingClip(0));

        Harness off;
        off.seq.setFollowActionsEnabled(false);
        auto& s2 = off.seq.getClipSettings(0, 1);
        s2.followEnabled = true; s2.followActionA = FollowAction::Next;
        off.seq.pressClip(0, 1);
        off.run(96000 * 2 + 100);
        CHECK(off.seq.getPlayingClip(0) == 1, "follow actions switched off globally");
    }

    // G5: instruments make sound for their notes and go quiet after release / their decay
    {
        auto renderNote = [](juce::Synthesiser& synth, int note, int samples) {
            synth.setCurrentPlaybackSampleRate(48000.0);
            juce::AudioBuffer<float> buffer(2, samples);
            buffer.clear();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, note), samples / 2);
            synth.renderNextBlock(buffer, midi, 0, samples);
            return std::make_pair(buffer.getMagnitude(0, 0, samples / 2), buffer.getMagnitude(0, samples - 2000, 2000));
        };
        TrackDevices synthDevice;
        juce::Synthesiser synth;
        for (int i = 0; i < 4; i++) synth.addVoice(new AndroSynthVoice(synthDevice.instrumentParams));
        synth.addSound(new AndroSynthSound(1));
        auto [on, tail] = renderNote(synth, 60, 48000);
        CHECK(on > 0.01f && tail < 1e-4f, "synth sounds and releases: %f %f", on, tail);

        TrackDevices drumDevice;
        drumDevice.instrumentParams.resetTo(instrumentSpecs(InstrumentType::Drums));
        juce::Synthesiser drums;
        for (int i = 0; i < 4; i++) drums.addVoice(new DrumKitVoice(drumDevice.instrumentParams));
        drums.addSound(new DrumKitSound(1));
        for (int note : { 36, 37, 38, 39, 42, 45, 46, 56 }) {
            auto [hit, end] = renderNote(drums, note, 96000);
            CHECK(hit > 0.01f && end < 1e-4f, "drum %d sounds then decays: %f %f", note, hit, end);
        }
    }
    // G6: track filter and compressor
    {
        TrackDevices device;
        TrackEffects fx; fx.prepare(48000.0);
        auto toneLevel = [&](double frequency) {
            juce::AudioBuffer<float> buffer(2, 9600);
            for (int i = 0; i < 9600; i++) {
                const float v = (float) (0.5 * std::sin(juce::MathConstants<double>::twoPi * frequency * i / 48000.0));
                buffer.setSample(0, i, v); buffer.setSample(1, i, v);
            }
            fx.reset();
            fx.process(buffer, 9600, device.effectParams);
            return buffer.getMagnitude(0, 4800, 4800);
        };
        CHECK(std::abs(toneLevel(5000.0) - 0.5f) < 1e-3f, "filter off: untouched");
        device.effectParams.set(EffectParam::FilterMode, 1.0f);
        device.effectParams.set(EffectParam::Cutoff, 500.0f);
        device.effectParams.set(EffectParam::Resonance, 0.0f);
        CHECK(toneLevel(5000.0) < 0.05f && toneLevel(100.0) > 0.4f, "low-pass cuts highs, keeps lows");
        device.effectParams.set(EffectParam::FilterMode, 0.0f);
        device.effectParams.set(EffectParam::Threshold, -20.0f);
        device.effectParams.set(EffectParam::Ratio, 10.0f);
        const float compressed = toneLevel(200.0);
        CHECK(compressed < 0.2f && fx.getGainReductionDb() > 5.0, "compressor reduces a loud signal: %f (%f dB)", compressed, fx.getGainReductionDb());
    }
    // Devices saved with the set
    {
        StepSequencer a; Mixer m;
        SetSerializer::Extras extras;
        extras.devices[1].present = true;
        extras.devices[1].instrument = (int) InstrumentType::Drums;
        extras.devices[1].instrumentValues[DrumParam::Tune] = 1.5f;
        extras.devices[1].effectValues[EffectParam::FilterMode] = 2.0f;
        auto tree = SetSerializer::save(a, m, extras);
        StepSequencer b; Mixer mb; SetSerializer::Extras loaded;
        SetSerializer::load(tree, b, mb, loaded);
        CHECK(loaded.devices[1].present && loaded.devices[1].instrument == (int) InstrumentType::Drums
              && loaded.devices[1].instrumentValues[DrumParam::Tune] == 1.5f && loaded.devices[1].effectValues[EffectParam::FilterMode] == 2.0f,
              "devices round-trip");
    }

    // G7: automation envelopes drive the mixer; a manual move overrides until re-enabled
    {
        ArrangementData arr;
        arr.automation[0][AUTOMATE_VOLUME] = { { 0.0, 0.0f }, { 4.0, 1.0f } };
        arr.automation[1][AUTOMATE_PAN] = { { 2.0, -1.0f } };
        CHECK(std::abs(ArrangementData::valueAt(arr.automation[0][AUTOMATE_VOLUME], 1.0) - 0.25f) < 1e-6, "linear ramp");
        CHECK(ArrangementData::valueAt(arr.automation[0][AUTOMATE_VOLUME], 9.0) == 1.0f, "held after last point");
        CHECK(ArrangementData::valueAt(arr.automation[1][AUTOMATE_PAN], 0.0) == -1.0f, "held before first point");

        Mixer mixer;
        AutomationPlayer player;
        player.apply(arr, 2.0, mixer);
        CHECK(std::abs(mixer.getTrackVolume(0) - 0.5f) < 1e-6 && mixer.getTrackPan(1) == -1.0f, "applied to the mixer");
        CHECK(mixer.getTrackVolume(2) == 1.0f, "unautomated tracks untouched");
        player.touched(&arr, 0, AUTOMATE_VOLUME);
        mixer.setTrackVolume(0, 0.9f);
        player.apply(arr, 3.0, mixer);
        CHECK(mixer.getTrackVolume(0) == 0.9f && player.anyOverridden(), "manual move overrides automation");
        player.touched(&arr, 2, AUTOMATE_VOLUME);
        CHECK(!player.isOverridden(2, AUTOMATE_VOLUME), "touching an unautomated control is no override");
        player.reenableAll();
        player.apply(arr, 3.0, mixer);
        CHECK(std::abs(mixer.getTrackVolume(0) - 0.75f) < 1e-6, "re-enabled automation applies again");

        StepSequencer a; Mixer m; SetSerializer::Extras extras;
        auto data = ArrangementData::Ptr(new ArrangementData(arr));
        a.setArrangement(data);
        auto tree = SetSerializer::save(a, m, extras);
        StepSequencer b; Mixer mb; SetSerializer::Extras loaded;
        SetSerializer::load(tree, b, mb, loaded);
        const auto& points = b.getArrangement()->automation[0][AUTOMATE_VOLUME];
        CHECK(points.size() == 2 && points[1].beat == 4.0 && points[1].value == 1.0f, "automation saved with the set");
    }

    std::printf(failures == 0 ? "ALL PASSED\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
