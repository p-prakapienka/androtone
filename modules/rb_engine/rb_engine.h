/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 rb_engine
  vendor:             riffbook
  version:            0.1.0
  name:               Riffbook engine
  description:        Set model, sequencer, instruments, effects and offline rendering.
  minimumCppStandard: 17

  dependencies:       juce_audio_basics juce_audio_formats juce_dsp juce_data_structures juce_events

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once
#define RB_ENGINE_H_INCLUDED

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "model/Music.h"
#include "model/Params.h"
#include "model/SampleData.h"
#include "model/Set.h"
#include "model/SetJson.h"

#include "devices/Device.h"
#include "devices/Dsp.h"
#include "devices/DeviceRegistry.h"
#include "devices/FactorySounds.h"
#include "devices/Presets.h"
#include "model/Edits.h"

#include "engine/Snapshot.h"
#include "engine/Commands.h"
#include "engine/SessionProcessor.h"
#include "engine/Document.h"

#include "capture/CaptureBuffer.h"
#include "capture/TempoDetect.h"

#include "render/OfflineRenderer.h"
