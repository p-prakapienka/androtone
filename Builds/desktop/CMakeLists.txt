cmake_minimum_required(VERSION 3.22)

project(Androtone VERSION 0.0.1)

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

set(ANDROTONE_FORMATS VST3 Standalone)

juce_add_plugin(Androtone
    COMPANY_NAME              "Restricted Space"
    BUNDLE_ID                 com.restrictedspace.androtone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE  RstS
    PLUGIN_CODE               Antn
    FORMATS                   ${ANDROTONE_FORMATS}
    PRODUCT_NAME              "Androtone")

target_sources(Androtone
    PRIVATE
        ../../source/PluginEditor.cpp
        ../../source/PluginProcessor.cpp
        ../../source/Engine/SoundSources/Metronome.h
        ../../source/Project/SetSerializer.h
        ../../source/Sequencer/Arrangement.h
        ../../source/Sequencer/ClipData.h
        ../../source/Sequencer/RecordEvents.h
        ../../source/Sequencer/Recorder.h
        ../../source/Sequencer/TapTempo.h
        ../../source/Ui/ArrangeTab.h
        ../../source/Ui/ClipPropertiesPanel.h
        ../../source/Ui/ClipTab.h
        ../../source/Ui/NoteGrid.h
        # Projucer-generated embedded resources (the kick sample). BinaryData is
        # checked in (see .gitignore) even though the rest of JuceLibraryCode is
        # not; re-save the .jucer in Projucer to regenerate it when resources change.
        ../../JuceLibraryCode/BinaryData.cpp)

target_include_directories(Androtone
    PRIVATE
        ../../source)

target_compile_definitions(Androtone
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Androtone
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Headless sequencer tests: cmake -DANDROTONE_BUILD_TESTS=ON, then run AndrotoneTests.
option(ANDROTONE_BUILD_TESTS "Build the headless sequencer tests" OFF)
if(ANDROTONE_BUILD_TESTS)
    juce_add_console_app(AndrotoneTests PRODUCT_NAME "AndrotoneTests")
    target_sources(AndrotoneTests PRIVATE ../../tests/SequencerTests.cpp)
    target_include_directories(AndrotoneTests PRIVATE ../../source)
    target_compile_definitions(AndrotoneTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AndrotoneTests PRIVATE juce::juce_audio_processors juce::juce_recommended_config_flags)
    enable_testing()
    add_test(NAME AndrotoneTests COMMAND AndrotoneTests)
endif()
