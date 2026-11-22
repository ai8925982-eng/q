if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The VST3 wrapper must be built as 64-bit: cmake -A x64")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(VSTBridge
    PRODUCT_NAME "VST Bridge 32-64"
    COMPANY_NAME "VSTBridge"
    PLUGIN_MANUFACTURER_CODE Brdg
    PLUGIN_CODE Br32
    FORMATS VST3
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(VSTBridge PRIVATE
    BridgeClient.cpp
    PluginProcessor.cpp
    PluginEditor.cpp)

target_compile_definitions(VSTBridge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    NOMINMAX)

target_link_libraries(VSTBridge
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Put Bridge32Host.exe next to the VST3 binary so the plugin can find it at runtime.
set(BRIDGE32_EXE "" CACHE FILEPATH "Path to the 32-bit Bridge32Host.exe")
if(BRIDGE32_EXE)
    file(TO_CMAKE_PATH "${BRIDGE32_EXE}" BRIDGE32_EXE)
    add_custom_command(TARGET VSTBridge_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${BRIDGE32_EXE}" "$<TARGET_FILE_DIR:VSTBridge_VST3>")
else()
    message(WARNING "BRIDGE32_EXE not set - copy Bridge32Host.exe next to the .vst3 binary manually.")
endif()
