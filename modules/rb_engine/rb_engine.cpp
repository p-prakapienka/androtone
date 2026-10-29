#ifdef RB_ENGINE_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of JUCE cpp file"
#endif

#include "rb_engine.h"

// Device classes are only needed by the registry.
#include "devices/instruments/AnalogSynth.h"
#include "devices/instruments/DrumKit.h"
#include "devices/instruments/MelodicSampler.h"
#include "devices/instruments/WaveSynth.h"
#include "devices/effects/AutoFilter.h"
#include "devices/effects/ChannelEq.h"
#include "devices/effects/Chorus.h"
#include "devices/effects/Crusher.h"
#include "devices/effects/Delay.h"
#include "devices/effects/Dynamics.h"
#include "devices/effects/Phaser.h"
#include "devices/effects/Reverb.h"
#include "devices/effects/Saturator.h"

#include "model/Edits.cpp"
#include "model/Music.cpp"
#include "model/Params.cpp"
#include "model/SampleData.cpp"
#include "model/Set.cpp"
#include "model/SetJson.cpp"
#include "devices/DeviceRegistry.cpp"
#include "devices/Presets.cpp"
#include "engine/Document.cpp"
#include "engine/SessionProcessor.cpp"
#include "capture/CaptureBuffer.cpp"
#include "capture/TempoDetect.cpp"
#include "render/OfflineRenderer.cpp"
