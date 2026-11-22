#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "BridgeClient.h"
#include <atomic>
#include <string>

static std::atomic<int> gCounter { 0 };

bool BridgeClient::start (const juce::File& dll, const juce::File& hostExe, juce::String& err)
{
    if (! hostExe.existsAsFile()) { err = "Bridge32Host.exe not found next to the plugin"; return false; }
    if (! dll.existsAsFile())     { err = "Plugin file not found"; return false; }

    sessionId = juce::String ((int) GetCurrentProcessId()) + "_" + juce::String (++gCounter);
    auto name = [this] (const char* sfx) { return juce::String ("VSTBridge_") + sessionId + sfx; };

    hMap = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                               (DWORD) sizeof (bridge::Shared), name ("").toWideCharPointer());
    if (hMap == nullptr) { err = "CreateFileMapping failed"; return false; }

    shm = (bridge::Shared*) MapViewOfFile (hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (bridge::Shared));
    if (shm == nullptr) { err = "MapViewOfFile failed"; shutdown(); return false; }
    ZeroMemory ((void*) shm, sizeof (bridge::Shared));

    evGo      = CreateEventW (nullptr, FALSE, FALSE, name ("_go").toWideCharPointer());
    evDone    = CreateEventW (nullptr, FALSE, FALSE, name ("_done").toWideCharPointer());
    evCmd     = CreateEventW (nullptr, FALSE, FALSE, name ("_cmd").toWideCharPointer());
    evCmdDone = CreateEventW (nullptr, FALSE, FALSE, name ("_cmdDone").toWideCharPointer());
    if (! evGo || ! evDone || ! evCmd || ! evCmdDone) { err = "CreateEvent failed"; shutdown(); return false; }

    const juce::String cmdline = "\"" + hostExe.getFullPathName() + "\" " + sessionId
                               + " \"" + dll.getFullPathName() + "\" " + juce::String ((int) GetCurrentProcessId());
    std::wstring wcmd = cmdline.toWideCharPointer();
    const juce::String workDir = dll.getParentDirectory().getFullPathName();

    STARTUPINFOW si {};
    si.cb = sizeof (si);
    PROCESS_INFORMATION pi {};
    if (! CreateProcessW (nullptr, wcmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                          workDir.toWideCharPointer(), &si, &pi))
    {
        err = "Could not start Bridge32Host.exe";
        shutdown();
        return false;
    }
    CloseHandle (pi.hThread);
    hProcess = pi.hProcess;

    // wait (max 10 s) until the host has loaded the plugin
    for (int i = 0; i < 500; ++i)
    {
        if (shm->ready != 0) break;
        if (WaitForSingleObject (hProcess, 20) == WAIT_OBJECT_0)
        {
            err = "32-bit host exited while loading the plugin";
            shutdown();
            return false;
        }
    }

    if (shm->ready == 1) return true;

    err = shm->ready == 2 ? juce::String (shm->str) : juce::String ("Timed out while loading the plugin");
    shutdown();
    return false;
}

void BridgeClient::shutdown()
{
    if (shm != nullptr && hProcess != nullptr)
    {
        if (shm->ready == 1) command (bridge::CMD_QUIT, 0, 0, 0, 500);
        if (WaitForSingleObject (hProcess, 1000) != WAIT_OBJECT_0)
            TerminateProcess (hProcess, 0);
    }

    auto closeH = [] (void*& h) { if (h != nullptr) { CloseHandle (h); h = nullptr; } };
    closeH (hProcess); closeH (evGo); closeH (evDone); closeH (evCmd); closeH (evCmdDone);

    if (shm != nullptr) { UnmapViewOfFile ((void*) shm); shm = nullptr; }
    closeH (hMap);
}

bool BridgeClient::command (int cmd, int a0, int a1, int a2, int timeoutMs)
{
    if (shm == nullptr) return false;
    const juce::ScopedLock sl (cmdLock);
    shm->a0 = a0; shm->a1 = a1; shm->a2 = a2;
    shm->cmd = cmd;
    SetEvent (evCmd);
    return WaitForSingleObject (evCmdDone, (DWORD) timeoutMs) == WAIT_OBJECT_0;
}

bool BridgeClient::runBlock (int timeoutMs)
{
    ResetEvent (evDone);
    SetEvent (evGo);
    return WaitForSingleObject (evDone, (DWORD) timeoutMs) == WAIT_OBJECT_0;
}

bool BridgeClient::openEditor (void* parentHwnd, int x, int y, int& w, int& h)
{
    HWND p = (HWND) parentHwnd;
    const LONG_PTR style = GetWindowLongPtrW (p, GWL_STYLE);
    SetWindowLongPtrW (p, GWL_STYLE, style | WS_CLIPCHILDREN);   // stop the parent repainting over the plugin GUI

    if (! command (bridge::CMD_OPEN_EDITOR, x, y, (int32_t) (intptr_t) p, 8000)) return false;
    w = shm->editorW;
    h = shm->editorH;
    shm->editorDirty = 0;
    return w > 0 && h > 0;
}

bool BridgeClient::pollEditorResize (int& w, int& h)
{
    if (shm == nullptr || ! shm->editorDirty) return false;
    w = shm->editorW;
    h = shm->editorH;
    shm->editorDirty = 0;
    return w > 0 && h > 0;
}
