cmake_minimum_required(VERSION 3.22)

project(Cliptone VERSION 0.0.1)

file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=c8cdc32c03816538ce22781ed72964dc864b2a34a310d3b7104812a5ca2d835d
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 8.0.12
    VERSION 8.0.12
)

set(CLIPTONE_FORMATS VST3 Standalone)

juce_add_plugin(Cliptone
    COMPANY_NAME              "Restricted Space"
    BUNDLE_ID                 com.restrictedspace.cliptone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE  RstS
    PLUGIN_CODE               Clpt
    FORMATS                   ${CLIPTONE_FORMATS}
    PRODUCT_NAME              "Cliptone")

target_sources(Cliptone
    PRIVATE
        ../../source/PluginEditor.cpp
        ../../source/PluginProcessor.cpp
        ../../source/Engine/Devices.h
        ../../source/Engine/Processors/TrackEffects.h
        ../../source/Engine/SoundSources/AndroSynth.h
        ../../source/Engine/SoundSources/DrumKit.h
        ../../source/Engine/SoundSources/Metronome.h
        ../../source/Mixer/Automation.h
        ../../source/Project/SetSerializer.h
        ../../source/Sequencer/Arrangement.h
        ../../source/Sequencer/ClipData.h
        ../../source/Sequencer/RecordEvents.h
        ../../source/Sequencer/Recorder.h
        ../../source/Sequencer/TapTempo.h
        ../../source/Ui/ArrangeTab.h
        ../../source/Ui/ClipPropertiesPanel.h
        ../../source/Ui/ClipTab.h
        ../../source/Ui/DeviceTab.h
        ../../source/Ui/NoteGrid.h
        # Projucer-generated embedded resources (the kick sample). BinaryData is
        # checked in (see .gitignore) even though the rest of JuceLibraryCode is
        # not; re-save the .jucer in Projucer to regenerate it when resources change.
        ../../JuceLibraryCode/BinaryData.cpp)

target_include_directories(Cliptone
    PRIVATE
        ../../source)

target_compile_definitions(Cliptone
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Cliptone
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless sequencer tests: cmake -DCLIPTONE_BUILD_TESTS=ON, then run CliptoneTests.
option(CLIPTONE_BUILD_TESTS "Build the headless sequencer tests" OFF)
if(CLIPTONE_BUILD_TESTS)
    juce_add_console_app(CliptoneTests PRODUCT_NAME "CliptoneTests")
    target_sources(CliptoneTests PRIVATE ../../tests/SequencerTests.cpp)
    target_include_directories(CliptoneTests PRIVATE ../../source)
    target_compile_definitions(CliptoneTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(CliptoneTests PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    enable_testing()
    add_test(NAME CliptoneTests COMMAND CliptoneTests)
endif()
