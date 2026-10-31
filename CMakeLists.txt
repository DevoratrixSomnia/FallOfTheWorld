cmake_minimum_required(VERSION 3.22)
project(FallOfTheWorld VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE is downloaded automatically the first time you configure the project.
# (Needs git + internet access once.)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# AU only exists on macOS, so we only ask for it there.
if(APPLE)
    set(FOTW_FORMATS VST3 AU Standalone)
else()
    set(FOTW_FORMATS VST3 Standalone)
endif()

juce_add_plugin(FallOfTheWorld
    COMPANY_NAME                "YourName"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    PLUGIN_MANUFACTURER_CODE    Vlad
    PLUGIN_CODE                 Fotw
    FORMATS                     ${FOTW_FORMATS}
    PRODUCT_NAME                "Fall of the World"
    VST3_CATEGORIES             Fx Distortion)

target_sources(FallOfTheWorld PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/FotwLookAndFeel.h)

target_compile_definitions(FallOfTheWorld PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FallOfTheWorld
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
