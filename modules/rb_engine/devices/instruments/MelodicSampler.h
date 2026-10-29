#pragma once

#include "SamplerPlayback.h"

namespace rb
{

// Sampler: one sample played chromatically (DEV-14, SMP-6, SMP-7). samplerRootNote (C3) plays
// the sample at its original pitch; other notes repitch it (Hermite interpolation, resampled
// to the engine rate). 16 voices, AD envelope with Trig/Gate mode and release, a LP/BP/HP
// filter whose cutoff a global LFO can modulate. Voices end when the sample runs out.
//
// Trig: attack then decay to silence, note-off is ignored. Gate: attack then decay while the
// key is held, release from the current level at note-off. Decay at its maximum is infinite.
class MelodicSampler : public InstrumentProcessor
{
public:
    MelodicSampler();

    static DeviceSpec createSpec();

    enum Param
    {
        pFilterType = 0, pCutoff, pResonance, pTranspose, pDetune, pAttack, pDecay, pEnvMode, pRelease,
        pFilterLfo, pLfoRate, pVolume, pGain, pSampleStart,
        numParams
    };

    static constexpr int maxVoices = 16;
    static constexpr int voicePoolSize = 24;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void setPitchBend (float semitones) override { bendTarget = semitones; }
    void setPressure (int note, float pressure) override;
    void setSample (int slot, const SampleData* sample) override;
    void process (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& send,
                  const NoteEvent* events, int numEvents, int numSamples) override;
    void allNotesOff() override;

    int getNumActiveVoices() const;

private:
    struct Voice
    {
        bool active = false;
        int note = 60;
        juce::uint32 age = 0;
        float velocityGain = 1.0f;
        double position = 0.0;

        enum class Stage { attack, decay, release, done };
        Stage stage = Stage::attack;
        float envLevel = 0.0f, envPhase = 0.0f;
        bool gateMode = true;
        bool held = true;

        bool fading = false;
        float fadeGain = 1.0f, fadeStep = 0.0f;

        std::array<dsp::Svf, 2> filters;
        float gain = 0.0f;
        float lastL = 0.0f, lastR = 0.0f;
    };

    void handleEvent (const NoteEvent&);
    void startVoice (const NoteEvent&);
    Voice* allocateVoice();
    void killVoice (Voice&);
    void beginFade (Voice&, float seconds);
    void renderRange (float* dl, float* dr, int start, int count);
    void renderVoice (Voice&, float* dl, float* dr, int count, float cutoff, float lfoGainTarget);

    double sampleRate = 48000.0;
    std::array<float, numParams> values {};
    const SampleData* sample = nullptr;
    std::array<Voice, voicePoolSize> voices;
    juce::uint32 nextAge = 1;

    double lfoPhase = 0.0;
    float bend = 0.0f, bendTarget = 0.0f, channelPressure = 0.0f;
    std::array<float, 128> notePressure {};
    float logCutoff = 14.0f;
    dsp::Smoother volume;
    sampler::Declicker declicker;
};

} // namespace rb
