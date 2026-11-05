#pragma once

#include "../Sequencer/StepSequencer.h"
#include "../Mixer/Mixer.h"

#include <juce_data_structures/juce_data_structures.h>

// Saving and loading a whole set (§5.4.1, §3.21): clips with their launch settings, the
// arrangement, mixer, transport and global settings. The undo history is not part of a set
// (§5.4.2). Stored as a ValueTree, written to disk as XML.
namespace SetSerializer {

    // Settings owned outside the sequencer / mixer.
    struct Extras {
        float metronomeVolume = 0.5f;
        bool exclusiveSolo = true;
        bool exclusiveArm = true;
        double recordQuantization = 0.0;
    };

    static constexpr int FORMAT_VERSION = 1;

    inline juce::ValueTree clipToTree(const ClipData& clip) {
        juce::ValueTree tree("CLIP");
        tree.setProperty("name", clip.name, nullptr);
        tree.setProperty("colour", static_cast<juce::int64>(clip.colour), nullptr);
        tree.setProperty("looping", clip.looping, nullptr);
        tree.setProperty("start", clip.startMarker, nullptr);
        tree.setProperty("end", clip.endMarker, nullptr);
        tree.setProperty("loopStart", clip.loopStart, nullptr);
        tree.setProperty("loopEnd", clip.loopEnd, nullptr);
        // Notes as compact text: "start length pitch velocity muted;" per note.
        juce::String notes;
        for (const auto& note : clip.notes) {
            notes << juce::String(note.start, 6) << ' ' << juce::String(note.length, 6) << ' ' << note.pitch << ' '
                  << note.velocity << ' ' << (note.muted ? 1 : 0) << ';';
        }
        tree.setProperty("notes", notes, nullptr);
        return tree;
    }

    inline ClipData::Ptr clipFromTree(const juce::ValueTree& tree) {
        ClipData::Ptr clip = new ClipData();
        clip->name = tree.getProperty("name").toString();
        clip->colour = static_cast<juce::uint32>(static_cast<juce::int64>(tree.getProperty("colour", 0)));
        clip->looping = tree.getProperty("looping", true);
        clip->startMarker = tree.getProperty("start", 0.0);
        clip->endMarker = tree.getProperty("end", ClipData::DEFAULT_LENGTH);
        clip->loopStart = tree.getProperty("loopStart", 0.0);
        clip->loopEnd = tree.getProperty("loopEnd", ClipData::DEFAULT_LENGTH);
        for (const auto& entry : juce::StringArray::fromTokens(tree.getProperty("notes").toString(), ";", {})) {
            const auto fields = juce::StringArray::fromTokens(entry, " ", {});
            if (fields.size() >= 4) {
                clip->notes.push_back({ fields[0].getDoubleValue(), fields[1].getDoubleValue(), fields[2].getIntValue(),
                                        fields[3].getIntValue(), fields.size() > 4 && fields[4].getIntValue() != 0 });
            }
        }
        clip->sanitise();
        return clip;
    }

    inline void settingsToTree(const ClipSettings& settings, juce::ValueTree& tree) {
        tree.setProperty("launchMode", static_cast<int>(settings.launchMode.load()), nullptr);
        tree.setProperty("follow", settings.followEnabled.load(), nullptr);
        tree.setProperty("followA", static_cast<int>(settings.followActionA.load()), nullptr);
        tree.setProperty("followB", static_cast<int>(settings.followActionB.load()), nullptr);
        tree.setProperty("chanceA", settings.chanceA.load(), nullptr);
        tree.setProperty("chanceB", settings.chanceB.load(), nullptr);
        tree.setProperty("followLinked", settings.followLinked.load(), nullptr);
        tree.setProperty("followMultiplier", settings.followMultiplier.load(), nullptr);
        tree.setProperty("followSteps", settings.followTimeSteps.load(), nullptr);
    }

    inline void settingsFromTree(const juce::ValueTree& tree, ClipSettings& settings) {
        settings.launchMode.store(static_cast<LaunchMode>(juce::jlimit(0, 3, static_cast<int>(tree.getProperty("launchMode", 0)))));
        settings.followEnabled.store(tree.getProperty("follow", false));
        settings.followActionA.store(static_cast<FollowAction>(juce::jlimit(0, 8, static_cast<int>(tree.getProperty("followA", 4)))));
        settings.followActionB.store(static_cast<FollowAction>(juce::jlimit(0, 8, static_cast<int>(tree.getProperty("followB", 0)))));
        settings.chanceA.store(tree.getProperty("chanceA", 100));
        settings.chanceB.store(tree.getProperty("chanceB", 0));
        settings.followLinked.store(tree.getProperty("followLinked", true));
        settings.followMultiplier.store(tree.getProperty("followMultiplier", 1));
        settings.followTimeSteps.store(tree.getProperty("followSteps", 16));
    }

    inline juce::ValueTree save(StepSequencer& sequencer, const Mixer& mixer, const Extras& extras) {
        juce::ValueTree set("ANDROTONE_SET");
        set.setProperty("version", FORMAT_VERSION, nullptr);
        set.setProperty("tempo", sequencer.getTempo(), nullptr);
        set.setProperty("quantization", sequencer.getQuantizationIndex(), nullptr);
        set.setProperty("songMode", sequencer.isSongMode(), nullptr);
        set.setProperty("swingTiming", sequencer.getSwingTiming(), nullptr);
        set.setProperty("swingVelocity", sequencer.getSwingVelocity(), nullptr);
        set.setProperty("metronome", sequencer.isMetronomeOn(), nullptr);
        set.setProperty("metronomeVolume", extras.metronomeVolume, nullptr);
        set.setProperty("countIn", sequencer.getCountInBars(), nullptr);
        set.setProperty("recordQuantization", extras.recordQuantization, nullptr);
        set.setProperty("exclusiveSolo", extras.exclusiveSolo, nullptr);
        set.setProperty("exclusiveArm", extras.exclusiveArm, nullptr);
        set.setProperty("loop", sequencer.isLoopEnabled(), nullptr);
        set.setProperty("loopStart", sequencer.getLoopStart(), nullptr);
        set.setProperty("loopEnd", sequencer.getLoopEnd(), nullptr);
        set.setProperty("startPosition", sequencer.getStartPosition(), nullptr);
        set.setProperty("scenes", sequencer.getNumActiveClips(), nullptr);
        set.setProperty("masterVolume", mixer.getMasterVolume(), nullptr);

        for (int r = 0; r < Mixer::NUM_RETURNS; r++) {
            juce::ValueTree ret("RETURN");
            ret.setProperty("index", r, nullptr);
            ret.setProperty("level", mixer.getReturnLevel(r), nullptr);
            ret.setProperty("pre", mixer.isReturnPre(r), nullptr);
            set.appendChild(ret, nullptr);
        }

        const auto arrangement = sequencer.getArrangement();
        for (int t = 0; t < sequencer.getNumActiveTracks(); t++) {
            juce::ValueTree track("TRACK");
            track.setProperty("index", t, nullptr);
            track.setProperty("volume", mixer.getTrackVolume(t), nullptr);
            track.setProperty("pan", mixer.getTrackPan(t), nullptr);
            track.setProperty("active", mixer.isTrackActive(t), nullptr);
            track.setProperty("solo", mixer.isTrackSoloed(t), nullptr);
            track.setProperty("delaySend", mixer.getTrackDelaySend(t), nullptr);
            track.setProperty("reverbSend", mixer.getTrackReverbSend(t), nullptr);
            track.setProperty("armed", sequencer.isArmed(t), nullptr);

            for (int slot = 0; slot < MAX_CLIPS; slot++) {
                const auto data = sequencer.getClipData(t, slot);
                if (data == nullptr) {
                    continue;
                }
                auto clip = clipToTree(*data);
                clip.setProperty("slot", slot, nullptr);
                settingsToTree(sequencer.getClipSettings(t, slot), clip);
                track.appendChild(clip, nullptr);
            }

            if (arrangement != nullptr) {
                for (const auto& placed : arrangement->clips[static_cast<size_t>(t)]) {
                    if (placed.content == nullptr) {
                        continue;
                    }
                    juce::ValueTree entry("ARRANGEMENT_CLIP");
                    entry.setProperty("start", placed.start, nullptr);
                    entry.setProperty("length", placed.length, nullptr);
                    entry.setProperty("offset", placed.offset, nullptr);
                    entry.setProperty("sourceSlot", placed.sourceSlot, nullptr);
                    entry.appendChild(clipToTree(*placed.content), nullptr);
                    track.appendChild(entry, nullptr);
                }
            }
            set.appendChild(track, nullptr);
        }
        return set;
    }

    // Restores a set. Returns false (and changes nothing) when the tree isn't a set.
    inline bool load(const juce::ValueTree& set, StepSequencer& sequencer, Mixer& mixer, Extras& extras) {
        if (!set.hasType("ANDROTONE_SET")) {
            return false;
        }
        int numTracks = 0;
        for (const auto& child : set) {
            if (child.hasType("TRACK")) {
                numTracks = juce::jmax(numTracks, static_cast<int>(child.getProperty("index")) + 1);
            }
        }
        sequencer.resetForSetLoad(juce::jmax(numTracks, 1), set.getProperty("scenes", StepSequencer::MIN_SCENES));

        sequencer.setTempo(set.getProperty("tempo", 120.0));
        sequencer.setQuantizationIndex(set.getProperty("quantization", StepSequencer::DEFAULT_QUANTIZATION_INDEX));
        sequencer.setSongMode(set.getProperty("songMode", false));
        sequencer.setSwing(set.getProperty("swingTiming", 0.0), set.getProperty("swingVelocity", 0.0));
        sequencer.setMetronome(set.getProperty("metronome", false));
        sequencer.setCountInBars(set.getProperty("countIn", 0));
        sequencer.setLoop(set.getProperty("loop", false), set.getProperty("loopStart", 0.0), set.getProperty("loopEnd", 16.0));
        sequencer.setStartPosition(set.getProperty("startPosition", 0.0));
        extras.metronomeVolume = set.getProperty("metronomeVolume", 0.5f);
        extras.recordQuantization = set.getProperty("recordQuantization", 0.0);
        extras.exclusiveSolo = set.getProperty("exclusiveSolo", true);
        extras.exclusiveArm = set.getProperty("exclusiveArm", true);
        mixer.setMasterVolume(set.getProperty("masterVolume", 0.75f));

        ArrangementData::Ptr arrangement = new ArrangementData();
        for (const auto& child : set) {
            if (child.hasType("RETURN")) {
                const int r = child.getProperty("index", 0);
                if (r >= 0 && r < Mixer::NUM_RETURNS) {
                    mixer.setReturnLevel(r, child.getProperty("level", 1.0f));
                    mixer.setReturnPre(r, child.getProperty("pre", false));
                }
                continue;
            }
            if (!child.hasType("TRACK")) {
                continue;
            }
            const int t = child.getProperty("index", 0);
            if (t < 0 || t >= NUM_TRACKS) {
                continue;
            }
            mixer.setTrackVolume(t, child.getProperty("volume", 1.0f));
            mixer.setTrackPan(t, child.getProperty("pan", 0.0f));
            mixer.setTrackActive(t, child.getProperty("active", true));
            mixer.setTrackSolo(t, child.getProperty("solo", false));
            mixer.setTrackDelaySend(t, child.getProperty("delaySend", 0.0f));
            mixer.setTrackReverbSend(t, child.getProperty("reverbSend", 0.0f));
            sequencer.setArmed(t, child.getProperty("armed", false));

            for (const auto& entry : child) {
                if (entry.hasType("CLIP")) {
                    const int slot = entry.getProperty("slot", -1);
                    if (slot >= 0 && slot < MAX_CLIPS) {
                        sequencer.setClipData(t, slot, clipFromTree(entry));
                        settingsFromTree(entry, sequencer.getClipSettings(t, slot));
                    }
                } else if (entry.hasType("ARRANGEMENT_CLIP") && entry.getNumChildren() > 0) {
                    arrangement->place(t, { entry.getProperty("start", 0.0), entry.getProperty("length", 4.0),
                                            clipFromTree(entry.getChild(0)), entry.getProperty("offset", 0.0),
                                            entry.getProperty("sourceSlot", -1) });
                }
            }
        }
        arrangement->render();
        sequencer.setArrangement(arrangement);
        return true;
    }

    // Writes a clip as a standard MIDI file (type 1, one track, 960 ticks per beat) (§5.2).
    inline bool exportMidiClip(const ClipData& clip, double tempo, const juce::File& file) {
        constexpr int ticksPerBeat = 960;
        juce::MidiMessageSequence sequence;
        sequence.addEvent(juce::MidiMessage::tempoMetaEvent(static_cast<int>(60000000.0 / tempo)), 0.0);
        sequence.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0.0);
        const double from = clip.startMarker;
        const double to = clip.getPlayEnd();
        for (const auto& note : clip.notes) {
            if (note.muted || note.start < from || note.start >= to) {
                continue;
            }
            const double on = (note.start - from) * ticksPerBeat;
            const double off = (juce::jmin(note.end(), to) - from) * ticksPerBeat;
            sequence.addEvent(juce::MidiMessage::noteOn(1, note.pitch, static_cast<juce::uint8>(note.velocity)), on);
            sequence.addEvent(juce::MidiMessage::noteOff(1, note.pitch), off);
        }
        sequence.updateMatchedPairs();
        juce::MidiFile midiFile;
        midiFile.setTicksPerQuarterNote(ticksPerBeat);
        midiFile.addTrack(sequence);
        file.deleteFile();
        juce::FileOutputStream stream(file);
        return stream.openedOk() && midiFile.writeTo(stream, 1);
    }
}
