# =============================================================================
#  CHOPPAGE — Twisted Genetics break chopper: one break at a time, S950 skin
#  CMake build for JUCE 8 (AU + VST3 + Standalone on macOS; VST3 + Standalone elsewhere)
# =============================================================================
cmake_minimum_required(VERSION 3.22)

project(Choppage VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(CHOP_BUILD_TESTS        "Build the headless test runner"                       ON)
option(CHOP_COPY_AFTER_BUILD   "Copy plugins into ~/Library/Audio/Plug-Ins after build" OFF)
option(CHOP_MINIMAL_LINUX_DEPS "Linux only: disable optional X11/ALSA features"       OFF)
set(CHOP_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE)")

# -----------------------------------------------------------------------------
#  JUCE
# -----------------------------------------------------------------------------
if(CHOP_JUCE_PATH)
    add_subdirectory(${CHOP_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.10
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# -----------------------------------------------------------------------------
#  Plugin
# -----------------------------------------------------------------------------
set(CHOP_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND CHOP_FORMATS AU)
endif()

juce_add_plugin(Choppage
    PRODUCT_NAME                "CHOPPAGE"
    COMPANY_NAME                "Twisted Genetics"
    BUNDLE_ID                   "com.twistedgenetics.choppage"
    PLUGIN_MANUFACTURER_CODE    Twgn
    PLUGIN_CODE                 Chpg
    FORMATS                     ${CHOP_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Instrument Sampler Drum
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    MICROPHONE_PERMISSION_ENABLED FALSE
    COPY_PLUGIN_AFTER_BUILD     ${CHOP_COPY_AFTER_BUILD})

juce_generate_juce_header(Choppage)

include(cmake/Sources.cmake)
target_sources(Choppage PRIVATE ${CHOP_PLUGIN_SOURCES})
target_include_directories(Choppage PRIVATE Source)

set(CHOP_JUCE_DEFINES
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

if(CHOP_MINIMAL_LINUX_DEPS)
    list(APPEND CHOP_JUCE_DEFINES
        JUCE_ALSA=0 JUCE_JACK=0
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XSHM=0
        JUCE_USE_XRENDER=0 JUCE_USE_XCURSOR=0)
endif()

target_compile_definitions(Choppage PUBLIC ${CHOP_JUCE_DEFINES})

target_link_libraries(Choppage
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# -----------------------------------------------------------------------------
#  Headless tests (no DAW): engine, patterns, FX, state, UI snapshot to PNG
# -----------------------------------------------------------------------------
if(CHOP_BUILD_TESTS)
    juce_add_console_app(ChoppageTests PRODUCT_NAME "ChoppageTests")
    target_sources(ChoppageTests PRIVATE Tests/TestMain.cpp ${CHOP_PLUGIN_SOURCES})
    target_include_directories(ChoppageTests PRIVATE Source)
    target_compile_definitions(ChoppageTests PRIVATE
        ${CHOP_JUCE_DEFINES}
        JucePlugin_Name="CHOPPAGE"
        JucePlugin_IsSynth=1
        JucePlugin_WantsMidiInput=1
        JucePlugin_ProducesMidiOutput=0
        JucePlugin_IsMidiEffect=0
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JUCE_MODAL_LOOPS_PERMITTED=1
        CHOP_TEST_BUILD=1)
    target_link_libraries(ChoppageTests PRIVATE
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    juce_generate_juce_header(ChoppageTests)

    # Loads the BUILT VST3 (and the installed AU on macOS) the way a DAW does.
    juce_add_console_app(ChoppageHostTest PRODUCT_NAME "ChoppageHostTest")
    target_sources(ChoppageHostTest PRIVATE Tests/HostTest.cpp)
    target_compile_definitions(ChoppageHostTest PRIVATE
        ${CHOP_JUCE_DEFINES} JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1
        $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)
    target_link_libraries(ChoppageHostTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_processors
        juce::juce_recommended_config_flags)
    juce_generate_juce_header(ChoppageHostTest)
    add_dependencies(ChoppageHostTest Choppage_VST3)
endif()
