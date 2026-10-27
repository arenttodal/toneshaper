juce_add_plugin(ToneShaper
    PRODUCT_NAME "ToneShaper"
    COMPANY_NAME "Arent Todal"
    COMPANY_WEBSITE "https://github.com/arenttodal/toneshaper"
    BUNDLE_ID com.arenttodal.toneshaper
    PLUGIN_MANUFACTURER_CODE Atdl
    PLUGIN_CODE Tnsh
    DESCRIPTION "Adaptive tone shaper"
    FORMATS AU VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx EQ Dynamics
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ToneShaper processes audio from your input device.")

target_sources(ToneShaper PRIVATE
    Parameters.cpp
    Presets.cpp
    PluginProcessor.cpp
    PluginEditor.cpp
    SpectrumDisplay.cpp
    LookAndFeel.cpp)

target_compile_definitions(ToneShaper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(ToneShaper
    PRIVATE
        toneshaper_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
