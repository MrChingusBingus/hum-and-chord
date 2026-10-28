cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS supported")
project(HumAndChord VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set HC_CHECK_ONLY=ON to compile-check the sources on a machine without the
# Linux desktop libraries (no web view is linked).
option(HC_CHECK_ONLY "Only compile the plugin's own sources" OFF)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# The whole user interface is the Hum & Chord web page, embedded in the plugin.
juce_add_binary_data(HumAndChordWeb SOURCES web/index.html)

if(HC_CHECK_ONLY)
    set(HC_WEB FALSE)
else()
    set(HC_WEB TRUE)
endif()

function(hc_add_plugin target code formats is_midi_effect is_synth)
    juce_add_plugin(${target}
        COMPANY_NAME            "Hum and Chord"
        PLUGIN_MANUFACTURER_CODE Hmch
        PLUGIN_CODE             ${code}
        FORMATS                 ${formats}
        PRODUCT_NAME            "Hum and Chord"
        BUNDLE_ID               "com.humandchord.${target}"
        IS_SYNTH                ${is_synth}
        IS_MIDI_EFFECT          ${is_midi_effect}
        NEEDS_MIDI_INPUT        TRUE
        NEEDS_MIDI_OUTPUT       TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        NEEDS_WEB_BROWSER       ${HC_WEB}
        VST3_CATEGORIES         Instrument Tools
        AU_MAIN_TYPE            ${ARGN}
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            HumAndChordWeb
            juce::juce_audio_utils
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

if(APPLE)
    # Logic: an Audio Unit MIDI effect that sits in the track's MIDI FX slot.
    hc_add_plugin(HumAndChordMidiFX Hmfx "AU" TRUE FALSE kAudioUnitType_MIDIProcessor)
endif()

# Reaper and other hosts: a VST3 that takes MIDI in and sends MIDI out,
# plus a standalone app.
hc_add_plugin(HumAndChord Hmvs "VST3;Standalone" FALSE TRUE kAudioUnitType_MusicDevice)
