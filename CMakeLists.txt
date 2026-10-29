cmake_minimum_required(VERSION 3.22)

project(Riffbook VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE comes from a local checkout when RB_JUCE_DIR is set (CI and the Android export use
# ./JUCE), otherwise it is fetched with git at configure time.
set(RB_JUCE_DIR "" CACHE PATH "Path to a JUCE 8.0.12 checkout (optional)")

if(RB_JUCE_DIR)
    add_subdirectory(${RB_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# All app code lives in two JUCE modules, so the Projucer project (Android) and this CMake
# build pick up new source files without editing either file list.
juce_add_module(modules/rb_engine)
juce_add_module(modules/rb_app)

set(RB_COMMON_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# --- The app (desktop build, used for development) -------------------------------------
juce_add_gui_app(Riffbook
    PRODUCT_NAME "Riffbook"
    COMPANY_NAME "Restricted Space"
    BUNDLE_ID com.restrictedspace.riffbook
    MICROPHONE_PERMISSION_ENABLED TRUE)

target_sources(Riffbook PRIVATE Source/Main.cpp)

target_compile_definitions(Riffbook PRIVATE
    ${RB_COMMON_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Riffbook,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Riffbook,JUCE_VERSION>")

target_link_libraries(Riffbook
    PRIVATE
        rb_app
        rb_engine
        juce::juce_audio_utils
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Tests ------------------------------------------------------------------------------
juce_add_console_app(RiffbookTests PRODUCT_NAME "RiffbookTests")

file(GLOB RB_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp)
target_sources(RiffbookTests PRIVATE ${RB_TEST_SOURCES})

target_compile_definitions(RiffbookTests PRIVATE ${RB_COMMON_DEFINITIONS})

target_link_libraries(RiffbookTests
    PRIVATE
        rb_engine
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME RiffbookTests COMMAND RiffbookTests)
