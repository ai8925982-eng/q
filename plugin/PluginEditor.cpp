#include "PluginEditor.h"

BridgeEditor::BridgeEditor (BridgeProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    addAndMakeVisible (loadButton);
    loadButton.onClick = [this] { chooseFile(); };

    status.setJustificationType (juce::Justification::centredLeft);
    status.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (status);

    setSize (520, 140);
    startTimerHz (30);
}

BridgeEditor::~BridgeEditor()
{
    stopTimer();
    closeView();
}

void BridgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22));
}

void BridgeEditor::resized()
{
    auto top = getLocalBounds().removeFromTop (kTop).reduced (6, 5);
    loadButton.setBounds (top.removeFromLeft (190));
    status.setBounds (top.reduced (8, 0));
}

void BridgeEditor::updateStatus (const juce::String& extra)
{
    auto* c = proc.getClient();
    if (extra.isNotEmpty())               status.setText (extra, juce::dontSendNotification);
    else if (c != nullptr && c->ok())     status.setText (proc.getPluginName() + "  (32-bit bridged)", juce::dontSendNotification);
    else                                  status.setText ("No plugin loaded", juce::dontSendNotification);
}

void BridgeEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Select a 32-bit VST2 plugin", juce::File(), "*.dll");

    juce::Component::SafePointer<BridgeEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (safe == nullptr || f == juce::File()) return;

        safe->updateStatus ("Loading...");
        juce::String err;
        if (! safe->proc.loadPlugin (f, err))
            safe->updateStatus ("Error: " + err);
        // on success the generation counter changes and timerCallback() re-opens the GUI
    });
}

void BridgeEditor::openView()
{
    tried = true;
    auto* c = proc.getClient();
    auto* peer = getPeer();
    if (c == nullptr || ! c->ok() || peer == nullptr) return;

    int w = 0, h = 0;
    if (c->openEditor (peer->getNativeHandle(), 0, kTop, w, h))
    {
        viewOpen = true;
        setSize (juce::jmax (w, 420), h + kTop);
        updateStatus();
    }
    else
    {
        updateStatus (c->shm->hasEditor ? "Could not open the plugin GUI" : "This plugin has no GUI");
    }
}

void BridgeEditor::closeView()
{
    if (! viewOpen) return;
    viewOpen = false;
    if (auto* c = proc.getClient())
        if (c->ok())
            c->command (bridge::CMD_CLOSE_EDITOR, 0, 0, 0, 1000);
}

void BridgeEditor::timerCallback()
{
    auto* c = proc.getClient();
    const int gen = proc.getGeneration();

    if (gen != shownGeneration)          // another plugin was loaded (or state restored)
    {
        shownGeneration = gen;
        viewOpen = false;                // the old host process is gone, its window with it
        tried = false;
        updateStatus();
    }

    if (c == nullptr || ! c->ok()) return;

    if (! viewOpen && ! tried && getPeer() != nullptr)
        openView();

    int w = 0, h = 0;
    if (viewOpen && c->pollEditorResize (w, h))
        setSize (juce::jmax (w, 420), h + kTop);
}
