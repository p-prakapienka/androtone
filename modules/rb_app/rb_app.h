/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 rb_app
  vendor:             riffbook
  version:            0.1.0
  name:               Riffbook app
  description:        Audio device glue, set library and the touch UI.
  minimumCppStandard: 17

  dependencies:       rb_engine juce_audio_devices juce_audio_utils juce_gui_extra

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once
#define RB_APP_H_INCLUDED

#include <rb_engine/rb_engine.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "app/AudioEngine.h"
#include "ui/MainComponent.h"
