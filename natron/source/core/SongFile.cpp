#include "SongFile.h"

namespace {
    namespace ids {
        const juce::Identifier song { "NatronSong" };
        const juce::Identifier machine { "Machine" };
        const juce::Identifier parameter { "Parameter" };
        const juce::Identifier mixer { "Mixer" };
        const juce::Identifier effect { "Effect" };
        const juce::Identifier block { "Block" };
        const juce::Identifier slot { "slot" };
        const juce::Identifier pattern_ { "pattern" };
        const juce::Identifier songMode { "songMode" };
        const juce::Identifier swing { "swing" };
        const juce::Identifier data { "Data" };
        const juce::Identifier loop { "loop" };
        const juce::Identifier loopStart { "loopStart" };
        const juce::Identifier loopEnd { "loopEnd" };
        const juce::Identifier master { "Master" };
        const juce::Identifier pattern { "Pattern" };
        const juce::Identifier note { "Note" };

        const juce::Identifier version { "version" };
        const juce::Identifier tempo { "tempo" };
        const juce::Identifier masterVolume { "masterVolume" };
        const juce::Identifier type { "type" };
        const juce::Identifier name { "name" };
        const juce::Identifier volume { "volume" };
        const juce::Identifier selectedPattern { "selectedPattern" };
        const juce::Identifier id { "id" };
        const juce::Identifier value { "value" };
        const juce::Identifier index { "index" };
        const juce::Identifier bars { "bars" };
        const juce::Identifier start { "start" };
        const juce::Identifier length { "length" };
        const juce::Identifier pitch { "pitch" };
        const juce::Identifier velocity { "velocity" };
    }

    bool isEmptyPattern(const Pattern& pattern) {
        return pattern.notes.empty() && pattern.bars == 1;
    }

    void writeValues(juce::ValueTree& parent, const juce::Identifier& type, const ParameterValues& values) {
        for (const auto& [parameterId, value] : values) {
            juce::ValueTree child(type);
            child.setProperty(ids::id, parameterId, nullptr);
            child.setProperty(ids::value, value, nullptr);
            parent.appendChild(child, nullptr);
        }
    }
}

juce::ValueTree SongFile::toValueTree(const Song& song) {
    juce::ValueTree root(ids::song);
    root.setProperty(ids::version, currentVersion, nullptr);
    root.setProperty(ids::tempo, song.tempo, nullptr);
    root.setProperty(ids::swing, song.swing, nullptr);
    root.setProperty(ids::songMode, song.songMode, nullptr);
    root.setProperty(ids::loop, song.loopEnabled, nullptr);
    root.setProperty(ids::loopStart, song.loopStartBar, nullptr);
    root.setProperty(ids::loopEnd, song.loopEndBar, nullptr);
    writeValues(root, ids::master, song.master);

    for (const auto& machine : song.machines) {
        juce::ValueTree machineTree(ids::machine);
        machineTree.setProperty(ids::type, machine.type, nullptr);
        machineTree.setProperty(ids::name, machine.name, nullptr);
        machineTree.setProperty(ids::selectedPattern, machine.selectedPattern, nullptr);
        writeValues(machineTree, ids::parameter, machine.parameters);
        writeValues(machineTree, ids::mixer, machine.mixer);

        for (size_t e = 0; e < machine.effects.size(); e++) {
            const auto& effectState = machine.effects[e];
            if (effectState.type.isEmpty()) {
                continue;
            }
            juce::ValueTree effectTree(ids::effect);
            effectTree.setProperty(ids::slot, static_cast<int>(e), nullptr);
            effectTree.setProperty(ids::type, effectState.type, nullptr);
            writeValues(effectTree, ids::parameter, effectState.parameters);
            machineTree.appendChild(effectTree, nullptr);
        }

        for (int i = 0; i < PatternBank::numPatterns; i++) {
            const auto& pattern = machine.patterns[static_cast<size_t>(i)];
            if (isEmptyPattern(pattern)) {
                continue;
            }
            juce::ValueTree patternTree(ids::pattern);
            patternTree.setProperty(ids::index, i, nullptr);
            patternTree.setProperty(ids::bars, pattern.bars, nullptr);
            for (const auto& note : pattern.notes) {
                juce::ValueTree noteTree(ids::note);
                noteTree.setProperty(ids::start, note.start, nullptr);
                noteTree.setProperty(ids::length, note.length, nullptr);
                noteTree.setProperty(ids::pitch, note.pitch, nullptr);
                noteTree.setProperty(ids::velocity, note.velocity, nullptr);
                patternTree.appendChild(noteTree, nullptr);
            }
            machineTree.appendChild(patternTree, nullptr);
        }

        if (machine.data.isValid()) {
            juce::ValueTree dataTree(ids::data);
            dataTree.appendChild(machine.data.createCopy(), nullptr);
            machineTree.appendChild(dataTree, nullptr);
        }

        for (const auto& songBlock : machine.blocks) {
            juce::ValueTree blockTree(ids::block);
            blockTree.setProperty(ids::start, songBlock.startBar, nullptr);
            blockTree.setProperty(ids::length, songBlock.lengthBars, nullptr);
            blockTree.setProperty(ids::pattern_, songBlock.pattern, nullptr);
            machineTree.appendChild(blockTree, nullptr);
        }

        root.appendChild(machineTree, nullptr);
    }
    return root;
}

juce::Result SongFile::fromValueTree(const juce::ValueTree& tree, Song& song) {
    if (!tree.hasType(ids::song)) {
        return juce::Result::fail("Not a Natron song");
    }
    const int version = tree.getProperty(ids::version, 0);
    if (version < 1) {
        return juce::Result::fail("Song file has no valid version");
    }
    if (version > currentVersion) {
        return juce::Result::fail("Song was saved by a newer version of Natron (format " + juce::String(version) + ")");
    }

    Song loaded;
    loaded.tempo = tree.getProperty(ids::tempo, loaded.tempo);
    loaded.songMode = tree.getProperty(ids::songMode, loaded.songMode);
    loaded.swing = juce::jlimit(Song::minSwing, Song::maxSwing, static_cast<float>(tree.getProperty(ids::swing, loaded.swing)));
    loaded.loopEnabled = tree.getProperty(ids::loop, loaded.loopEnabled);
    loaded.loopStartBar = tree.getProperty(ids::loopStart, loaded.loopStartBar);
    loaded.loopEndBar = tree.getProperty(ids::loopEnd, loaded.loopEndBar);
    if (loaded.loopStartBar < 0 || loaded.loopEndBar <= loaded.loopStartBar) {
        return juce::Result::fail("Invalid loop region");
    }
    if (version < 2 && tree.hasProperty(ids::masterVolume)) {
        loaded.master["volume"] = tree.getProperty(ids::masterVolume);
    }

    for (const auto& machineTree : tree) {
        if (machineTree.hasType(ids::master)) {
            loaded.master[machineTree.getProperty(ids::id).toString()] = machineTree.getProperty(ids::value);
        }
        if (!machineTree.hasType(ids::machine)) {
            continue;
        }
        MachineState machine;
        machine.type = machineTree.getProperty(ids::type).toString();
        machine.name = machineTree.getProperty(ids::name).toString();
        if (version < 2 && machineTree.hasProperty(ids::volume)) {
            machine.mixer["volume"] = machineTree.getProperty(ids::volume);
        }
        machine.selectedPattern = juce::jlimit(0, PatternBank::numPatterns - 1,
                                               static_cast<int>(machineTree.getProperty(ids::selectedPattern, 0)));

        for (const auto& child : machineTree) {
            if (child.hasType(ids::parameter)) {
                machine.parameters[child.getProperty(ids::id).toString()] = child.getProperty(ids::value);
            } else if (child.hasType(ids::mixer)) {
                machine.mixer[child.getProperty(ids::id).toString()] = child.getProperty(ids::value);
            } else if (child.hasType(ids::effect)) {
                const int effectSlot = child.getProperty(ids::slot, -1);
                if (effectSlot < 0 || effectSlot >= MachineState::numEffects) {
                    return juce::Result::fail("Effect slot out of range: " + juce::String(effectSlot));
                }
                auto& effectState = machine.effects[static_cast<size_t>(effectSlot)];
                effectState.type = child.getProperty(ids::type).toString();
                for (const auto& parameterTree : child) {
                    if (parameterTree.hasType(ids::parameter)) {
                        effectState.parameters[parameterTree.getProperty(ids::id).toString()] = parameterTree.getProperty(ids::value);
                    }
                }
            } else if (child.hasType(ids::data)) {
                if (child.getNumChildren() > 0) {
                    machine.data = child.getChild(0).createCopy();
                }
            } else if (child.hasType(ids::block)) {
                const SongBlock songBlock { child.getProperty(ids::start, -1), child.getProperty(ids::length, 0),
                                            child.getProperty(ids::pattern_, -1) };
                if (Arrangement::place(machine.blocks, songBlock) < 0) {
                    return juce::Result::fail("Invalid song block at bar " + juce::String(songBlock.startBar));
                }
            } else if (child.hasType(ids::pattern)) {
                const int index = child.getProperty(ids::index, -1);
                const int bars = child.getProperty(ids::bars, 1);
                if (index < 0 || index >= PatternBank::numPatterns) {
                    return juce::Result::fail("Pattern index out of range: " + juce::String(index));
                }
                if (!Pattern::isValidLength(bars)) {
                    return juce::Result::fail("Invalid pattern length: " + juce::String(bars) + " bars");
                }
                auto& pattern = machine.patterns[static_cast<size_t>(index)];
                pattern.bars = bars;
                for (const auto& noteTree : child) {
                    if (noteTree.hasType(ids::note)) {
                        pattern.notes.push_back({ noteTree.getProperty(ids::start, 0.0),
                                                  noteTree.getProperty(ids::length, Pattern::stepLength),
                                                  juce::jlimit(0, 127, static_cast<int>(noteTree.getProperty(ids::pitch, 60))),
                                                  juce::jlimit(0.0f, 1.0f, static_cast<float>(noteTree.getProperty(ids::velocity, 0.8f))) });
                    }
                }
            }
        }
        loaded.machines.push_back(std::move(machine));
    }

    song = std::move(loaded);
    return juce::Result::ok();
}

juce::Result SongFile::save(const Song& song, juce::OutputStream& stream) {
    const auto xml = toValueTree(song).createXml();
    if (xml == nullptr) {
        return juce::Result::fail("Could not serialise song");
    }
    xml->writeTo(stream);
    stream.flush();
    return juce::Result::ok();
}

juce::Result SongFile::load(juce::InputStream& stream, Song& song) {
    const auto xml = juce::parseXML(stream.readEntireStreamAsString());
    if (xml == nullptr) {
        return juce::Result::fail("Song file is not valid XML");
    }
    return fromValueTree(juce::ValueTree::fromXml(*xml), song);
}
