#pragma once
// Shared-memory layout between the 64-bit VST3 (client) and Bridge32Host.exe (32-bit server).
// Every member is 4 bytes wide, so the layout is identical in 32-bit and 64-bit builds.
#include <cstdint>

namespace bridge
{
constexpr int kMaxParams   = 128;
constexpr int kMaxChannels = 2;      // v1: stereo in / stereo out
constexpr int kMaxFrames   = 8192;
constexpr int kMaxMidi     = 256;
constexpr int kStrLen      = 64;

enum Cmd : int32_t
{
    CMD_NONE = 0,
    CMD_SETUP,         // a0 = sampleRate, a1 = blockSize
    CMD_MAINS,         // a0 = 1 on / 0 off
    CMD_OPEN_EDITOR,   // a0 = x, a1 = y, a2 = parent HWND (32-bit significant part)
    CMD_CLOSE_EDITOR,
    CMD_PARAM_NAME,    // a0 = index -> result in str
    CMD_QUIT
};

#pragma pack(push, 4)
struct MidiEv
{
    int32_t delta;
    uint8_t b0, b1, b2, pad;
};

struct Shared
{
    // ---- written by host32 after the plugin is loaded
    volatile int32_t ready;          // 0 = loading, 1 = ok, 2 = failed (message in str)
    int32_t numParams, numIn, numOut, latency, isSynth, hasEditor;
    char    name[kStrLen];

    // ---- command mailbox
    volatile int32_t cmd;
    int32_t a0, a1, a2;
    char    str[kStrLen];

    // ---- editor size reported by the plugin
    volatile int32_t editorW, editorH, editorDirty;

    // ---- one audio block
    int32_t frames, midiCount;
    MidiEv  midi[kMaxMidi];
    float   paramsIn [kMaxParams];   // 64 -> 32
    float   paramsOut[kMaxParams];   // 32 -> 64 (changes made in the plugin GUI)
    float   audioIn [kMaxChannels][kMaxFrames];
    float   audioOut[kMaxChannels][kMaxFrames];
};
#pragma pack(pop)
} // namespace bridge
