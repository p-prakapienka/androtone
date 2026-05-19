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

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(ANDROTONE_FORMATS Standalone)

    set(ANDROTONE_JUCE_JAVA_DEST "${CMAKE_SOURCE_DIR}/Builds/android/app/build/generated/juce-java")
    file(MAKE_DIRECTORY "${ANDROTONE_JUCE_JAVA_DEST}")
    foreach(juce_java_src IN ITEMS
        "${JUCE_SOURCE_DIR}/modules/juce_core/native/javacore/app/com"
        "${JUCE_SOURCE_DIR}/modules/juce_core/native/javacore/init/com"
        "${JUCE_SOURCE_DIR}/modules/juce_gui_basics/native/java/app/com"
        "${JUCE_SOURCE_DIR}/modules/juce_gui_basics/native/javaopt/app/com"
        "${JUCE_SOURCE_DIR}/modules/juce_audio_devices/native/java/app/com")
        file(COPY "${juce_java_src}" DESTINATION "${ANDROTONE_JUCE_JAVA_DEST}")
    endforeach()
else()
    set(ANDROTONE_FORMATS VST3 Standalone)
endif()

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
        source/PluginEditor.cpp
        source/PluginProcessor.cpp)

target_include_directories(Androtone
    PRIVATE
        source)

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
