cmake_minimum_required(VERSION 3.22)

project(Natron VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=c8cdc32c03816538ce22781ed72964dc864b2a34a310d3b7104812a5ca2d835d
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# JUCE is fetched into a fixed, checkout-relative folder (natron/external/JUCE, git-ignored) rather
# than the build tree, so the Projucer-exported Android project can reference the same modules
# through a relative path no matter which desktop build directory configured it.
CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 8.0.12
    VERSION 8.0.12
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE
)

# The engine: no GUI, no audio-device code. Compiled into both the app and the tests.
set(NATRON_CORE_SOURCES
    source/core/Arrangement.cpp
    source/core/Engine.cpp
    source/core/SongExport.cpp
    source/core/effects/Effects.cpp
    source/core/effects/MoreEffects.cpp
    source/core/MachineRegistry.cpp
    source/core/PatternPlayer.cpp
    source/core/SampleData.cpp
    source/core/Session.cpp
    source/core/SongFile.cpp
    source/core/dsp/DrumKit.cpp
    source/core/machines/AcidBass.cpp
    source/core/machines/Bytebeat.cpp
    source/core/machines/BytebeatProgram.cpp
    source/core/machines/DrumMachine.cpp
    source/core/machines/FmSynth.cpp
    source/core/machines/Modular.cpp
    source/core/machines/Organ.cpp
    source/core/machines/PadSynth.cpp
    source/core/machines/Pluck.cpp
    source/core/machines/Sampler.cpp
    source/core/machines/Supersaw.cpp
    source/core/machines/SubtractiveSynth.cpp
    source/core/machines/TestToneMachine.cpp
    source/core/machines/Vocoder.cpp
    source/core/mixer/ChannelStrip.cpp
    source/core/mixer/MasterBus.cpp)

set(NATRON_CORE_MODULES
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_data_structures
    juce::juce_dsp)

set(NATRON_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# ---------------------------------------------------------------------------
# App (desktop). Android is built from the Projucer export in Builds/Android.
# ---------------------------------------------------------------------------
juce_add_gui_app(Natron
    PRODUCT_NAME "Natron"
    COMPANY_NAME "Restricted Space"
    BUNDLE_ID    com.restrictedspace.natron)

target_sources(Natron
    PRIVATE
        ${NATRON_CORE_SOURCES}
        source/app/Main.cpp
        source/app/MainComponent.cpp
        source/app/MixerView.cpp
        source/app/ParameterPanel.cpp
        source/app/PatternEditorView.cpp
        source/app/RackView.cpp
        source/app/SongView.cpp)

target_include_directories(Natron PRIVATE source)

target_compile_definitions(Natron
    PRIVATE
        ${NATRON_COMMON_DEFINITIONS}
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Natron,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Natron,JUCE_VERSION>")

target_link_libraries(Natron
    PRIVATE
        ${NATRON_CORE_MODULES}
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests: headless Catch2 runner over the engine only (no GUI modules linked).
#   cmake --build <build-dir> --target NatronTests
#   ctest --test-dir <build-dir> --output-on-failure
# ---------------------------------------------------------------------------
option(NATRON_BUILD_TESTS "Build the NatronTests offline test runner" ON)

if(NATRON_BUILD_TESTS)
    CPMAddPackage("gh:catchorg/Catch2@3.7.1")

    juce_add_console_app(NatronTests PRODUCT_NAME "NatronTests")

    target_sources(NatronTests
        PRIVATE
            ${NATRON_CORE_SOURCES}
            tests/AllocationCounter.cpp
            tests/TransportTests.cpp
            tests/PatternPlayerTests.cpp
            tests/ParameterTests.cpp
            tests/PatternBankTests.cpp
            tests/EngineTests.cpp
            tests/SessionTests.cpp
            tests/SongFileTests.cpp
            tests/MachineTests.cpp
            tests/MixerTests.cpp
            tests/GoldenTests.cpp
            tests/ArrangementTests.cpp
            tests/SongModeTests.cpp
            tests/EffectTests.cpp
            tests/ExportTests.cpp
            tests/InfrastructureTests.cpp
            tests/MachineTests4b.cpp
            tests/MachineTests4c.cpp
            tests/EffectTests4d.cpp)

    target_include_directories(NatronTests PRIVATE source tests)
    target_compile_definitions(NatronTests
        PRIVATE
            ${NATRON_COMMON_DEFINITIONS}
            NATRON_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/golden")

    find_package(Threads REQUIRED)
    target_link_libraries(NatronTests
        PRIVATE
            ${NATRON_CORE_MODULES}
            Threads::Threads
            Catch2::Catch2WithMain
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(NatronTests)
endif()
