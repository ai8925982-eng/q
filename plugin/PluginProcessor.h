#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <memory>
#include <vector>
#include "BridgeClient.h"

// One generic automatable parameter; its name is replaced by the bridged plugin's name.
class BridgeParam : public juce::AudioProcessorParameter
{
public:
    explicit BridgeParam (int i) : name ("P" + juce::String (i + 1)) {}

    float getValue() const override                        { return value.load(); }
    void  setValue (float v) override                      { value.store (v); }
    float getDefaultValue() const override                 { return 0.0f; }
    juce::String getName (int maxLen) const override       { const juce::ScopedLock sl (lock); return name.substring (0, maxLen); }
    juce::String getLabel() const override                 { return {}; }
    float getValueForText (const juce::String& t) const override { return t.getFloatValue(); }

    void setBridgeName (const juce::String& n)             { const juce::ScopedLock sl (lock); name = n; }

private:
    std::atomic<float> value { 0.0f };
    juce::String name;
    mutable juce::CriticalSection lock;
};

class BridgeProcessor : public juce::AudioProcessor,
                        private juce::Timer
{
public:
    BridgeProcessor();
    ~BridgeProcessor() override;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VST Bridge 32-64"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // message thread
    bool loadPlugin (const juce::File& dll, juce::String& error);
    BridgeClient* getClient() { return client.get(); }
    int getGeneration() const { return generation.load(); }
    juce::String getPluginName() const { return pluginName; }
    juce::File getPluginFile() const { return pluginFile; }

private:
    void timerCallback() override;

    std::vector<BridgeParam*> params;           // owned by the AudioProcessor
    float lastSeenOut[bridge::kMaxParams] {};

    std::unique_ptr<BridgeClient> client;
    juce::SpinLock clientLock;

    double sampleRate = 44100.0;
    int blockSize = 512;
    bool prepared = false;

    juce::File pluginFile;
    juce::String pluginName;
    std::atomic<int> generation { 0 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (BridgeProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeProcessor)
};
