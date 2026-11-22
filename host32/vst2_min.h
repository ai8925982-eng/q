#pragma once
// Minimal, independently written description of the VST 2.x plugin ABI
// (struct layout + the few opcodes this bridge needs).
// NOTE: the official Steinberg VST2 SDK can no longer be redistributed; this header
// intentionally contains only what is needed for binary compatibility.
#include <cstdint>

#define VSTCALL __cdecl

namespace vst2
{
constexpr int32_t kMagic = 0x56737450; // 'VstP'

// host -> plugin opcodes
enum : int32_t
{
    effOpen = 0, effClose = 1,
    effGetParamName = 8,
    effSetSampleRate = 10, effSetBlockSize = 11, effMainsChanged = 12,
    effEditGetRect = 13, effEditOpen = 14, effEditClose = 15, effEditIdle = 19,
    effProcessEvents = 25,
    effGetEffectName = 45,
    effStartProcess = 71, effStopProcess = 72
};

// plugin -> host opcodes
enum : int32_t
{
    audioMasterAutomate = 0, audioMasterVersion = 1, audioMasterCurrentId = 2, audioMasterIdle = 3,
    audioMasterGetTime = 7, audioMasterSizeWindow = 15, audioMasterGetSampleRate = 16,
    audioMasterGetBlockSize = 17, audioMasterGetVendorString = 32, audioMasterGetProductString = 33,
    audioMasterGetVendorVersion = 34, audioMasterCanDo = 37, audioMasterUpdateDisplay = 42
};

enum : int32_t
{
    effFlagsHasEditor = 1 << 0,
    effFlagsCanReplacing = 1 << 4,
    effFlagsIsSynth = 1 << 8
};

struct AEffect;
typedef intptr_t (VSTCALL *HostCallback)(AEffect*, int32_t, int32_t, intptr_t, void*, float);

struct AEffect
{
    int32_t magic;
    intptr_t (VSTCALL *dispatcher)(AEffect*, int32_t, int32_t, intptr_t, void*, float);
    void (VSTCALL *process)(AEffect*, float**, float**, int32_t);
    void (VSTCALL *setParameter)(AEffect*, int32_t, float);
    float (VSTCALL *getParameter)(AEffect*, int32_t);
    int32_t numPrograms, numParams, numInputs, numOutputs, flags;
    intptr_t resvd1, resvd2;
    int32_t initialDelay, realQualities, offQualities;
    float ioRatio;
    void* object;
    void* user;
    int32_t uniqueID, version;
    void (VSTCALL *processReplacing)(AEffect*, float**, float**, int32_t);
    void (VSTCALL *processDoubleReplacing)(AEffect*, double**, double**, int32_t);
    char future[56];
};

typedef AEffect* (VSTCALL *PluginEntry)(HostCallback);

struct ERect { int16_t top, left, bottom, right; };

constexpr int32_t kMidiEventType = 1;
struct MidiEvent
{
    int32_t type, byteSize, deltaFrames, flags, noteLength, noteOffset;
    char midiData[4];
    char detune, noteOffVelocity, reserved1, reserved2;
};

template <int N>
struct EventList
{
    int32_t numEvents;
    intptr_t reserved;
    void* events[N];
};
} // namespace vst2
