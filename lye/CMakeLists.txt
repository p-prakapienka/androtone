cmake_minimum_required(VERSION 3.22)

project(Lye VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.2/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
    EXPECTED_HASH SHA256=c8cdc32c03816538ce22781ed72964dc864b2a34a310d3b7104812a5ca2d835d
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

# JUCE is fetched into a fixed, checkout-relative folder (lye/external/JUCE, git-ignored) rather
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
set(LYE_CORE_SOURCES
    source/core/Engine.cpp
    source/core/PatternPlayer.cpp
    source/core/machines/TestToneMachine.cpp)

set(LYE_CORE_MODULES
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_dsp)

set(LYE_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

# ---------------------------------------------------------------------------
# App (desktop). Android is built from the Projucer export in Builds/Android.
# ---------------------------------------------------------------------------
juce_add_gui_app(Lye
    PRODUCT_NAME "Lye"
    COMPANY_NAME "Restricted Space"
    BUNDLE_ID    com.restrictedspace.lye)

target_sources(Lye
    PRIVATE
        ${LYE_CORE_SOURCES}
        source/app/Main.cpp
        source/app/MainComponent.cpp)

target_include_directories(Lye PRIVATE source)

target_compile_definitions(Lye
    PRIVATE
        ${LYE_COMMON_DEFINITIONS}
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Lye,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Lye,JUCE_VERSION>")

target_link_libraries(Lye
    PRIVATE
        ${LYE_CORE_MODULES}
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests: headless Catch2 runner over the engine only (no GUI modules, so no X11/ALSA needed).
#   cmake --build <build-dir> --target LyeTests
#   ctest --test-dir <build-dir> --output-on-failure
# ---------------------------------------------------------------------------
option(LYE_BUILD_TESTS "Build the LyeTests offline test runner" ON)

if(LYE_BUILD_TESTS)
    CPMAddPackage("gh:catchorg/Catch2@3.7.1")

    juce_add_console_app(LyeTests PRODUCT_NAME "LyeTests")

    target_sources(LyeTests
        PRIVATE
            ${LYE_CORE_SOURCES}
            tests/TransportTests.cpp
            tests/PatternPlayerTests.cpp
            tests/EngineTests.cpp)

    target_include_directories(LyeTests PRIVATE source tests)
    target_compile_definitions(LyeTests PRIVATE ${LYE_COMMON_DEFINITIONS})

    target_link_libraries(LyeTests
        PRIVATE
            ${LYE_CORE_MODULES}
            Catch2::Catch2WithMain
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    enable_testing()
    list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(LyeTests)
endif()
