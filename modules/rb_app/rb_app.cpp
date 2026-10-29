#ifdef RB_APP_H_INCLUDED
 #error "Incorrect use of JUCE cpp file"
#endif

#include "rb_app.h"

#include "app/AudioEngine.cpp"
#include "app/AppSettings.cpp"
#include "app/SetLibrary.cpp"
#include "app/DemoSets.cpp"
#include "app/Studio.cpp"

#include "ui/Theme.cpp"
#include "ui/Widgets.cpp"
#include "ui/MainComponent.cpp"
#include "ui/LibraryView.cpp"
#include "ui/SessionView.cpp"
#include "ui/PadViews.cpp"
#include "ui/DevicePanel.cpp"
#include "ui/SampleView.cpp"
#include "ui/ClipEditor.cpp"
#include "ui/BrowserSheet.cpp"
#include "ui/SettingsSheets.cpp"
#include "ui/InstrumentView.cpp"
