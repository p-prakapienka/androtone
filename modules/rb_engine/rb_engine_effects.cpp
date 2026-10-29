#ifdef RB_ENGINE_H_INCLUDED
 /* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
 #error "Incorrect use of JUCE cpp file"
#endif

#include "rb_engine.h"

#include "devices/effects/AutoFilter.h"
#include "devices/effects/ChannelEq.h"
#include "devices/effects/Chorus.h"
#include "devices/effects/Crusher.h"
#include "devices/effects/Delay.h"
#include "devices/effects/Dynamics.h"
#include "devices/effects/Phaser.h"
#include "devices/effects/Reverb.h"
#include "devices/effects/Saturator.h"

#include "devices/effects/AutoFilter.cpp"
#include "devices/effects/ChannelEq.cpp"
#include "devices/effects/Chorus.cpp"
#include "devices/effects/Crusher.cpp"
#include "devices/effects/Delay.cpp"
#include "devices/effects/Dynamics.cpp"
#include "devices/effects/Phaser.cpp"
#include "devices/effects/Reverb.cpp"
#include "devices/effects/Saturator.cpp"
