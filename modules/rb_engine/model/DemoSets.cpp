namespace rb
{

namespace
{
    const TrackPreset* firstPreset (const juce::String& category, InstrumentType type)
    {
        for (auto& p : getFactoryPresets())
            if (p.category == category && p.type == type)
                return &p;
        for (auto& p : getFactoryPresets())
            if (p.type == type)
                return &p;
        return nullptr;
    }

    const TrackPreset* namedOr (const juce::String& name, const juce::String& category, InstrumentType type)
    {
        if (auto* p = findFactoryPreset (name))
            return p;
        return firstPreset (category, type);
    }

    void add (Set& set, const TrackPreset* preset)
    {
        addTrack (set, preset != nullptr ? *preset : TrackPreset {});
    }

    Note n (double start, int pitch, double length = 0.25, float velocity = 0.8f)
    {
        Note note;
        note.start = start;
        note.pitch = pitch;
        note.length = length;
        note.velocity = velocity;
        return note;
    }

    // Drum pads use the standard factory layout: 0 kick, 1 snare, 2 clap, 4 closed hat, 5 open hat.
    int pad (int index) { return drumBaseNote + index; }

    ClipPtr clip (std::vector<Note> notes, int bars)
    {
        return makeClip (std::move (notes), bars * (double) beatsPerBar);
    }

    std::vector<Note> fourOnTheFloor (int bars, bool hats, bool claps)
    {
        std::vector<Note> notes;
        for (int b = 0; b < bars * 4; ++b)
        {
            notes.push_back (n (b, pad (0), 0.25, 0.95f));
            if (hats)
                notes.push_back (n (b + 0.5, pad (4), 0.125, b % 2 == 0 ? 0.7f : 0.55f));
            if (claps && b % 2 == 1)
                notes.push_back (n (b, pad (2), 0.25, 0.85f));
        }
        return notes;
    }

    std::vector<Note> chords (const std::vector<std::vector<int>>& progression, double chordLength, float velocity)
    {
        std::vector<Note> notes;
        double t = 0.0;
        for (auto& chord : progression)
        {
            for (auto p : chord)
                notes.push_back (n (t, p, chordLength * 0.95, velocity));
            t += chordLength;
        }
        return notes;
    }
}

std::vector<Set> makeDemoSets()
{
    std::vector<Set> demos;

    // Night Drive: 4/4 synth-pop in A minor.
    {
        Set set;
        set.name = "Demo - Night Drive";
        set.tempo = 104.0;
        set.inKey = true;
        set.root = 9;  // A
        set.scale = 1; // minor

        add (set, firstPreset ("Drums", InstrumentType::drumKit));
        add (set, firstPreset ("Bass", InstrumentType::analog));
        add (set, firstPreset ("Pad", InstrumentType::wave));
        add (set, firstPreset ("Lead", InstrumentType::analog));

        auto& drums = set.tracks[0];
        drums.slots[0] = clip (fourOnTheFloor (1, true, false), 1);
        drums.slots[1] = clip (fourOnTheFloor (1, true, true), 1);
        {
            auto notes = fourOnTheFloor (2, true, true);
            notes.push_back (n (7.5, pad (5), 0.25, 0.7f));
            notes.push_back (n (7.75, pad (1), 0.25, 0.6f));
            drums.slots[2] = clip (notes, 2);
        }

        // A1 eighths through Am - F - C - G.
        std::vector<Note> bass;
        const int roots[] = { 33, 29, 36, 31 };
        for (int bar = 0; bar < 4; ++bar)
            for (int e = 0; e < 8; ++e)
                bass.push_back (n (bar * 4 + e * 0.5, roots[bar] + (e == 7 ? 12 : 0), 0.4, e % 2 == 0 ? 0.9f : 0.7f));
        set.tracks[1].slots[1] = clip (bass, 4);
        set.tracks[1].slots[2] = clip (bass, 4);

        const auto progression = chords ({ { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } }, 4.0, 0.6f);
        {
            // The first pad clip opens its filter over four bars (clip automation, DEV-31).
            auto sweep = std::make_shared<Clip> (*clip (progression, 4));
            const int cutoff = getInstrumentSpec (set.tracks[2].instrumentType).indexOf ("cutoff");
            if (cutoff >= 0)
            {
                AutomationLane lane;
                lane.target = { ParamTarget::instrument, 0, cutoff };
                lane.points = { { 0.0, 600.0f }, { 12.0, 5000.0f }, { 16.0, 1200.0f } };
                sweep->automation.push_back (lane);
            }
            set.tracks[2].slots[0] = sweep;
        }
        set.tracks[2].slots[1] = clip (progression, 4);
        set.tracks[2].slots[2] = clip (progression, 4);

        std::vector<Note> lead;
        const int phrase[] = { 69, 72, 76, 74, 72, 69, 67, 69 };
        for (int i = 0; i < 8; ++i)
            lead.push_back (n (i * 0.5 + (i >= 4 ? 4.0 : 0.0), phrase[i], 0.45, 0.8f));
        lead.push_back (n (10.0, 76, 1.5, 0.85f));
        lead.push_back (n (12.0, 74, 0.5, 0.75f));
        lead.push_back (n (12.5, 72, 2.5, 0.75f));
        set.tracks[3].slots[2] = clip (lead, 4);

        demos.push_back (std::move (set));
    }

    // Sunday Bounce: laid-back beat with keys in C dorian.
    {
        Set set;
        set.name = "Demo - Sunday Bounce";
        set.tempo = 88.0;
        set.inKey = true;
        set.root = 0;
        set.scale = 2; // dorian

        add (set, namedOr ("Dusty Kit", "Drums", InstrumentType::drumKit));
        add (set, firstPreset ("Bass", InstrumentType::wave));
        add (set, firstPreset ("Keys", InstrumentType::sampler));

        std::vector<Note> beat;
        const double kicks[] = { 0.0, 0.75, 2.5, 3.25 };
        for (int bar = 0; bar < 2; ++bar)
        {
            for (auto k : kicks)
                beat.push_back (n (bar * 4 + k, pad (0), 0.25, 0.9f));
            beat.push_back (n (bar * 4 + 1.0, pad (1), 0.25, 0.9f));
            beat.push_back (n (bar * 4 + 3.0, pad (1), 0.25, 0.95f));
            for (int h = 0; h < 8; ++h)
                beat.push_back (n (bar * 4 + h * 0.5 + (h % 2 == 1 ? 0.04 : 0.0), pad (4), 0.125, h % 2 == 0 ? 0.65f : 0.45f));
        }
        set.tracks[0].slots[0] = clip (beat, 2);

        std::vector<Note> bass {
            n (0.0, 36, 0.7, 0.9f), n (0.75, 36, 0.25, 0.6f), n (2.5, 39, 0.5, 0.8f), n (3.25, 41, 0.5, 0.8f),
            n (4.0, 43, 0.7, 0.9f), n (4.75, 43, 0.25, 0.6f), n (6.5, 41, 0.5, 0.8f), n (7.25, 39, 0.5, 0.8f),
        };
        set.tracks[1].slots[0] = clip (bass, 2);

        set.tracks[2].slots[0] = clip (chords ({ { 60, 63, 67, 70 }, { 65, 69, 72, 75 } }, 4.0, 0.55f), 2);

        demos.push_back (std::move (set));
    }

    return demos;
}

} // namespace rb
