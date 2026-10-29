#ifdef RB_ENGINE_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of JUCE cpp file"
#endif

#include "rb_engine.h"

#include "devices/instruments/AnalogSynth.h"
#include "devices/instruments/DrumKit.h"
#include "devices/instruments/MelodicSampler.h"
#include "devices/instruments/WaveSynth.h"
#include "devices/instruments/AnalogWaveCommon.h"
#include "devices/instruments/WaveTables.h"

#include "devices/FactorySounds.cpp"
#include "devices/instruments/AnalogSynth.cpp"
#include "devices/instruments/DrumKit.cpp"
#include "devices/instruments/MelodicSampler.cpp"
#include "devices/instruments/WaveTables.cpp"
#include "devices/instruments/WaveSynth.cpp"
