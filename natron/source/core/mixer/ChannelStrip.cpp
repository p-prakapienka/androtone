#include "ChannelStrip.h"

ChannelStrip::ChannelStrip() {
    parameters.add({ "volume", "Volume", 0.0f, 1.5f, 0.8f });
    parameters.add({ "pan", "Pan", -1.0f, 1.0f, 0.0f });
    parameters.add({ "width", "Width", 0.0f, 2.0f, 1.0f });
    parameters.add({ "eqLow", "Low", -15.0f, 15.0f, 0.0f });   // dB
    parameters.add({ "eqMid", "Mid", -15.0f, 15.0f, 0.0f });
    parameters.add({ "eqHigh", "High", -15.0f, 15.0f, 0.0f });
    parameters.add({ "delaySend", "Delay", 0.0f, 1.0f, 0.0f });
    parameters.add({ "reverbSend", "Reverb", 0.0f, 1.0f, 0.0f });
    parameters.add({ "mute", "Mute", 0.0f, 1.0f, 0.0f, 1.0f });
    parameters.add({ "solo", "Solo", 0.0f, 1.0f, 0.0f, 1.0f });
    jassert(parameters.size() == numParameters);
}

void ChannelStrip::prepare(double newSampleRate) {
    sampleRate = newSampleRate;
    low.reset();
    mid.reset();
    high.reset();
    lowGainDb = midGainDb = highGainDb = 0.0f;
    updateEq();

    primed = false;
}

void ChannelStrip::updateEq() {
    const float newLow = parameters.get(eqLow);
    const float newMid = parameters.get(eqMid);
    const float newHigh = parameters.get(eqHigh);
    if (!juce::exactlyEqual(newLow, lowGainDb)) {
        low.setLowShelf(sampleRate, lowShelfHz, newLow);
        lowGainDb = newLow;
    }
    if (!juce::exactlyEqual(newMid, midGainDb)) {
        mid.setPeak(sampleRate, midPeakHz, 0.7, newMid);
        midGainDb = newMid;
    }
    if (!juce::exactlyEqual(newHigh, highGainDb)) {
        high.setHighShelf(sampleRate, highShelfHz, newHigh);
        highGainDb = newHigh;
    }
}

void ChannelStrip::process(juce::AudioBuffer<float>& buffer, int numSamples, bool audible) {
    float* left = buffer.getWritePointer(0);
    float* right = buffer.getWritePointer(1);

    updateEq();
    const bool eqActive = !juce::exactlyEqual(lowGainDb, 0.0f) || !juce::exactlyEqual(midGainDb, 0.0f)
                          || !juce::exactlyEqual(highGainDb, 0.0f);
    const float widthValue = parameters.get(width);
    const bool widthActive = !juce::exactlyEqual(widthValue, 1.0f);

    if (eqActive || widthActive) {
        for (int i = 0; i < numSamples; i++) {
            float l = left[i];
            float r = right[i];
            if (eqActive) {
                l = high.process(0, mid.process(0, low.process(0, l)));
                r = high.process(1, mid.process(1, low.process(1, r)));
            }
            if (widthActive) {
                const float m = 0.5f * (l + r);
                const float s = 0.5f * (l - r) * widthValue;
                l = m + s;
                r = m - s;
            }
            left[i] = l;
            right[i] = r;
        }
    }

    // Volume, balance-law pan (centre = unity, hard left silences the right), and audibility.
    const float volumeValue = parameters.get(volume);
    const float panValue = parameters.get(pan);
    const float leftGain = volumeValue * juce::jmin(1.0f, 1.0f - panValue);
    const float rightGain = volumeValue * juce::jmin(1.0f, 1.0f + panValue);
    const float audibility = audible ? 1.0f : 0.0f;

    if (!primed) {
        lastLeftGain = leftGain;
        lastRightGain = rightGain;
        currentAudibility = audibility;
        lastDelaySend = parameters.get(delaySend);
        lastReverbSend = parameters.get(reverbSend);
        primed = true;
    }
    lastAudibility = currentAudibility;
    currentAudibility = audibility;

    buffer.applyGainRamp(0, 0, numSamples, lastLeftGain * lastAudibility, leftGain * currentAudibility);
    buffer.applyGainRamp(1, 0, numSamples, lastRightGain * lastAudibility, rightGain * currentAudibility);
    lastLeftGain = leftGain;
    lastRightGain = rightGain;
}

void ChannelStrip::addSends(const juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& delayBus,
                            juce::AudioBuffer<float>& reverbBus, int numSamples) {
    // Sends follow the channel's audibility too: a muted channel sends nothing.
    const float delayAmount = parameters.get(delaySend) * currentAudibility;
    const float reverbAmount = parameters.get(reverbSend) * currentAudibility;

    for (int ch = 0; ch < 2; ch++) {
        if (delayAmount > 0.0f || lastDelaySend > 0.0f) {
            delayBus.addFromWithRamp(ch, 0, buffer.getReadPointer(ch), numSamples, lastDelaySend, delayAmount);
        }
        if (reverbAmount > 0.0f || lastReverbSend > 0.0f) {
            reverbBus.addFromWithRamp(ch, 0, buffer.getReadPointer(ch), numSamples, lastReverbSend, reverbAmount);
        }
    }
    lastDelaySend = delayAmount;
    lastReverbSend = reverbAmount;
}
