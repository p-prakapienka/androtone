#pragma once

namespace rb
{

class Studio;

// External MIDI (MIDI-1..MIDI-3): USB and Bluetooth inputs the user enabled, notes played on
// the active track, and learnable mappings for macros and commands.
class MidiInputs : private juce::MidiInputCallback,
                   private juce::AsyncUpdater
{
public:
    explicit MidiInputs (Studio&);
    ~MidiInputs() override;

    struct Device
    {
        juce::MidiDeviceInfo info;
        bool enabled = false;
        bool open = false;
    };

    std::vector<Device> getDevices() const;
    void setEnabled (const juce::String& identifier, bool enabled);
    void refresh(); // reopen enabled devices after a list change

    // Mapping targets (MIDI-3).
    static const juce::StringArray& getTargets(); // Macro 1..8, Capture, Clear Capture, ...
    void startLearning (const juce::String& target);
    void cancelLearning();
    juce::String getLearningTarget() const;
    void clearMapping (const juce::String& target);

    std::function<void()> onChange; // device list, learning or mapping changed

    // Feeds a message as if it came from a device (tests, virtual input).
    void handleMessage (const juce::MidiMessage&);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void handleAsyncUpdate() override;
    void runMappedAction (const juce::String& target, int value);
    static juce::String sourceKey (const juce::MidiMessage&);

    Studio& studio;
    std::map<juce::String, std::unique_ptr<juce::MidiInput>> openInputs;
    juce::MidiDeviceListConnection listConnection;

    std::mutex learnMutex;
    juce::String learningTarget;

    // Control messages are handled on the message thread.
    juce::CriticalSection pendingLock;
    std::vector<juce::MidiMessage> pending;
};

} // namespace rb
