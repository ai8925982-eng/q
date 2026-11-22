// Bridge32Host.exe - 32-bit process that loads a 32-bit VST2 plugin and serves it
// to the 64-bit VST3 wrapper through shared memory + named events.
//
// usage: Bridge32Host.exe <sessionId> <path-to-vst2.dll> <parentPid>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cstring>
#include <string>
#include "vst2_min.h"
#include "../common/ipc.h"

using namespace bridge;
using namespace vst2;

namespace
{
Shared*  S  = nullptr;
AEffect* fx = nullptr;
HANDLE evGo = nullptr, evDone = nullptr, evCmd = nullptr, evCmdDone = nullptr;
float gRate = 44100.f;
int   gBlock = 512;
HWND  gEditor = nullptr;
bool  gEditorOpen = false;
float lastIn[kMaxParams];
float scratchZero[kMaxFrames];
float scratchSink[kMaxFrames];

intptr_t disp (int32_t op, int32_t idx = 0, intptr_t val = 0, void* ptr = nullptr, float opt = 0.f)
{
    return fx->dispatcher (fx, op, idx, val, ptr, opt);
}

intptr_t VSTCALL hostCallback (AEffect*, int32_t op, int32_t idx, intptr_t val, void* ptr, float)
{
    switch (op)
    {
        case audioMasterVersion:          return 2400;
        case audioMasterGetSampleRate:    return (intptr_t) gRate;
        case audioMasterGetBlockSize:     return gBlock;
        case audioMasterGetVendorString:  strcpy ((char*) ptr, "VSTBridge"); return 1;
        case audioMasterGetProductString: strcpy ((char*) ptr, "VSTBridge 32to64"); return 1;
        case audioMasterGetVendorVersion: return 1000;
        case audioMasterCanDo:
        {
            const char* s = (const char*) ptr;
            return (! strcmp (s, "sizeWindow") || ! strcmp (s, "sendVstMidiEvent")
                    || ! strcmp (s, "receiveVstMidiEvent")) ? 1 : 0;
        }
        case audioMasterSizeWindow:
            if (gEditor != nullptr)
                SetWindowPos (gEditor, nullptr, 0, 0, idx, (int) val, SWP_NOMOVE | SWP_NOZORDER);
            S->editorW = idx; S->editorH = (int) val; S->editorDirty = 1;
            return 1;
        default: return 0; // automate / idle / updateDisplay / getTime ... : not needed in v1
    }
}

LRESULT CALLBACK EditorProc (HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW (h, m, w, l); }

void openEditor (HWND parent, int x, int y)
{
    if (gEditorOpen || ! (fx->flags & effFlagsHasEditor)) return;

    static bool registered = false;
    if (! registered)
    {
        WNDCLASSW wc {};
        wc.lpfnWndProc = EditorProc;
        wc.hInstance = GetModuleHandleW (nullptr);
        wc.hCursor = LoadCursorW (nullptr, (LPCWSTR) IDC_ARROW);
        wc.lpszClassName = L"VSTBridgeEditor";
        RegisterClassW (&wc);
        registered = true;
    }

    S->editorW = S->editorH = 0;
    gEditor = CreateWindowExW (0, L"VSTBridgeEditor", L"",
                               WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                               x, y, 10, 10, parent, nullptr, GetModuleHandleW (nullptr), nullptr);
    if (gEditor == nullptr) return;

    disp (effEditOpen, 0, 0, gEditor);
    ERect* r = nullptr;
    disp (effEditGetRect, 0, 0, &r);
    int w = 300, h = 200;
    if (r != nullptr) { w = r->right - r->left; h = r->bottom - r->top; }
    SetWindowPos (gEditor, nullptr, x, y, w, h, SWP_NOZORDER);
    S->editorW = w; S->editorH = h;
    gEditorOpen = true;
}

void closeEditor()
{
    if (! gEditorOpen) return;
    disp (effEditClose);
    DestroyWindow (gEditor);
    gEditor = nullptr;
    gEditorOpen = false;
}

VOID CALLBACK idleTimer (HWND, UINT, UINT_PTR, DWORD)
{
    if (gEditorOpen) disp (effEditIdle);
}

void publishInfo()
{
    S->numParams = std::min (fx->numParams, kMaxParams);
    S->numIn  = fx->numInputs;
    S->numOut = fx->numOutputs;
    S->latency = fx->initialDelay;
    S->isSynth = (fx->flags & effFlagsIsSynth) != 0;
    S->hasEditor = (fx->flags & effFlagsHasEditor) != 0;

    char buf[256] = {};
    disp (effGetEffectName, 0, 0, buf);
    strncpy (S->name, buf, kStrLen - 1);

    for (int i = 0; i < S->numParams; ++i)
        lastIn[i] = S->paramsIn[i] = S->paramsOut[i] = fx->getParameter (fx, i);

    S->ready = 1;
}

void fail (const char* msg)
{
    strncpy (S->str, msg, kStrLen - 1);
    S->ready = 2;
}

void processBlock()
{
    const int n  = std::min (std::max ((int) S->frames, 0), kMaxFrames);
    const int np = S->numParams;

    for (int i = 0; i < np; ++i)
    {
        const float v = S->paramsIn[i];
        if (v != lastIn[i]) { lastIn[i] = v; fx->setParameter (fx, i, v); }
    }

    if (S->midiCount > 0)
    {
        static MidiEvent ev[kMaxMidi];
        static EventList<kMaxMidi> list;
        const int c = std::min ((int) S->midiCount, kMaxMidi);
        for (int i = 0; i < c; ++i)
        {
            memset (&ev[i], 0, sizeof (MidiEvent));
            ev[i].type = kMidiEventType;
            ev[i].byteSize = sizeof (MidiEvent);
            ev[i].deltaFrames = S->midi[i].delta;
            ev[i].midiData[0] = (char) S->midi[i].b0;
            ev[i].midiData[1] = (char) S->midi[i].b1;
            ev[i].midiData[2] = (char) S->midi[i].b2;
            list.events[i] = &ev[i];
        }
        list.numEvents = c;
        list.reserved = 0;
        disp (effProcessEvents, 0, 0, &list);
    }

    float* ins[32];
    float* outs[32];
    const int nin  = std::min (std::max (fx->numInputs, 0), 32);
    const int nout = std::min (std::max (fx->numOutputs, 0), 32);
    for (int c = 0; c < nin;  ++c) ins[c]  = c < kMaxChannels ? S->audioIn[c]  : scratchZero;
    for (int c = 0; c < nout; ++c) outs[c] = c < kMaxChannels ? S->audioOut[c] : scratchSink;

    if (n > 0 && (fx->flags & effFlagsCanReplacing) && fx->processReplacing != nullptr && nout > 0)
    {
        fx->processReplacing (fx, ins, outs, n);
        if (nout == 1) memcpy (S->audioOut[1], S->audioOut[0], sizeof (float) * (size_t) n);
    }
    else
    {
        // no usable output: pass input through so the track is not muted
        for (int c = 0; c < kMaxChannels; ++c)
            memcpy (S->audioOut[c], S->audioIn[c], sizeof (float) * (size_t) n);
    }

    for (int i = 0; i < np; ++i)
        S->paramsOut[i] = fx->getParameter (fx, i);
}

bool handleCommand()   // returns false when we should quit
{
    bool keepRunning = true;
    switch (S->cmd)
    {
        case CMD_SETUP:
            gRate = (float) S->a0; gBlock = S->a1;
            disp (effSetSampleRate, 0, 0, nullptr, gRate);
            disp (effSetBlockSize, 0, gBlock);
            break;
        case CMD_MAINS:
            disp (effMainsChanged, 0, S->a0);
            disp (S->a0 ? effStartProcess : effStopProcess);
            break;
        case CMD_OPEN_EDITOR:  openEditor ((HWND) (INT_PTR) S->a2, S->a0, S->a1); break;
        case CMD_CLOSE_EDITOR: closeEditor(); break;
        case CMD_PARAM_NAME:
        {
            char b[256] = {};
            disp (effGetParamName, S->a0, 0, b);
            strncpy (S->str, b, kStrLen - 1);
            S->str[kStrLen - 1] = 0;
            break;
        }
        case CMD_QUIT: keepRunning = false; break;
        default: break;
    }
    S->cmd = CMD_NONE;
    return keepRunning;
}

std::wstring objName (const std::wstring& id, const wchar_t* suffix)
{
    return L"VSTBridge_" + id + suffix;
}
} // namespace

int WINAPI WinMain (HINSTANCE, HINSTANCE, LPSTR, int)
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW (GetCommandLineW(), &argc);
    if (argv == nullptr || argc < 4) return 1;

    const std::wstring id = argv[1];
    const std::wstring dllPath = argv[2];
    const DWORD parentPid = (DWORD) _wtoi (argv[3]);

    HANDLE map = OpenFileMappingW (FILE_MAP_ALL_ACCESS, FALSE, objName (id, L"").c_str());
    if (map == nullptr) return 2;
    S = (Shared*) MapViewOfFile (map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (Shared));
    if (S == nullptr) return 3;

    evGo      = OpenEventW (EVENT_ALL_ACCESS, FALSE, objName (id, L"_go").c_str());
    evDone    = OpenEventW (EVENT_ALL_ACCESS, FALSE, objName (id, L"_done").c_str());
    evCmd     = OpenEventW (EVENT_ALL_ACCESS, FALSE, objName (id, L"_cmd").c_str());
    evCmdDone = OpenEventW (EVENT_ALL_ACCESS, FALSE, objName (id, L"_cmdDone").c_str());
    if (! evGo || ! evDone || ! evCmd || ! evCmdDone) return 4;

    HANDLE hParent = OpenProcess (SYNCHRONIZE, FALSE, parentPid);

    // let the plugin find its own dependencies
    const size_t slash = dllPath.find_last_of (L"\\/");
    if (slash != std::wstring::npos)
    {
        const std::wstring dir = dllPath.substr (0, slash);
        SetDllDirectoryW (dir.c_str());
        SetCurrentDirectoryW (dir.c_str());
    }

    HMODULE mod = LoadLibraryW (dllPath.c_str());
    if (mod == nullptr) { fail ("LoadLibrary failed (is the DLL really 32-bit?)"); return 5; }

    PluginEntry entry = (PluginEntry) GetProcAddress (mod, "VSTPluginMain");
    if (entry == nullptr) entry = (PluginEntry) GetProcAddress (mod, "main");
    if (entry == nullptr) { fail ("No VST2 entry point found"); return 6; }

    fx = entry (hostCallback);
    if (fx == nullptr || fx->magic != kMagic) { fail ("Not a valid VST2 plugin"); return 7; }

    disp (effOpen);
    disp (effSetSampleRate, 0, 0, nullptr, gRate);
    disp (effSetBlockSize, 0, gBlock);
    publishInfo();

    SetTimer (nullptr, 0, 30, idleTimer);

    HANDLE handles[4] = { evGo, evCmd, hParent ? hParent : evGo, nullptr };
    const DWORD count = hParent ? 3 : 2;

    for (;;)
    {
        const DWORD r = MsgWaitForMultipleObjects (count, handles, FALSE, INFINITE, QS_ALLINPUT);

        if (r == WAIT_OBJECT_0)
        {
            processBlock();
            SetEvent (evDone);
        }
        else if (r == WAIT_OBJECT_0 + 1)
        {
            const bool keep = handleCommand();
            SetEvent (evCmdDone);
            if (! keep) break;
        }
        else if (hParent && r == WAIT_OBJECT_0 + 2)
        {
            break;   // the DAW (64-bit side) is gone
        }
        else
        {
            MSG msg;
            bool quit = false;
            while (PeekMessageW (&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT) { quit = true; break; }
                TranslateMessage (&msg);
                DispatchMessageW (&msg);
            }
            if (quit) break;
        }
    }

    closeEditor();
    disp (effClose);
    ExitProcess (0);   // do not FreeLibrary: some old plugins crash on unload
    return 0;
}
