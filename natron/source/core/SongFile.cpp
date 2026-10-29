#include "SongFile.h"

namespace {
    namespace ids {
        const juce::Identifier song { "NatronSong" };
        const juce::Identifier machine { "Machine" };
        const juce::Identifier parameter { "Parameter" };
        const juce::Identifier mixer { "Mixer" };
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
    writeValues(root, ids::master, song.master);

    for (const auto& machine : song.machines) {
        juce::ValueTree machineTree(ids::machine);
        machineTree.setProperty(ids::type, machine.type, nullptr);
        machineTree.setProperty(ids::name, machine.name, nullptr);
        machineTree.setProperty(ids::selectedPattern, machine.selectedPattern, nullptr);
        writeValues(machineTree, ids::parameter, machine.parameters);
        writeValues(machineTree, ids::mixer, machine.mixer);

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
