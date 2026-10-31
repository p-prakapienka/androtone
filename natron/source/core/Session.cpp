#include "Session.h"

#include "MachineRegistry.h"
#include "effects/Effect.h"
#include "SongFile.h"

#include <algorithm>

namespace {
    bool succeeded(bool result) { return result; }
    bool succeeded(int machineIndex) { return machineIndex >= 0; }
    bool succeeded(const juce::Result& result) { return result.wasOk(); }

    bool differs(const ParameterValues& values, const juce::String& id, float value) {
        const auto it = values.find(id);
        return it == values.end() || !juce::exactlyEqual(it->second, value);
    }
}

template <typename Change>
auto Session::edit(const juce::String& key, Change&& change) {
    if (restoring) {
        return change();
    }
    const double now = clock();
    const bool merge = gestureDepth > 0 ? gestureRecorded
                                        : key.isNotEmpty() && key == lastEditKey && now - lastEditTime < coalesceSeconds;
    if (merge) {
        auto result = change();
        if (succeeded(result)) {
            lastEditTime = now;
        }
        return result;
    }
    Song before = song;
    auto result = change();
    if (succeeded(result)) {
        pushUndo(std::move(before));
        lastEditKey = gestureDepth > 0 ? juce::String() : key;
        lastEditTime = now;
        gestureRecorded = gestureDepth > 0;
    }
    return result;
}

void Session::pushUndo(Song before) {
    undoStack.push_back(std::move(before));
    while (static_cast<int>(undoStack.size()) > maxUndoSteps) {
        undoStack.pop_front();
    }
    redoStack.clear();
}

void Session::clearHistory() {
    undoStack.clear();
    redoStack.clear();
    lastEditKey = {};
    gestureRecorded = false;
}

void Session::beginGesture() {
    if (gestureDepth++ == 0) {
        gestureRecorded = false;
    }
}

void Session::endGesture() {
    if (gestureDepth > 0 && --gestureDepth == 0) {
        gestureRecorded = false;
        lastEditKey = {};
    }
}

bool Session::undo() {
    if (undoStack.empty()) {
        return false;
    }
    redoStack.push_back(song);
    const Song target = std::move(undoStack.back());
    undoStack.pop_back();
    restore(target);
    lastEditKey = {};
    gestureRecorded = false;
    return true;
}

bool Session::redo() {
    if (redoStack.empty()) {
        return false;
    }
    undoStack.push_back(song);
    const Song target = std::move(redoStack.back());
    redoStack.pop_back();
    restore(target);
    lastEditKey = {};
    gestureRecorded = false;
    return true;
}

void Session::restore(const Song& target) {
    const juce::ScopedValueSetter<bool> applying(restoring, true);

    // Machines are only ever added at the end of the rack, so when one rack is the other plus
    // machines at the end, adding or removing those is enough. Anything else (a machine removed
    // from the middle) reloads the whole song.
    const size_t common = std::min(song.machines.size(), target.machines.size());
    bool samePrefix = true;
    for (size_t m = 0; samePrefix && m < common; m++) {
        samePrefix = song.machines[m].type == target.machines[m].type;
    }
    if (samePrefix) {
        while (getNumMachines() > static_cast<int>(target.machines.size())) {
            removeMachine(getNumMachines() - 1);
        }
        while (getNumMachines() < static_cast<int>(target.machines.size())) {
            const auto& added = target.machines[static_cast<size_t>(getNumMachines())];
            if (addMachine(added.type, added.name) < 0) {
                samePrefix = false;
                break;
            }
            selectPattern(getNumMachines() - 1, added.selectedPattern);
        }
    }
    if (!samePrefix) {
        const bool wasPlaying = isPlaying();
        auto reloaded = target;
        reloaded.songMode = song.songMode; // neither the mode nor the selection is undone
        for (size_t m = 0; m < reloaded.machines.size() && m < song.machines.size(); m++) {
            reloaded.machines[m].selectedPattern = song.machines[m].selectedPattern;
        }
        const auto result = replaceSong(reloaded);
        jassert(result.wasOk()); // it was a valid song before
        juce::ignoreUnused(result);
        setPlaying(wasPlaying);
        return;
    }

    setTempo(target.tempo);
    setSwing(target.swing);
    setLoop(target.loopEnabled, target.loopStartBar, target.loopEndBar);
    for (const auto& [id, value] : target.master) {
        if (differs(song.master, id, value)) {
            setMasterParameter(id, value);
        }
    }
    for (size_t m = 0; m < target.machines.size(); m++) {
        const auto& from = target.machines[m];
        const int index = static_cast<int>(m);
        auto& current = song.machines[m];
        if (current.name != from.name) {
            renameMachine(index, from.name);
        }
        if (!current.data.isEquivalentTo(from.data) && from.data.isValid()) {
            setMachineData(index, from.data);
        }
        for (const auto& [id, value] : from.parameters) {
            if (differs(current.parameters, id, value)) {
                setParameter(index, id, value);
            }
        }
        for (const auto& [id, value] : from.mixer) {
            if (differs(current.mixer, id, value)) {
                setMixerParameter(index, id, value);
            }
        }
        for (int slot = 0; slot < Engine::effectsPerMachine; slot++) {
            const auto& effect = from.effects[static_cast<size_t>(slot)];
            if (current.effects[static_cast<size_t>(slot)].type != effect.type) {
                setEffect(index, slot, effect.type);
            }
            for (const auto& [id, value] : effect.parameters) {
                if (differs(current.effects[static_cast<size_t>(slot)].parameters, id, value)) {
                    setEffectParameter(index, slot, id, value);
                }
            }
        }
        if (!(current.blocks == from.blocks)) {
            setBlocks(index, from.blocks);
        }
        for (int p = 0; p < PatternBank::numPatterns; p++) {
            if (!(current.patterns[static_cast<size_t>(p)] == from.patterns[static_cast<size_t>(p)])) {
                setPattern(index, p, from.patterns[static_cast<size_t>(p)]);
            }
        }
    }
    if (!(song.automation == target.automation)) {
        setSongAutomation(target.automation);
    }
}

Session::Session(Engine& engineToControl) :
    engine(engineToControl) {
    engine.setTempo(song.tempo);
    syncTransportSettings();
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
    return edit({}, [&] {
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
        state.data = machine->getData().createCopy();

        const int index = getNumMachines();
        postMachine(std::move(machine), std::move(channel), {}, state, index);
        song.machines.push_back(std::move(state));
        return index;
    });
}

void Session::postMachine(std::unique_ptr<Machine> machine, std::unique_ptr<ChannelStrip> channel,
                          EffectSlots machineEffects, const MachineState& state, int index) {
    machine->parameterChanged(-1);
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
            EngineCommand command { EngineCommand::Type::setPattern, index, i };
            command.newPattern = new Pattern(pattern);
            engine.post(command);
        }
    }
    post({ EngineCommand::Type::selectPattern, index, state.selectedPattern });

    effects.push_back({});
    for (int slot = 0; slot < Engine::effectsPerMachine; slot++) {
        if (machineEffects[static_cast<size_t>(slot)] != nullptr) {
            postEffect(index, slot, std::move(machineEffects[static_cast<size_t>(slot)]));
        }
    }
    if (!state.blocks.empty()) {
        EngineCommand blocks { EngineCommand::Type::setBlocks, index };
        blocks.newBlocks = new BlockList(state.blocks);
        engine.post(blocks);
    }
}

void Session::postEffect(int machine, int slot, std::unique_ptr<Effect> effect) {
    if (effect != nullptr) {
        effect->prepare(engine.getSampleRate(), engine.getMaxBlockSize());
    }
    effects[static_cast<size_t>(machine)][static_cast<size_t>(slot)] = effect.get();
    EngineCommand command { EngineCommand::Type::setEffect, machine, slot };
    command.newEffect = effect.release();
    engine.post(command);
}

bool Session::removeMachine(int index) {
    return edit({}, [&] {
        if (!isValidMachine(index)) {
            return false;
        }
        // Forget the machine before the engine trashes it, so nothing here can touch it afterwards.
        machines.erase(machines.begin() + index);
        channels.erase(channels.begin() + index);
        effects.erase(effects.begin() + index);
        song.machines.erase(song.machines.begin() + index);
        const bool removed = post({ EngineCommand::Type::removeMachine, index });

        // Song automation follows the machines: lanes for this one go, later ones move up.
        AutomationLanes lanes;
        for (auto lane : song.automation) {
            if (lane.target.scope != AutomationTarget::Scope::master && lane.target.machine == index) {
                continue;
            }
            if (lane.target.scope != AutomationTarget::Scope::master && lane.target.machine > index) {
                lane.target.machine--;
            }
            lanes.push_back(std::move(lane));
        }
        if (lanes.size() != song.automation.size() || !(lanes == song.automation)) {
            song.automation = std::move(lanes);
            postSongAutomation();
        }
        return removed;
    });
}

bool Session::renameMachine(int index, const juce::String& name) {
    return edit("name/" + juce::String(index), [&] {
        if (!isValidMachine(index) || name.isEmpty()) {
            return false;
        }
        song.machines[static_cast<size_t>(index)].name = name;
        return true;
    });
}

bool Session::setPattern(int machine, int patternIndex, const Pattern& pattern) {
    return edit({}, [&] {
        if (!isValidMachine(machine) || !isValidPatternIndex(patternIndex) || !Pattern::isValidLength(pattern.bars)) {
            return false;
        }
        for (const auto& lane : pattern.automation) {
            if (!isValidLane(lane, false)) {
                return false;
            }
        }
        song.machines[static_cast<size_t>(machine)].patterns[static_cast<size_t>(patternIndex)] = pattern;
        EngineCommand command { EngineCommand::Type::setPattern, machine, patternIndex };
        command.newPattern = new Pattern(pattern);
        return engine.post(command);
    });
}

const Pattern& Session::getPattern(int machine, int patternIndex) const {
    return song.machines[static_cast<size_t>(machine)].patterns[static_cast<size_t>(patternIndex)];
}

bool Session::selectPattern(int machine, int patternIndex) {
    if (!isValidMachine(machine) || !isValidPatternIndex(patternIndex)) {
        return false;
    }
    song.machines[static_cast<size_t>(machine)].selectedPattern = patternIndex;
    return post({ EngineCommand::Type::selectPattern, machine, patternIndex });
}

bool Session::setParameter(int machine, const juce::String& parameterId, float value) {
    return edit("parameter/" + juce::String(machine) + "/" + parameterId, [&] {
        if (!isValidMachine(machine)) {
            return false;
        }
        auto* live = machines[static_cast<size_t>(machine)];
        if (!setValue(live->getParameters(), song.machines[static_cast<size_t>(machine)].parameters, parameterId, value)) {
            return false;
        }
        live->parameterChanged(live->getParameters().indexOf(parameterId));
        return true;
    });
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
    return edit("mixer/" + juce::String(machine) + "/" + parameterId, [&] {
        return isValidMachine(machine)
               && setValue(channels[static_cast<size_t>(machine)]->getParameters(),
                           song.machines[static_cast<size_t>(machine)].mixer, parameterId, value);
    });
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
    return edit("master/" + parameterId, [&] {
        return setValue(engine.getMasterParameters(), song.master, parameterId, value);
    });
}

float Session::getMasterParameter(const juce::String& parameterId) const {
    const auto it = song.master.find(parameterId);
    return it != song.master.end() ? it->second : 0.0f;
}

void Session::setTempo(double bpm) {
    edit("tempo", [&] {
        engine.setTempo(bpm);
        song.tempo = engine.getTempo(); // clamped
        return true;
    });
}

juce::Result Session::loadSong(const Song& newSong) {
    const auto result = replaceSong(newSong);
    if (result.wasOk()) {
        clearHistory();
    }
    return result;
}

juce::Result Session::replaceSong(const Song& newSong) {
    if (static_cast<int>(newSong.machines.size()) > Engine::maxMachines) {
        return juce::Result::fail("Song has more than " + juce::String(Engine::maxMachines) + " machines");
    }

    // Create every machine and effect first, so an unknown type fails before anything changes.
    std::vector<std::unique_ptr<Machine>> created;
    std::vector<EffectSlots> createdEffects;
    for (const auto& state : newSong.machines) {
        auto machine = MachineRegistry::create(state.type);
        if (machine == nullptr) {
            return juce::Result::fail("Unknown machine type: " + state.type);
        }
        if (state.data.isValid()) {
            if (const auto loaded = machine->setData(state.data); loaded.failed()) {
                return juce::Result::fail(state.name + ": " + loaded.getErrorMessage());
            }
        }
        created.push_back(std::move(machine));

        EffectSlots slots;
        for (size_t e = 0; e < slots.size(); e++) {
            const auto& type = state.effects[e].type;
            if (type.isNotEmpty() && (slots[e] = EffectRegistry::create(type)) == nullptr) {
                return juce::Result::fail("Unknown effect type: " + type);
            }
        }
        createdEffects.push_back(std::move(slots));
        if (!Arrangement::isValid(state.blocks)) {
            return juce::Result::fail("Invalid song timeline for " + state.name);
        }
    }

    engine.setPlaying(false);
    post({ EngineCommand::Type::clearRack });
    machines.clear();
    channels.clear();
    effects.clear();

    song = Song {};
    song.tempo = newSong.tempo;
    song.songMode = newSong.songMode;
    song.swing = juce::jlimit(Song::minSwing, Song::maxSwing, newSong.swing);
    song.automation = {};
    song.loopEnabled = newSong.loopEnabled;
    song.loopStartBar = newSong.loopStartBar;
    song.loopEndBar = newSong.loopEndBar;
    engine.setTempo(song.tempo);
    syncTransportSettings();
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
        state.data = created[i]->getData().createCopy();
        for (size_t e = 0; e < state.effects.size(); e++) {
            auto& effect = createdEffects[i][e];
            state.effects[e].parameters = effect != nullptr ? apply(effect->getParameters(), state.effects[e].parameters)
                                                            : ParameterValues {};
        }

        postMachine(std::move(created[i]), std::move(channel), std::move(createdEffects[i]), state, static_cast<int>(i));
        song.machines.push_back(std::move(state));
    }
    // Song automation last: its lanes are checked against the machines just loaded.
    AutomationLanes lanes;
    for (const auto& lane : newSong.automation) {
        if (isValidLane(lane, true)) {
            lanes.push_back(lane);
        }
    }
    song.automation = std::move(lanes);
    postSongAutomation();
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

bool Session::post(const EngineCommand& command) {
    return engine.post(command);
}

bool Session::setEffect(int machine, int slot, const juce::String& type) {
    return edit({}, [&] {
        if (!isValidMachine(machine) || !isValidEffectSlot(slot)) {
            return false;
        }
        std::unique_ptr<Effect> effect;
        if (type.isNotEmpty() && (effect = EffectRegistry::create(type)) == nullptr) {
            return false;
        }
        auto& state = song.machines[static_cast<size_t>(machine)].effects[static_cast<size_t>(slot)];
        state.type = type;
        state.parameters = effect != nullptr ? apply(effect->getParameters(), {}) : ParameterValues {};
        postEffect(machine, slot, std::move(effect));
        return true;
    });
}

bool Session::setEffectParameter(int machine, int slot, const juce::String& parameterId, float value) {
    return edit("effect/" + juce::String(machine) + "/" + juce::String(slot) + "/" + parameterId, [&] {
        if (!isValidMachine(machine) || !isValidEffectSlot(slot)) {
            return false;
        }
        auto* effect = effects[static_cast<size_t>(machine)][static_cast<size_t>(slot)];
        return effect != nullptr
               && setValue(effect->getParameters(),
                           song.machines[static_cast<size_t>(machine)].effects[static_cast<size_t>(slot)].parameters,
                           parameterId, value);
    });
}

const ParameterSet* Session::getEffectParameters(int machine, int slot) const {
    if (!isValidMachine(machine) || !isValidEffectSlot(slot)) {
        return nullptr;
    }
    const auto* effect = effects[static_cast<size_t>(machine)][static_cast<size_t>(slot)];
    return effect != nullptr ? &effect->getParameters() : nullptr;
}

bool Session::setBlocks(int machine, const BlockList& blocks) {
    return edit({}, [&] {
        if (!isValidMachine(machine) || !Arrangement::isValid(blocks)) {
            return false;
        }
        song.machines[static_cast<size_t>(machine)].blocks = blocks;
        EngineCommand command { EngineCommand::Type::setBlocks, machine };
        command.newBlocks = new BlockList(blocks);
        return engine.post(command);
    });
}

void Session::setSongMode(bool enabled) {
    song.songMode = enabled;
    syncTransportSettings();
}

bool Session::setLoop(bool enabled, int startBar, int endBar) {
    return edit("loop", [&] {
        if (startBar < 0 || endBar <= startBar || endBar > Arrangement::maxBars) {
            return false;
        }
        song.loopEnabled = enabled;
        song.loopStartBar = startBar;
        song.loopEndBar = endBar;
        syncTransportSettings();
        return true;
    });
}

void Session::collectGarbage() {
    engine.collectGarbage();
    for (auto* machine : machines) {
        machine->collectGarbage();
    }
}

juce::Result Session::setMachineData(int machine, const juce::ValueTree& data) {
    return edit({}, [&] {
        if (!isValidMachine(machine)) {
            return juce::Result::fail("No such machine");
        }
        auto* live = machines[static_cast<size_t>(machine)];
        const auto result = live->setData(data);
        if (result.wasOk()) {
            song.machines[static_cast<size_t>(machine)].data = live->getData().createCopy();
        }
        return result;
    });
}

juce::ValueTree Session::getMachineData(int machine) const {
    return isValidMachine(machine) ? song.machines[static_cast<size_t>(machine)].data : juce::ValueTree();
}

void Session::setSwing(float amount) {
    edit("swing", [&] {
        song.swing = juce::jlimit(Song::minSwing, Song::maxSwing, amount);
        syncTransportSettings();
        return true;
    });
}

void Session::syncTransportSettings() {
    engine.setSwing(song.swing);
    engine.setSongMode(song.songMode);
    engine.setLoop(song.loopEnabled, song.loopStartBar, song.loopEndBar);
}

bool Session::isValidLane(const AutomationLane& lane, bool songLane) const {
    if (lane.target.parameterId.isEmpty() || !lane.isSorted()) {
        return false;
    }
    if (lane.target.scope == AutomationTarget::Scope::master) {
        return songLane; // pattern automation stays within its machine
    }
    if (lane.target.scope == AutomationTarget::Scope::effect && !isValidEffectSlot(lane.target.effectSlot)) {
        return false;
    }
    return !songLane || isValidMachine(lane.target.machine);
}

bool Session::setSongAutomation(const AutomationLanes& lanes) {
    return edit({}, [&] {
        for (const auto& lane : lanes) {
            if (!isValidLane(lane, true)) {
                return false;
            }
        }
        song.automation = lanes;
        postSongAutomation();
        return true;
    });
}

void Session::postSongAutomation() {
    EngineCommand command { EngineCommand::Type::setSongAutomation };
    command.newAutomation = song.automation.empty() ? nullptr : new AutomationLanes(song.automation);
    engine.post(command);
}
