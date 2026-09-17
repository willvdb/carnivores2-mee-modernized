// Audio_DLL.cpp — OpenAL Soft backend with optional legacy DirectSound DLL support
// Keeps the same public API signatures as the original DLL‑based system
// so Hunt.h / Hunt.cpp / Game.cpp need zero changes.

#include "Hunt.h"
#include "Platform/Platform.h"
#include <thread>
#include <mutex>
#include <atomic>
#include "Audio.h"
#include <unordered_map>
#include <cmath>

// ---------------------------------------------------------------------------
// OpenAL state
// ---------------------------------------------------------------------------
static ALCdevice*  alDevice = nullptr;
static ALCcontext* alContext = nullptr;
static std::unordered_map<short*, ALuint> bufferCache;
#ifdef _WIN32
static HANDLE      hAudioThread = nullptr;
static DWORD       AudioTId;
static CRITICAL_SECTION AudioCS;
static volatile BOOL g_AudioShutdown = false;

static void LockAudio() { EnterCriticalSection(&AudioCS); }
static void UnlockAudio() { LeaveCriticalSection(&AudioCS); }
#else
static std::thread audioThread;
static std::recursive_mutex audioMutex;
static std::atomic<bool> g_AudioShutdown{false};
static void LockAudio() { audioMutex.lock(); }
static void UnlockAudio() { audioMutex.unlock(); }
#endif

static int iSoundActive = 0;
static CHANNEL channel[MAX_CHANNEL]{};
static AMBIENT ambient{};
static AMBIENT ambient2{};
static MAMBIENT mambient{};

enum class AudioBackend {
    OpenALSoft,
    LegacyDLL
};

static AudioBackend g_AudioBackend = AudioBackend::OpenALSoft;
#ifdef _WIN32
static HMODULE g_LegacyAudioDLL = nullptr;

using LegacyAudioInitFn = void (WINAPI *)(HWND, HANDLE);
using LegacyAudioVoidFn = void (WINAPI *)(void);
using LegacyAudioCameraFn = void (WINAPI *)(float, float, float, float, float);
using LegacyAudioSoundFn = void (WINAPI *)(int, short int*, int);
using LegacyAudioSound3DFn = void (WINAPI *)(int, short int*, float, float, float);
using LegacyAudioVoiceFn = void (WINAPI *)(int, short int*, float, float, float, int);
using LegacyAudioVersionFn = int (WINAPI *)(void);
using LegacyAudioEnvFn = void (WINAPI *)(int, float);
using LegacyAudioGeomFn = void (WINAPI *)(int, void*);

static LegacyAudioInitFn g_LegacyInitAudioSystem = nullptr;
static LegacyAudioVoidFn g_LegacyAudioRestore = nullptr;
static LegacyAudioVoidFn g_LegacyAudioStop = nullptr;
static LegacyAudioVoidFn g_LegacyAudioShutdown = nullptr;
static LegacyAudioCameraFn g_LegacyAudioSetCameraPos = nullptr;
static LegacyAudioSoundFn g_LegacySetAmbient = nullptr;
static LegacyAudioSound3DFn g_LegacySetAmbient3d = nullptr;
static LegacyAudioVoiceFn g_LegacyAddVoice3dv = nullptr;
static LegacyAudioVersionFn g_LegacyAudioGetVersion = nullptr;
static LegacyAudioEnvFn g_LegacyAudioSetEnvironment = nullptr;
static LegacyAudioGeomFn g_LegacyAudioUploadGeometry = nullptr;

#endif

// For EAX → EFX reverb
static ALuint g_effect = 0;
static ALuint g_slot   = 0;
static int    g_CurrentEnv = -1;

// ---------------------------------------------------------------------------
// EAX 2.0 environment presets  (values match original EAX2Audio/Audio3d.cpp)
// ---------------------------------------------------------------------------
struct EAX2ENV {
    int   envID;      // EAX preset enum (unused in EFX path)
    int   room;       // mB  (−10000 … 0)
    float decay;      // seconds
    float decayHF;    // ratio
    float diffusion;  // 0.0 … 1.0
    int   reverb;     // mB  (−10000 … 0)
};

static const EAX2ENV g_EnvPresets[9] = {
    { 0, -400, 1.49f,  0.3f,   0.4f,   -620 },  // 0  Generic
    { 1, -400, 1.2f,  0.2f,   0.2f,   -600 },  // 1  Plate
    { 2, -400, 1.49f,  0.18f,  0.3f,   -700 },  // 2  Forest
    { 3, -200, 2.6f,  0.2f,   0.326f, -500 },  // 3  Mountain
    { 4, -400, 1.1f,  0.6f,   0.275f, -700 },  // 4  Canyon
    { 5, -300, 3.2f,  0.6f,   0.9f,   -200 },  // 5  Cave
    { 6, -400, 1.8f,  0.4f,   0.4f,   -400 },  // 6  Special 2
    { 7,    0, 0.0f,  0.0f,   0.0f,      0 },  // 7  Special 3 (no‑op)
    { 8, -400, 1.5f,  0.1f,   0.1f,   -200 },  // 8  Underwater
};

// Convert EAX mB to linear gain (OpenAL EFX uses 0.0–1.0)
static float mBToGain(int mB) {
    return std::pow(10.0f, mB / 2000.0f);
}

// Runtime-tunable copy of the preset table (config.cfg envN_* keys).
// Untouched entries read exactly the compiled defaults above.
static EAX2ENV g_EnvRuntime[9];
static bool g_EnvRuntimeInit = false;
static void EnsureEnvRuntime() {
    if (!g_EnvRuntimeInit) {
        memcpy(g_EnvRuntime, g_EnvPresets, sizeof(g_EnvPresets));
        g_EnvRuntimeInit = true;
    }
}
// field: 0=decay (s, 0.1–20), 1=decayHF (ratio, 0.1–2), 2=diffusion (0–1),
// 3=reverb (mB, −10000–0). room/envID are intentionally not settable — the
// EFX path does not consume them, so a knob would be wired to nothing.
bool Audio_SetEnvParam(int env, int field, float v)
{
    // NaN bypasses ordered range comparisons; reject it (and infinities)
    // before storing floats or converting the reverb level to an integer.
    if (env < 0 || env > 8 || !std::isfinite(v)) return false;
    EnsureEnvRuntime();
    switch (field) {
    case 0: if (v < 0.1f || v > 20.0f) return false; g_EnvRuntime[env].decay = v; break;
    case 1: if (v < 0.1f || v > 2.0f) return false; g_EnvRuntime[env].decayHF = v; break;
    case 2: if (v < 0.0f || v > 1.0f) return false; g_EnvRuntime[env].diffusion = v; break;
    case 3: if (v < -10000.0f || v > 0.0f) return false; g_EnvRuntime[env].reverb = static_cast<int>(v); break;
    default: return false;
    }
    if (env == g_CurrentEnv) g_CurrentEnv = -1;  // force re-push if live
    return true;
}

#ifdef _WIN32
static void UnloadLegacyAudioBackend()
{
    if (g_LegacyAudioDLL) {
        FreeLibrary(g_LegacyAudioDLL);
        g_LegacyAudioDLL = nullptr;
    }

    g_LegacyInitAudioSystem = nullptr;
    g_LegacyAudioRestore = nullptr;
    g_LegacyAudioStop = nullptr;
    g_LegacyAudioShutdown = nullptr;
    g_LegacyAudioSetCameraPos = nullptr;
    g_LegacySetAmbient = nullptr;
    g_LegacySetAmbient3d = nullptr;
    g_LegacyAddVoice3dv = nullptr;
    g_LegacyAudioGetVersion = nullptr;
    g_LegacyAudioSetEnvironment = nullptr;
    g_LegacyAudioUploadGeometry = nullptr;
    g_AudioBackend = AudioBackend::OpenALSoft;
}

static bool LoadLegacyAudioBackend(const char* dllName)
{
    UnloadLegacyAudioBackend();

    g_LegacyAudioDLL = LoadLibraryA(dllName);
    if (!g_LegacyAudioDLL)
        return false;

#define LOAD_LEGACY_PROC(member, type, name) \
    g_##member = reinterpret_cast<type>(GetProcAddress(g_LegacyAudioDLL, name)); \
    if (!g_##member) { UnloadLegacyAudioBackend(); return false; }

    LOAD_LEGACY_PROC(LegacyInitAudioSystem, LegacyAudioInitFn, "InitAudioSystem");
    LOAD_LEGACY_PROC(LegacyAudioRestore, LegacyAudioVoidFn, "Audio_Restore");
    LOAD_LEGACY_PROC(LegacyAudioStop, LegacyAudioVoidFn, "AudioStop");
    LOAD_LEGACY_PROC(LegacyAudioShutdown, LegacyAudioVoidFn, "Audio_Shutdown");
    LOAD_LEGACY_PROC(LegacyAudioSetCameraPos, LegacyAudioCameraFn, "AudioSetCameraPos");
    LOAD_LEGACY_PROC(LegacySetAmbient, LegacyAudioSoundFn, "SetAmbient");
    LOAD_LEGACY_PROC(LegacySetAmbient3d, LegacyAudioSound3DFn, "SetAmbient3d");
    LOAD_LEGACY_PROC(LegacyAddVoice3dv, LegacyAudioVoiceFn, "AddVoice3dv");
    LOAD_LEGACY_PROC(LegacyAudioSetEnvironment, LegacyAudioEnvFn, "Audio_SetEnvironment");

    g_LegacyAudioGetVersion = reinterpret_cast<LegacyAudioVersionFn>(GetProcAddress(g_LegacyAudioDLL, "Audio_GetVersion"));
    g_LegacyAudioUploadGeometry = reinterpret_cast<LegacyAudioGeomFn>(GetProcAddress(g_LegacyAudioDLL, "Audio_UploadGeometry"));

#undef LOAD_LEGACY_PROC

    if (g_LegacyAudioGetVersion) {
        int v = g_LegacyAudioGetVersion();
        char buf[128];
        snprintf(buf, sizeof(buf), "Legacy audio driver version: %d.%d\n", v >> 16, v & 0xFFFF);
        PrintLog(buf);
    }

    g_AudioBackend = AudioBackend::LegacyDLL;
    return true;
}

// ---------------------------------------------------------------------------
// Debug helpers
// ---------------------------------------------------------------------------
#endif

#define AL_CHECK(call) do { call; ALenum err = alGetError ? alGetError() : AL_NO_ERROR; \
    if (err != AL_NO_ERROR) { char m[128]; snprintf(m, sizeof(m),"ALerr 0x%04X at %s\n",err,#call); PrintLog(m); } } while(0)

// ---------------------------------------------------------------------------
// Buffer cache
// ---------------------------------------------------------------------------
static ALuint GetBuffer(short int* lpData, int length) {
    if (!lpData) return 0;
    auto it = bufferCache.find(lpData);
    if (it != bufferCache.end())
        return it->second;

    ALuint buffer = 0;
    AL_CHECK(alGenBuffers(1, &buffer));
    AL_CHECK(alBufferData(buffer, AL_FORMAT_MONO16, lpData, length, 22050));
    bufferCache[lpData] = buffer;
    return buffer;
}

// ---------------------------------------------------------------------------
// Audio thread  (ambient crossfade only)
// ---------------------------------------------------------------------------
static void RunAudioThread() {
    while (!g_AudioShutdown) {
        if (iSoundActive) {
            LockAudio();

            // Fade in new ambient
            if (ambient.volume < 256) {
                float gain = (ambient.volume * ambient.avolume) / (256.0f * 256.0f);
                AL_CHECK(alSourcef(ambient.source, AL_GAIN, gain));
                ambient.volume += 16;
                if (ambient.volume > 256) ambient.volume = 256;
            }

            // Fade out old ambient
            if (ambient2.volume > 0) {
                float gain = (ambient2.volume * ambient2.avolume) / (256.0f * 256.0f);
                AL_CHECK(alSourcef(ambient2.source, AL_GAIN, gain));
                ambient2.volume -= 16;
                if (ambient2.volume <= 0) {
                    ambient2.volume = 0;
                    AL_CHECK(alSourceStop(ambient2.source));
                    AL_CHECK(alSourcei(ambient2.source, AL_BUFFER, 0));
                    ambient2.lpData = nullptr;
                }
            }

            UnlockAudio();
        }
        Platform::SleepMilliseconds(70);
    }
}
#ifdef _WIN32
static DWORD WINAPI ProcessAudioThread(LPVOID) { RunAudioThread(); return 0; }
#endif

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------
void InitAudioSystem(int driver)
{
#ifdef _WIN32
    driver = NormalizeAudioBackend(driver);

    if (driver == AUDIO_DIRECTSOUND) {
        if (LoadLegacyAudioBackend("a_ds3d.dll")) {
            PrintLog("DirectSound: legacy audio DLL loaded\n");
            if (g_LegacyInitAudioSystem)
                g_LegacyInitAudioSystem(hwndMain, hlog);
            g_AudioShutdown = false;
            iSoundActive = 1;
            return;
        }

        PrintLog("DirectSound: legacy audio DLL not found — falling back to OpenAL Soft\n");
    }

#endif

    g_AudioBackend = AudioBackend::OpenALSoft;

    if (!LoadOpenAL()) {
        PrintLog("OpenAL: native library not found — audio disabled\n");
        return;
    }

    alDevice = alcOpenDevice(nullptr);
    if (!alDevice) {
        PrintLog("OpenAL: no default audio device — audio disabled\n");
        UnloadOpenAL();
        return;
    }

    alContext = alcCreateContext(alDevice, nullptr);
    if (!alContext || !alcMakeContextCurrent(alContext)) {
        PrintLog("OpenAL: failed to create context — audio disabled\n");
        if (alContext) alcDestroyContext(alContext);
        alcCloseDevice(alDevice);
        alDevice = nullptr;
        alContext = nullptr;
        UnloadOpenAL();
        return;
    }

#ifdef _WIN32
    InitializeCriticalSection(&AudioCS);
#endif
    g_CurrentEnv = -1;

    AL_CHECK(alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED));

    // ── Voice channels ──
    for (int i = 0; i < MAX_CHANNEL; i++) {
        AL_CHECK(alGenSources(1, &channel[i].source));
        alSourcef(channel[i].source, AL_ROLLOFF_FACTOR, 0.418f);
        alSourcef(channel[i].source, AL_REFERENCE_DISTANCE, static_cast<float>(MIN_RADIUS));
        alSourcef(channel[i].source, AL_MAX_DISTANCE, 10000.0f);
    }

    // ── Ambient (non‑positional, looping) ──
    AL_CHECK(alGenSources(1, &ambient.source));
    alSourcei(ambient.source, AL_LOOPING, AL_TRUE);
    alSourcei(ambient.source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(ambient.source, AL_POSITION, 0, 0, 0);

    AL_CHECK(alGenSources(1, &ambient2.source));
    alSourcei(ambient2.source, AL_LOOPING, AL_TRUE);
    alSourcei(ambient2.source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(ambient2.source, AL_POSITION, 0, 0, 0);

    // ── Moving ambient (positional, looping) ──
    AL_CHECK(alGenSources(1, &mambient.source));
    alSourcei(mambient.source, AL_LOOPING, AL_TRUE);
    alSourcef(mambient.source, AL_ROLLOFF_FACTOR, 0.418f);
    alSourcef(mambient.source, AL_REFERENCE_DISTANCE, static_cast<float>(MIN_RADIUS));
    alSourcef(mambient.source, AL_MAX_DISTANCE, 10000.0f);

    // ── EFX effect + slot (created once, updated on SetEnvironment) ──
    // OpenAL Soft ≥1.24 exposes EFX as AL_SOFT_effect_target, not AL_EXT_EFX.
    // We just try to create the objects and check for errors.
    // Clear any residual AL errors from earlier calls before testing EFX.
    while (alGetError && alGetError() != AL_NO_ERROR);
    if (alGenEffects && alGenAuxiliaryEffectSlots) {
        AL_CHECK(alGenEffects(1, &g_effect));
        ALenum err = alGetError();
        if (err == AL_NO_ERROR && g_effect != 0) {
            AL_CHECK(alGenAuxiliaryEffectSlots(1, &g_slot));
            err = alGetError();
            if (err == AL_NO_ERROR && g_slot != 0) {
                PrintLog("OpenAL: EFX reverb available\n");
                if (alAuxiliaryEffectSlotf)
                    AL_CHECK(alAuxiliaryEffectSlotf(g_slot, AL_EFFECTSLOT_GAIN, 1.0f));
            } else {
                if (g_effect) alDeleteEffects(1, &g_effect);
                if (g_slot) alDeleteAuxiliaryEffectSlots(1, &g_slot);
                g_effect = 0; g_slot = 0;
                PrintLog("OpenAL: EFX slots unavailable\n");
            }
        } else {
            if (g_effect) alDeleteEffects(1, &g_effect);
            g_effect = 0;
            PrintLog("OpenAL: EFX effects unavailable\n");
        }
    } else {
        PrintLog("OpenAL: EFX functions not loaded\n");
    }

    g_AudioShutdown = false;
    iSoundActive = 1;

    // Start the background thread (same pattern as original audio DLLs)
#ifdef _WIN32
    hAudioThread = CreateThread(nullptr, 0, ProcessAudioThread, nullptr, 0, &AudioTId);
    if (hAudioThread)
        SetThreadPriority(hAudioThread, THREAD_PRIORITY_HIGHEST);
#else
    audioThread = std::thread(RunAudioThread);
#endif

    // ── Log device info ──
    {
        char buf[256];
        const char* devName = alcGetString ? alcGetString(alDevice, ALC_DEVICE_SPECIFIER) : nullptr;
        snprintf(buf, sizeof(buf), "OpenAL: device=\"%s\"\n", devName ? devName : "(unknown)");
        PrintLog(buf);
    }
    PrintLog("OpenAL: Audio System Initialized\n");
}

void Audio_Shutdown()
{
#ifdef _WIN32
    if (g_AudioBackend == AudioBackend::LegacyDLL) {
        if (g_LegacyAudioShutdown)
            g_LegacyAudioShutdown();
        UnloadLegacyAudioBackend();
        iSoundActive = 0;
        g_AudioShutdown = true;
        return;
    }
#endif

    if (!iSoundActive) return;

    g_AudioShutdown = true;

#ifdef _WIN32
    if (hAudioThread) {
        WaitForSingleObject(hAudioThread, 5000);
        CloseHandle(hAudioThread);
        hAudioThread = nullptr;
    }
#else
    if (audioThread.joinable()) audioThread.join();
#endif

    LockAudio();
    AudioStop();
    iSoundActive = 0;
    g_CurrentEnv = -1;

    for (int i = 0; i < MAX_CHANNEL; i++)
        AL_CHECK(alDeleteSources(1, &channel[i].source));
    AL_CHECK(alDeleteSources(1, &ambient.source));
    AL_CHECK(alDeleteSources(1, &ambient2.source));
    AL_CHECK(alDeleteSources(1, &mambient.source));

    if (g_effect) { AL_CHECK(alDeleteEffects(1, &g_effect));   g_effect = 0; }
    if (g_slot)   { AL_CHECK(alDeleteAuxiliaryEffectSlots(1, &g_slot)); g_slot   = 0; }

    for (auto& kv : bufferCache)
        AL_CHECK(alDeleteBuffers(1, &kv.second));
    bufferCache.clear();

    UnlockAudio();

    if (alContext) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(alContext);
        alContext = nullptr;
    }
    if (alDevice) {
        alcCloseDevice(alDevice);
        alDevice = nullptr;
    }

#ifdef _WIN32
    DeleteCriticalSection(&AudioCS);
#endif
    UnloadOpenAL();

    PrintLog("OpenAL Audio System Shut Down\n");
}

void AudioStop()
{
#ifdef _WIN32
    if (g_AudioBackend == AudioBackend::LegacyDLL) {
        if (g_LegacyAudioStop)
            g_LegacyAudioStop();
        return;
    }
#endif

    if (!alContext) return;

    LockAudio();

    for (int i = 0; i < MAX_CHANNEL; i++) {
        AL_CHECK(alSourceStop(channel[i].source));
        AL_CHECK(alSourcei(channel[i].source, AL_BUFFER, 0));
        channel[i].lpData = nullptr;
    }

    AL_CHECK(alSourceStop(ambient.source));
    AL_CHECK(alSourcei(ambient.source, AL_BUFFER, 0));
    ambient.lpData = nullptr;
    ambient.volume = 0;

    AL_CHECK(alSourceStop(ambient2.source));
    AL_CHECK(alSourcei(ambient2.source, AL_BUFFER, 0));
    ambient2.lpData = nullptr;
    ambient2.volume = 0;

    AL_CHECK(alSourceStop(mambient.source));
    AL_CHECK(alSourcei(mambient.source, AL_BUFFER, 0));
    mambient.lpData = nullptr;

    UnlockAudio();
}

// ---------------------------------------------------------------------------
// Restore  (no‑op for OpenAL — device loss is handled by the OS)
// ---------------------------------------------------------------------------
void Audio_Restore()
{
#ifdef _WIN32
    if (g_AudioBackend == AudioBackend::LegacyDLL) {
        if (g_LegacyAudioRestore)
            g_LegacyAudioRestore();
        return;
    }
#endif

    // Nothing to do.
}

// ---------------------------------------------------------------------------
// Camera / listener
// ---------------------------------------------------------------------------
void AudioSetCameraPos(float cx, float cy, float cz, float ca, float cb)
{
#ifdef _WIN32
    if (g_AudioBackend == AudioBackend::LegacyDLL) {
        if (g_LegacyAudioSetCameraPos)
            g_LegacyAudioSetCameraPos(cx, cy, cz, ca, cb);
        return;
    }
#endif

    if (!iSoundActive) return;

    ALfloat pos[] = { cx, cy, cz };
    ALfloat orient[] = {
        std::sin(ca) * std::cos(cb),
        std::sin(cb),
       -std::cos(ca) * std::cos(cb),

       -std::sin(ca) * std::sin(cb),
        std::cos(cb),
        std::cos(ca) * std::sin(cb)
    };
    alListenerfv(AL_POSITION, pos);
    alListenerfv(AL_ORIENTATION, orient);
}

// ---------------------------------------------------------------------------
// Ambient (non‑positional, e.g. jungle loop)
// ---------------------------------------------------------------------------
void SetAmbient(int length, short int* lpdata, int av)
{
#ifdef _WIN32
    if (g_AudioBackend == AudioBackend::LegacyDLL) {
        if (g_LegacySetAmbient)
            g_LegacySetAmbient(length, lpdata, av);
        return;
    }
#endif

    if (!iSoundActive) return;

    LockAudio();

    // Already playing this exact sound — nothing to do
    if (ambient.lpData == lpdata && ambient.avolume == av) {
        UnlockAudio();
        return;
    }

    // Move current ambient to fade‑out slot
    AL_CHECK(alSourceStop(ambient2.source));
    AL_CHECK(alSourcei(ambient2.source, AL_BUFFER, 0));

    ambient2.lpData  = ambient.lpData;
    ambient2.iLength = ambient.iLength;
    ambient2.volume  = ambient.volume;
    ambient2.avolume = ambient.avolume;
    ambient2.buffer  = ambient.buffer;

    if (ambient2.lpData && ambient2.buffer && ambient2.volume > 0) {
        float oldGain = (ambient2.volume * ambient2.avolume) / (256.0f * 256.0f);
        AL_CHECK(alSourcei(ambient2.source, AL_BUFFER, ambient2.buffer));
        AL_CHECK(alSourcef(ambient2.source, AL_GAIN, oldGain));
        AL_CHECK(alSourcePlay(ambient2.source));
    }

    // Start new ambient at zero volume (thread will fade in)
    AL_CHECK(alSourceStop(ambient.source));
    AL_CHECK(alSourcei(ambient.source, AL_BUFFER, 0));

    ambient.lpData   = lpdata;
    ambient.iLength  = length;
    ambient.volume   = 0;
    ambient.avolume  = av;
    ambient.buffer   = GetBuffer(lpdata, length);

    if (ambient.buffer) {
        AL_CHECK(alSourcei(ambient.source, AL_BUFFER, ambient.buffer));
        AL_CHECK(alSourcef(ambient.source, AL_GAIN, 0.0f));
        AL_CHECK(alSourcePlay(ambient.source));
    } else {
        ambient.lpData  = nullptr;
        ambient.iLength = 0;
        ambient.avolume = 0;
    }

    UnlockAudio();
}

// ---------------------------------------------------------------------------
// Ambient with 3D position (e.g. ship engine)
// ---------------------------------------------------------------------------
void SetAmbient3d(int length, short int* lpdata, float cx, float cy, float cz)
{
#ifdef _WIN32
    if (g_AudioBackend == AudioBackend::LegacyDLL) {
        if (g_LegacySetAmbient3d)
            g_LegacySetAmbient3d(length, lpdata, cx, cy, cz);
        return;
    }
#endif

    if (!iSoundActive) return;

    LockAudio();

    if (!lpdata) {
        // Stop moving ambient
        AL_CHECK(alSourceStop(mambient.source));
        AL_CHECK(alSourcei(mambient.source, AL_BUFFER, 0));
        mambient.lpData = nullptr;
        UnlockAudio();
        return;
    }

    if (mambient.lpData != lpdata) {
        mambient.lpData  = lpdata;
        mambient.iLength = length;
        mambient.buffer  = GetBuffer(lpdata, length);
        AL_CHECK(alSourcei(mambient.source, AL_BUFFER, mambient.buffer));
        AL_CHECK(alSourcePlay(mambient.source));
    }

    mambient.x = cx;
    mambient.y = cy;
    mambient.z = cz;
    AL_CHECK(alSource3f(mambient.source, AL_POSITION, cx, cy, cz));

    UnlockAudio();
}

// ---------------------------------------------------------------------------
// Moving-ambient channel queries. The trophy ship and the resupply ship
// SHARE the single looping mambient channel, so callers must coordinate:
// take it only when free or already yours, and stop it on dismissal only
// when you still own it. Otherwise the idle ship's per-frame stop makes
// the active ship replay from frame 0 at 60 Hz (the "engine buzz").
// ---------------------------------------------------------------------------
bool IsAmbient3dOwner(short int* lpdata)
{
    if (g_AudioBackend == AudioBackend::LegacyDLL) return true; // unknowable: keep legacy behaviour
    if (!iSoundActive) return false;
    LockAudio();
    bool owned = (lpdata != nullptr && mambient.lpData == lpdata);
    UnlockAudio();
    return owned;
}

bool IsAmbient3dFree()
{
    if (g_AudioBackend == AudioBackend::LegacyDLL) return true; // unknowable: keep legacy behaviour
    if (!iSoundActive) return false;
    LockAudio();
    bool idle = (mambient.lpData == nullptr);
    UnlockAudio();
    return idle;
}

// ---------------------------------------------------------------------------
// 3D voice (one‑shot sounds)
// ---------------------------------------------------------------------------
void AddVoice3dv(int length, short int* lpdata, float cx, float cy, float cz, int vol)
{
#ifdef _WIN32
    if (g_AudioBackend == AudioBackend::LegacyDLL) {
        if (g_LegacyAddVoice3dv && lpdata)
            g_LegacyAddVoice3dv(length, lpdata, cx, cy, cz, vol);
        return;
    }
#endif

    if (!iSoundActive || !lpdata) return;

    LockAudio();

    // Find a free channel
    int idx = -1;
    for (int i = 0; i < MAX_CHANNEL; i++) {
        ALint state;
        AL_CHECK(alGetSourcei(channel[i].source, AL_SOURCE_STATE, &state));
        if (state != AL_PLAYING) { idx = i; break; }
    }
    if (idx < 0) { UnlockAudio(); return; }

    channel[idx].lpData  = lpdata;
    channel[idx].x       = cx;
    channel[idx].y       = cy;
    channel[idx].z       = cz;
    channel[idx].volume  = vol;
    channel[idx].buffer  = GetBuffer(lpdata, length);

    AL_CHECK(alSourcei(channel[idx].source, AL_BUFFER, channel[idx].buffer));
    AL_CHECK(alSourcef(channel[idx].source, AL_GAIN, vol / 256.0f));

    if (cx == 0.0f && cy == 0.0f && cz == 0.0f) {
        AL_CHECK(alSourcei(channel[idx].source, AL_SOURCE_RELATIVE, AL_TRUE));
        AL_CHECK(alSource3f(channel[idx].source, AL_POSITION, 0, 0, 0));
    } else {
        AL_CHECK(alSourcei(channel[idx].source, AL_SOURCE_RELATIVE, AL_FALSE));
        AL_CHECK(alSource3f(channel[idx].source, AL_POSITION, cx, cy, cz));
    }

    AL_CHECK(alSourcePlay(channel[idx].source));

    UnlockAudio();
}

void AddVoice3d(int length, short int* lpdata, float cx, float cy, float cz)
{
    AddVoice3dv(length, lpdata, cx, cy, cz, 256);
}

void AddVoicev(int length, short int* lpdata, int v)
{
    AddVoice3dv(length, lpdata, 0, 0, 0, v);
}

void AddVoice(int length, short int* lpdata)
{
    AddVoice3dv(length, lpdata, 0, 0, 0, 256);
}

// ---------------------------------------------------------------------------
// Environment reverb  (EAX → OpenAL EFX)
// ---------------------------------------------------------------------------
void Audio_SetEnvironment(int e, float f)
{
#ifdef _WIN32
    if (g_AudioBackend == AudioBackend::LegacyDLL) {
        if (g_LegacyAudioSetEnvironment)
            g_LegacyAudioSetEnvironment(e, f);
        return;
    }
#endif

    if (!iSoundActive || !alContext) return;
    if (e == g_CurrentEnv) return;
    g_CurrentEnv = e;
    EnsureEnvRuntime();

    // No EFX support — silently ignore
    if (!g_effect || !g_slot) { PrintLog("Audio_SetEnvironment: no EFX objects\n"); return; }
    if (e < 0 || e > 8) { PrintLog("Audio_SetEnvironment: invalid env %d\n"); return; }
    if (e == 7) return;  // Special 3 — intentionally empty

    // Clear any residual errors before setting up reverb
    while (alGetError && alGetError() != AL_NO_ERROR);

    const EAX2ENV* env = &g_EnvRuntime[e];
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "Audio_SetEnvironment: env=%d gain=100 decay=%d decayHF=%d diff=%d reverblevel=%d\n",
                  e, static_cast<int>((env->decay*10)), static_cast<int>((env->decayHF*100)), static_cast<int>((env->diffusion*100)), env->reverb);
        PrintLog(buf);
    }

    AL_CHECK(alEffecti(g_effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB));

    // Set reverb parameters — map EAX mB fields correctly:
    //   room   → AL_EAXREVERB_ROOM (reverb return level in mB)
    //   reverb → AL_EAXREVERB_LATE_REVERB_GAIN (late reverb level, linear from mB)
    //   decay  → AL_EAXREVERB_DECAY_TIME
    //   decayHF → AL_EAXREVERB_DECAY_HFRATIO
    //   diffusion → AL_EAXREVERB_DIFFUSION
    // Note: AL_EAXREVERB_ROOM does not exist in OpenAL Soft's EFX.
    // The EAX2 room level is mapped via the gain parameters below.
    AL_CHECK(alEffectf(g_effect, AL_EAXREVERB_GAIN, 1.0f));
    AL_CHECK(alEffectf(g_effect, AL_EAXREVERB_DECAY_TIME, env->decay));
    AL_CHECK(alEffectf(g_effect, AL_EAXREVERB_DECAY_HFRATIO, env->decayHF));
    AL_CHECK(alEffectf(g_effect, AL_EAXREVERB_DIFFUSION, env->diffusion));
    AL_CHECK(alEffectf(g_effect, AL_EAXREVERB_LATE_REVERB_GAIN, mBToGain(env->reverb)));

    // Apply effect to slot, then set slot gain
    AL_CHECK(alAuxiliaryEffectSloti(g_slot, AL_EFFECTSLOT_EFFECT, g_effect));

    // Connect to voice channels
    for (int i = 0; i < MAX_CHANNEL; i++) {
        AL_CHECK(alSource3i(channel[i].source, AL_AUXILIARY_SEND_FILTER,
                            g_slot, 0, AL_FILTER_NULL));
    }
    AL_CHECK(alSource3i(ambient.source, AL_AUXILIARY_SEND_FILTER,
                        g_slot, 0, AL_FILTER_NULL));
    AL_CHECK(alSource3i(ambient2.source, AL_AUXILIARY_SEND_FILTER,
                        g_slot, 0, AL_FILTER_NULL));
    AL_CHECK(alSource3i(mambient.source, AL_AUXILIARY_SEND_FILTER,
                        g_slot, 0, AL_FILTER_NULL));
}

// ---------------------------------------------------------------------------
// Terrain geometry upload  (no‑op for OpenAL)
// Still calls UploadGeometry() to keep the data array populated, but
// never sends it to any audio driver.
// ---------------------------------------------------------------------------
void Audio_UploadGeometry()
{
    UploadGeometry();

#ifdef _WIN32
    if (g_AudioBackend == AudioBackend::LegacyDLL) {
        if (g_LegacyAudioUploadGeometry)
            g_LegacyAudioUploadGeometry(AudioFCount, data);
        return;
    }
#endif

    // OpenAL Soft has no terrain‑occlusion equivalent — discard.
}
