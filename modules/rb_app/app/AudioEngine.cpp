namespace rb
{

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
}

void AudioEngine::start (bool withInput)
{
    inputEnabled = withInput;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.initialise (withInput ? 1 : 0, 2, nullptr, true, {}, nullptr);

    setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate = 48000.0;
    deviceManager.setAudioDeviceSetup (setup, true);

    deviceManager.addAudioCallback (this);
}

void AudioEngine::enableInput (std::function<void (bool)> onDone)
{
    auto open = [this, onDone] (bool granted)
    {
        if (granted && ! inputEnabled)
        {
            inputEnabled = true;
            auto setup = deviceManager.getAudioDeviceSetup();
            setup.useDefaultInputChannels = true;
            setup.inputChannels.setRange (0, 1, true);
            deviceManager.setAudioDeviceSetup (setup, true);
        }
        if (onDone != nullptr)
            onDone (granted && inputEnabled);
    };

    if (juce::RuntimePermissions::isRequired (juce::RuntimePermissions::recordAudio)
        && ! juce::RuntimePermissions::isGranted (juce::RuntimePermissions::recordAudio))
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio, open);
    else
        open (true);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const int blockSize = juce::jmax (512, device->getCurrentBufferSizeSamples());
    stereo.setSize (2, blockSize);
    inputBuffer.setSize (2, blockSize);
    session.prepare (device->getCurrentSampleRate(), blockSize);
}

void AudioEngine::audioDeviceStopped()
{
    session.releaseResources();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                    float* const* outputs, int numOutputs, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;

    // Devices may deliver larger blocks than announced; render in prepared-size pieces.
    for (int start = 0; start < numSamples;)
    {
        const int n = juce::jmin (stereo.getNumSamples(), numSamples - start);

        const juce::AudioBuffer<float>* input = nullptr;
        if (numInputs > 0 && inputs != nullptr && inputs[0] != nullptr)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                const float* source = inputs[juce::jmin (ch, numInputs - 1)];
                inputBuffer.copyFrom (ch, 0, (source != nullptr ? source : inputs[0]) + start, n);
            }
            input = &inputBuffer;
        }

        session.process (stereo, n, input);

        for (int ch = 0; ch < numOutputs; ++ch)
        {
            if (outputs[ch] == nullptr)
                continue;

            float* out = outputs[ch] + start;

            if (numOutputs == 1)
            {
                juce::FloatVectorOperations::copy (out, stereo.getReadPointer (0), n);
                juce::FloatVectorOperations::add (out, stereo.getReadPointer (1), n);
                juce::FloatVectorOperations::multiply (out, 0.5f, n);
            }
            else if (ch < 2)
            {
                juce::FloatVectorOperations::copy (out, stereo.getReadPointer (ch), n);
            }
            else
            {
                juce::FloatVectorOperations::clear (out, n);
            }

            // Last line of defence against overs.
            juce::FloatVectorOperations::clip (out, out, -1.0f, 1.0f, n);
        }

        start += n;
    }
}

} // namespace rb
