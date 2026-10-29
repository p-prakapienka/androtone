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

#include <set>

#include "app/AudioEngine.h"
#include "app/AppSettings.h"
#include "app/SetLibrary.h"
#include "app/MidiInputs.h"
#include "app/Studio.h"

#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/MainComponent.h"
#include "ui/PadViews.h"
#include "ui/SampleView.h"
#include "ui/ClipEditor.h"
#include "ui/BrowserSheet.h"
#include "ui/SettingsSheets.h"
#include "ui/MidiSettingsSheet.h"
#include "ui/DevicePanel.h"
#include "ui/InstrumentView.h"
#include "ui/SessionView.h"
#include "ui/LibraryView.h"
