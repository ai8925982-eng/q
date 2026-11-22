#include "PluginProcessor.h"
#include "PluginEditor.h"

BridgeProcessor::BridgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    for (int i = 0; i < bridge::kMaxParams; ++i)
    {
        auto* p = new BridgeParam (i);
        params.push_back (p);
        addParameter (p);   // ownership goes to the processor
    }
    startTimerHz (30);
}

BridgeProcessor::~BridgeProcessor()
{
    stopTimer();
    client.reset();
}

bool BridgeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void BridgeProcessor::prepareToPlay (double sr, int bs)
{
    sampleRate = sr;
    blockSize = juce::jmin (bs, bridge::kMaxFrames);
    prepared = true;

    if (client != nullptr && client->ok())
    {
        client->command (bridge::CMD_SETUP, (int) sampleRate, blockSize);
        client->command (bridge::CMD_MAINS, 1);
    }
}

void BridgeProcessor::releaseResources()
{
    prepared = false;
    if (client != nullptr && client->ok())
        client->command (bridge::CMD_MAINS, 0);
}

bool BridgeProcessor::loadPlugin (const juce::File& dll, juce::String& err)
{
    const auto hostExe = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                             .getSiblingFile ("Bridge32Host.exe");

    auto nc = std::make_unique<BridgeClient>();
    if (! nc->start (dll, hostExe, err))
        return false;

    auto* s = nc->shm;
    const int n = juce::jmin (s->numParams, bridge::kMaxParams);

    for (int i = 0; i < bridge::kMaxParams; ++i)
    {
        if (i < n && nc->command (bridge::CMD_PARAM_NAME, i))
            params[(size_t) i]->setBridgeName (juce::String::fromUTF8 (s->str));
        else
            params[(size_t) i]->setBridgeName ("-");
    }

    if (prepared)
    {
        nc->command (bridge::CMD_SETUP, (int) sampleRate, blockSize);
        nc->command (bridge::CMD_MAINS, 1);
    }

    {
        const juce::SpinLock::ScopedLockType sl (clientLock);
        std::swap (client, nc);          // audio thread only ever try-locks
    }
    nc.reset();                          // old host process is shut down outside the lock

    pluginFile = dll;
    pluginName = juce::String (client->shm->name);
    if (pluginName.isEmpty()) pluginName = dll.getFileNameWithoutExtension();

    for (int i = 0; i < n; ++i)
    {
        lastSeenOut[i] = client->shm->paramsOut[i];
        params[(size_t) i]->setValueNotifyingHost (lastSeenOut[i]);
    }

    setLatencySamples (client->shm->latency);
    updateHostDisplay (ChangeDetails().withParameterInfoChanged (true));
    ++generation;
    return true;
}

void BridgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const juce::SpinLock::ScopedTryLockType lk (clientLock);
    auto* c = client.get();
    const int chans = buffer.getNumChannels();

    if (! lk.isLocked() || c == nullptr || ! c->ok() || chans == 0)
    {
        buffer.clear();
        return;
    }

    auto* s = c->shm;
    const int total = buffer.getNumSamples();

    for (int i = 0; i < s->numParams; ++i)
        s->paramsIn[i] = params[(size_t) i]->getValue();

    for (int pos = 0; pos < total; pos += bridge::kMaxFrames)
    {
        const int n = juce::jmin (bridge::kMaxFrames, total - pos);

        for (int ch = 0; ch < bridge::kMaxChannels; ++ch)
            std::memcpy (s->audioIn[ch],
                         buffer.getReadPointer (juce::jmin (ch, chans - 1)) + pos,
                         sizeof (float) * (size_t) n);

        int mc = 0;
        for (const auto meta : midi)
        {
            const int t = meta.samplePosition;
            if (t < pos || t >= pos + n || meta.numBytes < 1 || meta.numBytes > 3 || mc >= bridge::kMaxMidi)
                continue;

            const auto* d = meta.data;
            s->midi[mc].delta = t - pos;
            s->midi[mc].b0 = d[0];
            s->midi[mc].b1 = meta.numBytes > 1 ? d[1] : 0;
            s->midi[mc].b2 = meta.numBytes > 2 ? d[2] : 0;
            s->midi[mc].pad = 0;
            ++mc;
        }

        s->frames = n;
        s->midiCount = mc;

        if (! c->runBlock())
        {
            for (int ch = 0; ch < chans; ++ch)
                buffer.clear (ch, pos, n);
            continue;
        }

        for (int ch = 0; ch < chans; ++ch)
            std::memcpy (buffer.getWritePointer (ch) + pos,
                         s->audioOut[juce::jmin (ch, bridge::kMaxChannels - 1)],
                         sizeof (float) * (size_t) n);
    }
}

// Pull parameter changes made inside the plugin's own GUI back into the DAW.
void BridgeProcessor::timerCallback()
{
    if (client == nullptr || ! client->ok()) return;

    auto* s = client->shm;
    for (int i = 0; i < s->numParams; ++i)
    {
        const float o = s->paramsOut[i];
        if (o == lastSeenOut[i]) continue;          // only react when the plugin value really moved

        lastSeenOut[i] = o;
        if (std::abs (o - params[(size_t) i]->getValue()) > 1.0e-4f)
            params[(size_t) i]->setValueNotifyingHost (o);
    }
}

juce::AudioProcessorEditor* BridgeProcessor::createEditor()
{
    return new BridgeEditor (*this);
}

void BridgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml ("VSTBRIDGE");
    xml.setAttribute ("dll", pluginFile.getFullPathName());
    for (int i = 0; i < bridge::kMaxParams; ++i)
        xml.setAttribute ("p" + juce::String (i), (double) params[(size_t) i]->getValue());
    copyXmlToBinary (xml, dest);
}

void BridgeProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("VSTBRIDGE")) return;

    const juce::String path = xml->getStringAttribute ("dll");
    std::vector<float> values;
    for (int i = 0; i < bridge::kMaxParams; ++i)
        values.push_back ((float) xml->getDoubleAttribute ("p" + juce::String (i), 0.0));

    if (path.isEmpty()) return;

    juce::WeakReference<BridgeProcessor> self (this);
    juce::MessageManager::callAsync ([self, path, values]
    {
        if (self == nullptr) return;

        juce::String err;
        if (! self->loadPlugin (juce::File (path), err)) return;

        const int n = self->client->shm->numParams;
        for (int i = 0; i < n; ++i)
        {
            self->params[(size_t) i]->setValueNotifyingHost (values[(size_t) i]);
            self->lastSeenOut[i] = values[(size_t) i];
        }
    });
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BridgeProcessor();
}
