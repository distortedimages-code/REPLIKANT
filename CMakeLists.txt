cmake_minimum_required(VERSION 3.22)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
endif()

project(REPLIKANT VERSION 0.4.5 LANGUAGES C CXX)

# GitHub repository whose releases the plug-in checks for updates (public releases).
set(RK_UPDATE_REPO "distortedimages-code/REPLIKANT" CACHE STRING "owner/repo checked for updates (empty = no update check)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# JUCE: a local checkout with -DJUCE_DIR=/path/to/JUCE, otherwise JUCE 8.0.4 (same as OXIde and RackAttack Editor).
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

#==============================================================================
# The DSP core, no JUCE inside: interpolation, formants, loops, pitch, captures, WAV chunks, and the player's DSP.
add_library(rk_core STATIC
    Source/core/RkCore.cpp
    Source/core/Sinc.cpp
    Source/core/Analysis.cpp
    Source/core/LoopFinder.cpp
    Source/core/Formant.cpp
    Source/core/Transpose.cpp
    Source/core/Plan.cpp
    Source/core/Wav.cpp
    Source/core/Capture.cpp
    # the player's settings (one table)
    Source/engine/Sound.cpp
    # the player's DSP: oversampling, analog filters, drives, modulators, effects
    Source/dsp/Oversampler.cpp
    Source/dsp/VAFilter.cpp
    Source/dsp/Cube.cpp
    Source/dsp/Drive.cpp
    Source/dsp/Modulators.cpp
    Source/dsp/Space.cpp
    Source/dsp/Echo.cpp
    Source/dsp/Comp.cpp
    Source/dsp/Effects.cpp
    Source/dsp/Cycles.cpp
    Source/dsp/Polyphase.cpp
    Source/dsp/Orbite.cpp
    # RADIO: the receivers' feed and history, the listening, the takes and their pads; the decoder (minimp3, CC0)
    Source/radio/RadioCore.cpp
    Source/radio/RadioDec.cpp
    Source/radio/RadioSlicer.cpp
    # DRUM: eight tracks, their voices and their sequencer
    Source/drum/DrumCore.cpp)
target_compile_features(rk_core PUBLIC cxx_std_17)
# Signalsmith Stretch and Linear (MIT), vendored in ThirdParty
target_include_directories(rk_core PUBLIC ThirdParty)
set_target_properties(rk_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    target_compile_definitions(rk_core PUBLIC SIGNALSMITH_USE_ACCELERATE)
    target_link_libraries(rk_core PUBLIC "-framework Accelerate")
endif()
if(NOT MSVC)
    # the DSP is heavy: optimised even in debug builds
    target_compile_options(rk_core PRIVATE -O3)
endif()

#==============================================================================
# REPLIKANT: AU (music effect: audio in from the synth, MIDI in to play), VST3 and the app (standalone)
juce_add_plugin(REPLIKANT
    COMPANY_NAME "distortedimages"
    PRODUCT_NAME "REPLIK8"
    BUNDLE_ID com.distortedimages.replikant
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Dimg
    PLUGIN_CODE Rplk
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "REPLIK8 enregistre ton synthé par l'entrée de ta carte son. Rien n'est envoyé nulle part."
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    VST3_CATEGORIES Fx Sampler Instrument
    FORMATS AU VST3 Standalone)

juce_generate_juce_header(REPLIKANT)

set(RK_APP_SOURCES
    Source/plugin/PluginProcessor.cpp
    Source/plugin/Updater.cpp
    Source/plugin/MidiClock.cpp
    Source/engine/Instrument.cpp
    Source/engine/Recorder.cpp
    Source/engine/Worker.cpp
    Source/engine/Player.cpp
    Source/engine/TestSynth.cpp
    Source/radio/RadioNet.cpp
    Source/ui/Look.cpp
    Source/ui/Pictos.cpp
    Source/ui/Controls.cpp
    Source/ui/UpdatePanel.cpp
    Source/ui/RadioUi.cpp
    Source/ui/DrumUi.cpp
    Source/ui/Machine.cpp)

# Fonts (SIL Open Font License, see Resources/fonts)
juce_add_binary_data(RkFonts SOURCES
    Resources/fonts/SpaceGrotesk-Light.ttf
    Resources/fonts/SpaceGrotesk-Medium.ttf
    Resources/fonts/SpaceGrotesk-Bold.ttf)
set_target_properties(RkFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

# This version's notes, for the window that tells what it brought the first time it opens
juce_add_binary_data(RkNotes HEADER_NAME RkNotesData.h NAMESPACE RkNotesData SOURCES RELEASE_NOTES.md)
set_target_properties(RkNotes PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(RK_DEFINITIONS
    RK_VERSION="${PROJECT_VERSION}"
    RK_UPDATE_REPO="${RK_UPDATE_REPO}")

target_sources(REPLIKANT PRIVATE ${RK_APP_SOURCES})

target_compile_definitions(REPLIKANT PUBLIC
    ${RK_DEFINITIONS}
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(REPLIKANT
    PRIVATE
        rk_core
        RkFonts
        RkNotes
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_cryptography
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
option(RK_BUILD_TESTS "Build the tests" ON)

if(RK_BUILD_TESTS)
    enable_testing()

    # the DSP core on its own
    add_executable(CoreTests Tests/CoreTests.cpp)
    target_link_libraries(CoreTests PRIVATE rk_core)
    add_test(NAME CoreTests COMMAND CoreTests)

    # the player's DSP: filters, drives, effects, modulators
    add_executable(DspTests Tests/DspTests.cpp)
    target_link_libraries(DspTests PRIVATE rk_core)
    add_test(NAME DspTests COMMAND DspTests)

    # RADIO without the network: the decoder, the feed and its clock, the listening and its air, the takes, the slicer, the pads
    add_executable(RadioTests Tests/RadioTests.cpp)
    target_link_libraries(RadioTests PRIVATE rk_core)
    target_compile_definitions(RadioTests PRIVATE RK_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/radio")
    add_test(NAME RadioTests COMMAND RadioTests)

    # DRUM: the sequencer to the sample, the patterns, the sources, the voices
    add_executable(DrumTests Tests/DrumTests.cpp)
    target_link_libraries(DrumTests PRIVATE rk_core)
    add_test(NAME DrumTests COMMAND DrumTests)

    # a whole session on the test synth: recorder, worker, instrument, player, saving, exports
    juce_add_console_app(EngineTests PRODUCT_NAME "EngineTests")
    juce_generate_juce_header(EngineTests)
    target_sources(EngineTests PRIVATE Tests/EngineTests.cpp
        Source/engine/Instrument.cpp Source/engine/Recorder.cpp Source/engine/Worker.cpp
        Source/engine/Player.cpp Source/engine/TestSynth.cpp)
    target_compile_definitions(EngineTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(EngineTests PRIVATE
        rk_core
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME EngineTests COMMAND EngineTests)
    set_tests_properties(EngineTests PROPERTIES ENVIRONMENT "HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home")

    # the player on made-up samples: anti-aliasing, ALIASING, phase modulation, voices, filters, mods, FX mode, CPU
    juce_add_console_app(PlayerTests PRODUCT_NAME "PlayerTests")
    juce_generate_juce_header(PlayerTests)
    target_sources(PlayerTests PRIVATE Tests/PlayerTests.cpp Source/engine/Instrument.cpp Source/engine/Player.cpp
        Source/plugin/MidiClock.cpp)
    target_compile_definitions(PlayerTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(PlayerTests PRIVATE
        rk_core
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME PlayerTests COMMAND PlayerTests)

    # RADIO on the network: the receivers against local stand-ins (Tests/radio/radio_mock.py, python3)
    juce_add_console_app(RadioNetTests PRODUCT_NAME "RadioNetTests")
    juce_generate_juce_header(RadioNetTests)
    target_sources(RadioNetTests PRIVATE Tests/RadioNetTests.cpp Source/radio/RadioNet.cpp)
    target_compile_definitions(RadioNetTests PRIVATE ${RK_DEFINITIONS} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        RK_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Tests/radio")
    target_link_libraries(RadioNetTests PRIVATE
        rk_core
        juce::juce_core
        juce::juce_events
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME RadioNetTests COMMAND RadioNetTests)

    # the look: fonts and text layout (a tooltip with characters the pixel fonts lack must not recurse)
    juce_add_console_app(UiTests PRODUCT_NAME "UiTests")
    juce_generate_juce_header(UiTests)
    target_sources(UiTests PRIVATE Tests/UiTests.cpp Source/ui/Look.cpp)
    target_compile_definitions(UiTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(UiTests PRIVATE
        RkFonts
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME UiTests COMMAND UiTests)

    # Update checker: version compare, GitHub answer parsing, checksum, bundle replacement
    juce_add_console_app(UpdaterTests PRODUCT_NAME "UpdaterTests")
    juce_generate_juce_header(UpdaterTests)
    target_sources(UpdaterTests PRIVATE Tests/UpdaterTests.cpp Source/plugin/Updater.cpp)
    target_compile_definitions(UpdaterTests PRIVATE ${RK_DEFINITIONS} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(UpdaterTests PRIVATE
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_cryptography
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_test(NAME UpdaterTests COMMAND UpdaterTests)

    # The processor as a host runs it (on GitHub's Macs too)
    juce_add_console_app(ProcessorTests PRODUCT_NAME "ProcessorTests")
    juce_generate_juce_header(ProcessorTests)
    target_sources(ProcessorTests PRIVATE Tests/ProcessorTests.cpp ${RK_APP_SOURCES})
    target_compile_definitions(ProcessorTests PRIVATE ${RK_DEFINITIONS} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="REPLIK8" JucePlugin_Build_Standalone=0 RK_SNAPSHOT=1)
    target_link_libraries(ProcessorTests PRIVATE
        rk_core
        RkFonts
        RkNotes
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_cryptography
        juce::juce_recommended_config_flags)
    add_test(NAME ProcessorTests COMMAND ProcessorTests)

    # Not a test: the window drawn to a PNG (to look at it without a Mac)
    juce_add_console_app(Snapshot PRODUCT_NAME "Snapshot")
    juce_generate_juce_header(Snapshot)
    target_sources(Snapshot PRIVATE Tests/Snapshot.cpp ${RK_APP_SOURCES})
    target_compile_definitions(Snapshot PRIVATE ${RK_DEFINITIONS} JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="REPLIK8" JucePlugin_Build_Standalone=0 RK_SNAPSHOT=1)
    target_link_libraries(Snapshot PRIVATE
        rk_core
        RkFonts
        RkNotes
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_cryptography
        juce::juce_recommended_config_flags)
    set_target_properties(Snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
