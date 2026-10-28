cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS supported")
project(HumAndChord VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set HC_CHECK_ONLY=ON to compile-check the sources on a machine without the
# Linux desktop libraries (no web view is linked).
option(HC_CHECK_ONLY "Only compile the plugin's own sources" OFF)

# Set HC_COPY_PLUGINS=ON to install each plugin into ~/Library/Audio/Plug-Ins after it builds.
option(HC_COPY_PLUGINS "Copy plugins to the plug-in folders after building" OFF)

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

# hc_add_plugin(<target> NAME <product name> CODE <4 chars> FORMATS <list>
#               MIDI_EFFECT <bool> AU_TYPE <AU main type>)
function(hc_add_plugin target)
    cmake_parse_arguments(HC "" "NAME;CODE;MIDI_EFFECT;AU_TYPE" "FORMATS" ${ARGN})

    juce_add_plugin(${target}
        COMPANY_NAME             "Hum and Chord"
        PLUGIN_MANUFACTURER_CODE Hmch
        PLUGIN_CODE              ${HC_CODE}
        FORMATS                  ${HC_FORMATS}
        PRODUCT_NAME             "${HC_NAME}"
        BUNDLE_ID                "com.humandchord.${target}"
        IS_SYNTH                 FALSE
        IS_MIDI_EFFECT           ${HC_MIDI_EFFECT}
        NEEDS_MIDI_INPUT         TRUE
        NEEDS_MIDI_OUTPUT        TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        NEEDS_WEB_BROWSER        ${HC_WEB}
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "Hum and Chord listens to your singing or playing to suggest chords."
        VST3_CATEGORIES          Fx Analyzer
        AU_MAIN_TYPE             ${HC_AU_TYPE}
        COPY_PLUGIN_AFTER_BUILD  ${HC_COPY_PLUGINS})

    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/AudioAnalyser.cpp)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            HumAndChordWeb
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_gui_extra
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

if(APPLE)
    # Logic, software instrument tracks: goes in the MIDI FX slot.
    hc_add_plugin(HumAndChordMidiFX NAME "Hum and Chord" CODE Hmfx
                  FORMATS AU MIDI_EFFECT TRUE AU_TYPE kAudioUnitType_MIDIProcessor)

    # Logic, audio tracks: goes in an Audio FX slot and listens to the audio.
    hc_add_plugin(HumAndChordAudioFX NAME "Hum and Chord Audio" CODE Hmau
                  FORMATS AU MIDI_EFFECT FALSE AU_TYPE kAudioUnitType_Effect)
endif()

# Reaper (any track, audio or MIDI) and the standalone app.
hc_add_plugin(HumAndChord NAME "Hum and Chord" CODE Hmvs
              FORMATS VST3 Standalone MIDI_EFFECT FALSE AU_TYPE kAudioUnitType_Effect)
