cmake_minimum_required(VERSION 3.22)
project(SchwungBreakbeat VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(FetchContent)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(BREAKBEAT_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND BREAKBEAT_FORMATS AU)
endif()
juce_add_plugin(SchwungBreakbeat
    COMPANY_NAME Schwung
    BUNDLE_ID com.schwung.breakbeat
    PLUGIN_MANUFACTURER_CODE Schg
    PLUGIN_CODE SwBb
    PRODUCT_NAME "Schwung Breakbeat"
    FORMATS ${BREAKBEAT_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(SchwungBreakbeat PRIVATE
    Source/BreakbeatEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    ../src/dsp/slice_select.c
    ../src/dsp/manual_slices.c
    ../src/dsp/grain_stretch.c)
target_compile_definitions(SchwungBreakbeat PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_IGNORE_VST3_MISMATCHED_PARAMETER_ID_WARNING=1)
target_link_libraries(SchwungBreakbeat PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

include(CTest)
if(BUILD_TESTING)
    add_executable(breakbeat_engine_test
        tests/EngineTest.cpp Source/BreakbeatEngine.cpp
        ../src/dsp/slice_select.c ../src/dsp/manual_slices.c ../src/dsp/grain_stretch.c)
    target_include_directories(breakbeat_engine_test PRIVATE Source)
    add_test(NAME engine COMMAND breakbeat_engine_test)
endif()
