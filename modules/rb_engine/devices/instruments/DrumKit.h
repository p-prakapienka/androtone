#pragma once

#include "SamplerPlayback.h"

namespace rb
{

// Drum Kit: a rack of 16 Pad Samplers (DEV-12, DEV-13, DEV-21, PAD-1, SMP-6).
//
// Pad n answers MIDI note drumBaseNote + n. Each hit is a voice: the pad's sample is read with
// Hermite interpolation at transpose + detune + the note's pitch offset (resampled from the
// sample's rate to the engine rate), runs through the pad's playback FX and filter, then the
// AHD envelope, gain and velocity, and is panned.
//
// Send: the pad's `send` amount taps the voice after the pan (post-envelope, post-pan), so
// the Pad FX return keeps each pad's stereo position. Dry always gets the full signal.
//
// Voices: up to 32 sounding hits; a retriggered pad keeps its previous hit ringing (except in
// the choke group). Beyond 32 the oldest hit fades out over 3 ms.
class DrumKit : public InstrumentProcessor
{
public:
    DrumKit();

    static DeviceSpec createSpec();
    // Per-pad parameters (Pad Sampler, DEV-12/13).
    static DeviceSpec createPadSpec();

    // Indices into createPadSpec().params.
    enum PadParam
    {
        pTranspose = 0, pFilterType, pCutoff, pResonance, pDetune, pAttack, pHold, pEnvMode, pDecay,
        pPan, pFxType, pFx1, pFx2, pVelTarget, pVelAmount, pVelVolume, pSend, pChoke, pGain, pSampleStart,
        numPadParams
    };

    enum FilterType { filterLp12 = 0, filterLp24, filterHp24, filterPeak };
    enum FxType { fxNone = 0, fxStretch, fxLoop, fxPitchEnv, fxPunch, fxBitCrush, fxFm, fxRingMod, fxSubOsc, fxNoise };
    enum VelTarget { velFilter = 0, velAttack, velHold, velDecay, velFx1, velFx2 };

    static constexpr int maxVoices = 32;       // sounding hits
    static constexpr int voicePoolSize = 40;   // extra room for hits that are fading out

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() override;
    void setParameters (const float* values, int numValues) override;
    void setPadParameters (int pad, const float* values, int numValues) override;
    void setSample (int pad, const SampleData* sample) override;
    void process (juce::AudioBuffer<float>& dry, juce::AudioBuffer<float>& send,
                  const NoteEvent* events, int numEvents, int numSamples) override;
    void allNotesOff() override;

    int getNumActiveVoices() const;

private:
    struct Voice
    {
        bool active = false;
        int pad = -1;
        const SampleData* sample = nullptr;
        juce::uint32 age = 0;

        float velocity = 1.0f, velocityCurve = 1.0f, pitchOffset = 0.0f;
        double rateRatio = 1.0;   // sample rate / engine rate
        double startFrame = 0.0;
        double position = 0.0;    // playhead, in sample frames
        juce::int64 elapsed = 0;  // samples since note-on

        // AHD envelope
        enum class Stage { attack, hold, decay, release, done };
        Stage stage = Stage::attack;
        float envLevel = 0.0f, envPhase = 0.0f, releaseStep = 0.0f;
        bool gateMode = false;

        // Choke / steal fade
        bool fading = false;
        float fadeGain = 1.0f, fadeStep = 0.0f;

        // Filter: [left stage 1, left stage 2, right stage 1, right stage 2]
        std::array<dsp::Svf, 4> filters;
        float logCutoff = 0.0f;
        float peakGain = 1.0f;

        // Output gains, ramped per chunk
        float gainL = 0.0f, gainR = 0.0f, sendGain = 0.0f;

        // Playback FX state
        double stretchPosition = 0.0;
        std::array<double, 2> grainHead {};
        std::array<float, 2> grainPhase {};
        float fxPhase = 0.0f, fxEnv = 1.0f;
        float crushPhase = 1.0f;
        sampler::Frame crushHeld;
        float follower = 0.0f, noiseLp = 0.0f;
        sampler::NoiseSource noise;

        float lastL = 0.0f, lastR = 0.0f, lastSendL = 0.0f, lastSendR = 0.0f;
    };

    // Values derived from the pad's parameters for one chunk of a voice.
    struct ChunkParams;

    void handleEvent (const NoteEvent&);
    void startVoice (int pad, const NoteEvent&);
    Voice* allocateVoice();
    void killVoice (Voice&);
    void beginFade (Voice&, float seconds);
    ChunkParams computeChunkParams (const Voice&) const;
    void renderRange (float* dl, float* dr, float* sl, float* sr, int start, int count);
    void renderVoice (Voice&, float* dl, float* dr, float* sl, float* sr, int count);

    double sampleRate = 48000.0;
    std::array<std::array<float, numPadParams>, numDrumPads> padValues {};
    std::array<const SampleData*, numDrumPads> samples {};
    std::array<Voice, voicePoolSize> voices;
    juce::uint32 nextAge = 1;
    juce::uint32 noiseSeed = 0x2545F491u;

    float volumeDb = 0.0f;
    dsp::Smoother volume;
    sampler::Declicker declicker;
};

} // namespace rb
