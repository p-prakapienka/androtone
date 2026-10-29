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
    SampleLibrary& getSamples() { return samples; }
    Document& getDocument() { return document; }
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }

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
