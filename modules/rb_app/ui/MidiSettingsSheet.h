#pragma once

namespace rb
{

// MIDI devices and mappings (MIDI-1, MIDI-3).
// STUB until MIDI input lands.
class MidiSettingsSheet : public Sheet
{
public:
    explicit MidiSettingsSheet (Studio&) : Sheet ("MIDI Input") {}
};

} // namespace rb
