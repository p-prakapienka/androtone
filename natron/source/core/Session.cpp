#include "Session.h"

#include "MachineRegistry.h"
#include "SongFile.h"

Session::Session(Engine& engineToControl) :
    engine(engineToControl) {
    engine.setTempo(song.tempo);
    engine.setMasterVolume(song.masterVolume);
}

int Session::addMachine(const juce::String& type, const juce::String& name) {
    if (getNumMachines() >= Engine::maxMachines) {
        return -1;
    }
    auto machine = MachineRegistry::create(type);
    if (machine == nullptr) {
        return -1;
    }

    MachineState state;
    state.type = type;
    state.name = name.isNotEmpty() ? name : type + " " + juce::String(getNumMachines() + 1);
    const auto& parameters = machine->getParameters();
    for (int i = 0; i < parameters.size(); i++) {
        state.parameters[parameters.getSpec(i).id] = parameters.get(i);
    }

    const int index = getNumMachines();
    postMachine(std::move(machine), state, index);
    song.machines.push_back(std::move(state));
    return index;
}

void Session::postMachine(std::unique_ptr<Machine> machine, const MachineState& state, int index) {
    auto& parameters = machine->getParameters();
    for (const auto& [id, value] : state.parameters) {
        const int parameterIndex = parameters.indexOf(id);
        if (parameterIndex >= 0) {
            parameters.set(parameterIndex, value);
        }
    }
    machine->prepare(engine.getSampleRate(), engine.getMaxBlockSize());

    machines.push_back(machine.get());
    EngineCommand add;
    add.type = EngineCommand::Type::addMachine;
    add.newMachine = machine.release();
    engine.post(add);
    engine.setMachineVolume(index, state.volume);

    for (int i = 0; i < PatternBank::numPatterns; i++) {
        const auto& pattern = state.patterns[static_cast<size_t>(i)];
        if (!pattern.isEmpty() || pattern.bars != 1) {
            engine.post({ EngineCommand::Type::setPattern, index, i, nullptr, new Pattern(pattern) });
        }
    }
    engine.post({ EngineCommand::Type::selectPattern, index, state.selectedPattern, nullptr, nullptr });
}

bool Session::removeMachine(int index) {
    if (!isValidMachine(index)) {
        return false;
    }
    // Forget the machine before the engine trashes it, so nothing here can touch it afterwards.
    machines.erase(machines.begin() + index);
    song.machines.erase(song.machines.begin() + index);
    return engine.post({ EngineCommand::Type::removeMachine, index, -1, nullptr, nullptr });
}

bool Session::renameMachine(int index, const juce::String& name) {
    if (!isValidMachine(index) || name.isEmpty()) {
        return false;
    }
    song.machines[static_cast<size_t>(index)].name = name;
    return true;
}

bool Session::setPattern(int machine, int patternIndex, const Pattern& pattern) {
    if (!isValidMachine(machine) || !isValidPatternIndex(patternIndex) || !Pattern::isValidLength(pattern.bars)) {
        return false;
    }
    song.machines[static_cast<size_t>(machine)].patterns[static_cast<size_t>(patternIndex)] = pattern;
    return engine.post({ EngineCommand::Type::setPattern, machine, patternIndex, nullptr, new Pattern(pattern) });
}

const Pattern& Session::getPattern(int machine, int patternIndex) const {
    return song.machines[static_cast<size_t>(machine)].patterns[static_cast<size_t>(patternIndex)];
}

bool Session::selectPattern(int machine, int patternIndex) {
    if (!isValidMachine(machine) || !isValidPatternIndex(patternIndex)) {
        return false;
    }
    song.machines[static_cast<size_t>(machine)].selectedPattern = patternIndex;
    return engine.post({ EngineCommand::Type::selectPattern, machine, patternIndex, nullptr, nullptr });
}

bool Session::setParameter(int machine, const juce::String& parameterId, float value) {
    if (!isValidMachine(machine)) {
        return false;
    }
    auto& parameters = machines[static_cast<size_t>(machine)]->getParameters();
    const int index = parameters.indexOf(parameterId);
    if (index < 0) {
        return false;
    }
    parameters.set(index, value);
    song.machines[static_cast<size_t>(machine)].parameters[parameterId] = parameters.get(index); // clamped
    return true;
}

float Session::getParameter(int machine, const juce::String& parameterId) const {
    const auto& values = song.machines[static_cast<size_t>(machine)].parameters;
    const auto it = values.find(parameterId);
    return it != values.end() ? it->second : 0.0f;
}

const ParameterSet* Session::getParameters(int machine) const {
    return isValidMachine(machine) ? &machines[static_cast<size_t>(machine)]->getParameters() : nullptr;
}

void Session::setMachineVolume(int machine, float volume) {
    if (isValidMachine(machine)) {
        song.machines[static_cast<size_t>(machine)].volume = volume;
        engine.setMachineVolume(machine, volume);
    }
}

void Session::setTempo(double bpm) {
    engine.setTempo(bpm);
    song.tempo = engine.getTempo(); // clamped
}

void Session::setMasterVolume(float volume) {
    song.masterVolume = volume;
    engine.setMasterVolume(volume);
}

juce::Result Session::loadSong(const Song& newSong) {
    if (static_cast<int>(newSong.machines.size()) > Engine::maxMachines) {
        return juce::Result::fail("Song has more than " + juce::String(Engine::maxMachines) + " machines");
    }

    // Create every machine first, so an unknown type fails before anything changes.
    std::vector<std::unique_ptr<Machine>> created;
    for (const auto& state : newSong.machines) {
        auto machine = MachineRegistry::create(state.type);
        if (machine == nullptr) {
            return juce::Result::fail("Unknown machine type: " + state.type);
        }
        created.push_back(std::move(machine));
    }

    engine.setPlaying(false);
    engine.post({ EngineCommand::Type::clearRack, -1, -1, nullptr, nullptr });
    machines.clear();

    song = Song {};
    song.tempo = newSong.tempo;
    song.masterVolume = newSong.masterVolume;
    engine.setTempo(song.tempo);
    engine.setMasterVolume(song.masterVolume);

    for (size_t i = 0; i < created.size(); i++) {
        auto state = newSong.machines[i];
        // Keep only parameters the machine knows, at their clamped values.
        auto& parameters = created[i]->getParameters();
        std::map<juce::String, float> values;
        for (int p = 0; p < parameters.size(); p++) {
            const auto& id = parameters.getSpec(p).id;
            const auto it = state.parameters.find(id);
            if (it != state.parameters.end()) {
                parameters.set(p, it->second);
            }
            values[id] = parameters.get(p);
        }
        state.parameters = values;

        postMachine(std::move(created[i]), state, static_cast<int>(i));
        song.machines.push_back(std::move(state));
    }
    return juce::Result::ok();
}

juce::Result Session::loadFromStream(juce::InputStream& stream) {
    Song loaded;
    const auto result = SongFile::load(stream, loaded);
    return result.wasOk() ? loadSong(loaded) : result;
}

juce::Result Session::saveToStream(juce::OutputStream& stream) const {
    return SongFile::save(song, stream);
}
