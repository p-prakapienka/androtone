#pragma once

namespace rb
{

// Connects the SessionProcessor to the audio device and owns the long-lived engine objects.
class AudioEngine : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // Opens the default device. withInput opens the microphone too (asks for permission on
    // Android the first time).
    void start (bool withInput);
    void enableInput (std::function<void (bool granted)> onDone = nullptr);
    bool hasInput() const { return inputEnabled; }

    SessionProcessor& getSession() { return session; }
    const SessionProcessor& getSession() const { return session; }
    SampleLibrary& getSamples() { return samples; }
    Document& getDocument() { return document; }
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

    // For the diagnostics line in App Settings.
    struct Status
    {
        juce::String deviceType, deviceName;
        double sampleRate = 0.0;
        int bufferSize = 0;
        double cpuLoad = 0.0; // 0..1 of the time available per callback
        int dropouts = -1;    // xruns reported by the device, -1 if unknown
    };
    Status getStatus();

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs,
                                           int numOutputs, int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager deviceManager;
    SessionProcessor session;
    SampleLibrary samples;
    Document document { session, samples };

    juce::AudioBuffer<float> stereo;
    juce::AudioBuffer<float> inputBuffer;
    bool inputEnabled = false;
};

} // namespace rb
