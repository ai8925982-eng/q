#pragma once
#include <juce_core/juce_core.h>
#include "../common/ipc.h"

// Talks to Bridge32Host.exe through shared memory + named events.
// Everything except runBlock() must be called from the message thread.
class BridgeClient
{
public:
    BridgeClient() = default;
    ~BridgeClient() { shutdown(); }

    bool start (const juce::File& vst2Dll, const juce::File& hostExe, juce::String& error);
    void shutdown();

    bool ok() const { return shm != nullptr && shm->ready == 1; }

    // blocking command to the 32-bit process (message thread only)
    bool command (int cmd, int a0 = 0, int a1 = 0, int a2 = 0, int timeoutMs = 4000);

    // audio thread: process shm->frames samples, returns false on timeout
    bool runBlock (int timeoutMs = 250);

    // editor embedding
    bool openEditor (void* parentHwnd, int x, int y, int& w, int& h);
    bool pollEditorResize (int& w, int& h);

    bridge::Shared* shm = nullptr;

private:
    juce::String sessionId;
    void* hMap = nullptr;
    void* evGo = nullptr;
    void* evDone = nullptr;
    void* evCmd = nullptr;
    void* evCmdDone = nullptr;
    void* hProcess = nullptr;
    juce::CriticalSection cmdLock;

    JUCE_DECLARE_NON_COPYABLE (BridgeClient)
};
