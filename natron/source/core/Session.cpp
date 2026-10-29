#include "Session.h"

#include "MachineRegistry.h"
#include "SongFile.h"

Session::Session(Engine& engineToControl) :
    engine(engineToControl) {
    engine.setTempo(song.tempo);
    song.master = apply(engine.getMasterParameters(), {});
}

ParameterValues Session::apply(ParameterSet& parameters, const ParameterValues& values) {
    ParameterValues result;
    for (int i = 0; i < parameters.size(); i++) {
        const auto& id = parameters.getSpec(i).id;
        const auto it = values.find(id);
        if (it != values.end()) {
            parameters.set(i, it->second);
        }
        result[id] = parameters.get(i);
    }
    return result;
}

bool Session::setValue(ParameterSet& parameters, ParameterValues& stored, const juce::String& id, float value) {
    const int index = parameters.indexOf(id);
    if (index < 0) {
        return false;
    }
    parameters.set(index, value);
    stored[id] = parameters.get(index); // clamped
    return true;
}

int Session::addMachine(const juce::String& type, const juce::String& name) {
    if (getNumMachines() >= Engine::maxMachines) {
        return -1;
    }
    auto machine = MachineRegistry::create(type);
    if (machine == nullptr) {
        return -1;
    }

    auto channel = std::make_unique<ChannelStrip>();

    MachineState state;
    state.type = type;
    state.name = name.isNotEmpty() ? name : type + " " + juce::String(getNumMachines() + 1);
    state.parameters = apply(machine->getParameters(), {});
    state.mixer = apply(channel->getParameters(), {});

    const int index = getNumMachines();
    postMachine(std::move(machine), std::move(channel), state, index);
    song.machines.push_back(std::move(state));
    return index;
}

void Session::postMachine(std::unique_ptr<Machine> machine, std::unique_ptr<ChannelStrip> channel,
                          const MachineState& state, int index) {
    // Parameters were applied by the caller; prepare at the engine's current settings (the engine
    // re-prepares everything if the device changes later).
    machine->prepare(engine.getSampleRate(), engine.getMaxBlockSize());
    channel->prepare(engine.getSampleRate());

    machines.push_back(machine.get());
    channels.push_back(channel.get());
    EngineCommand add;
    add.type = EngineCommand::Type::addMachine;
    add.newMachine = machine.release();
    add.newChannel = channel.release();
    engine.post(add);

    for (int i = 0; i < PatternBank::numPatterns; i++) {
        const auto& pattern = state.patterns[static_cast<size_t>(i)];
        if (!pattern.isEmpty() || pattern.bars != 1) {
            engine.post({ EngineCommand::Type::setPattern, index, i, nullptr, new Pattern(pattern), nullptr });
        }
    }
    engine.post({ EngineCommand::Type::selectPattern, index, state.selectedPattern, nullptr, nullptr, nullptr });
}

bool Session::removeMachine(int index) {
    if (!isValidMachine(index)) {
        return false;
    }
    // Forget the machine before the engine trashes it, so nothing here can touch it afterwards.
    machines.erase(machines.begin() + index);
    channels.erase(channels.begin() + index);
    song.machines.erase(song.machines.begin() + index);
    return engine.post({ EngineCommand::Type::removeMachine, index, -1, nullptr, nullptr, nullptr });
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
    return engine.post({ EngineCommand::Type::setPattern, machine, patternIndex, nullptr, new Pattern(pattern), nullptr });
}

const Pattern& Session::getPattern(int machine, int patternIndex) const {
    return song.machines[static_cast<size_t>(machine)].patterns[static_cast<size_t>(patternIndex)];
}

bool Session::selectPattern(int machine, int patternIndex) {
    if (!isValidMachine(machine) || !isValidPatternIndex(patternIndex)) {
        return false;
    }
    song.machines[static_cast<size_t>(machine)].selectedPattern = patternIndex;
    return engine.post({ EngineCommand::Type::selectPattern, machine, patternIndex, nullptr, nullptr, nullptr });
}

bool Session::setParameter(int machine, const juce::String& parameterId, float value) {
    return isValidMachine(machine)
           && setValue(machines[static_cast<size_t>(machine)]->getParameters(),
                       song.machines[static_cast<size_t>(machine)].parameters, parameterId, value);
}

float Session::getParameter(int machine, const juce::String& parameterId) const {
    const auto& values = song.machines[static_cast<size_t>(machine)].parameters;
    const auto it = values.find(parameterId);
    return it != values.end() ? it->second : 0.0f;
}

const ParameterSet* Session::getParameters(int machine) const {
    return isValidMachine(machine) ? &machines[static_cast<size_t>(machine)]->getParameters() : nullptr;
}

bool Session::setMixerParameter(int machine, const juce::String& parameterId, float value) {
    return isValidMachine(machine)
           && setValue(channels[static_cast<size_t>(machine)]->getParameters(),
                       song.machines[static_cast<size_t>(machine)].mixer, parameterId, value);
}

float Session::getMixerParameter(int machine, const juce::String& parameterId) const {
    const auto& values = song.machines[static_cast<size_t>(machine)].mixer;
    const auto it = values.find(parameterId);
    return it != values.end() ? it->second : 0.0f;
}

const ParameterSet* Session::getMixerParameters(int machine) const {
    return isValidMachine(machine) ? &channels[static_cast<size_t>(machine)]->getParameters() : nullptr;
}

bool Session::setMasterParameter(const juce::String& parameterId, float value) {
    return setValue(engine.getMasterParameters(), song.master, parameterId, value);
}

float Session::getMasterParameter(const juce::String& parameterId) const {
    const auto it = song.master.find(parameterId);
    return it != song.master.end() ? it->second : 0.0f;
}

void Session::setTempo(double bpm) {
    engine.setTempo(bpm);
    song.tempo = engine.getTempo(); // clamped
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
    engine.post({ EngineCommand::Type::clearRack, -1, -1, nullptr, nullptr, nullptr });
    machines.clear();
    channels.clear();

    song = Song {};
    song.tempo = newSong.tempo;
    engine.setTempo(song.tempo);
    // Master values not in the file go back to their defaults.
    auto& masterParameters = engine.getMasterParameters();
    for (int i = 0; i < masterParameters.size(); i++) {
        masterParameters.set(i, masterParameters.getSpec(i).defaultValue);
    }
    song.master = apply(masterParameters, newSong.master);

    for (size_t i = 0; i < created.size(); i++) {
        auto state = newSong.machines[i];
        auto channel = std::make_unique<ChannelStrip>();
        // Keep only parameters the machine and channel know, at their clamped values.
        state.parameters = apply(created[i]->getParameters(), state.parameters);
        state.mixer = apply(channel->getParameters(), state.mixer);

        postMachine(std::move(created[i]), std::move(channel), state, static_cast<int>(i));
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
