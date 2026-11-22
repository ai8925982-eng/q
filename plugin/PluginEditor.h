#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Top bar (load button + status) and, below it, the native window of the 32-bit plugin GUI.
class BridgeEditor : public juce::AudioProcessorEditor,
                     private juce::Timer
{
public:
    explicit BridgeEditor (BridgeProcessor&);
    ~BridgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFile();
    void openView();
    void closeView();
    void updateStatus (const juce::String& extra = {});

    static constexpr int kTop = 40;

    BridgeProcessor& proc;
    juce::TextButton loadButton { "Load 32-bit VST2 (.dll)..." };
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;

    bool viewOpen = false;
    bool tried = false;
    int shownGeneration = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeEditor)
};
