#include "OpenAL_Loader.h"
#include "../Platform/SharedLibrary.h"

LPALCOPENDEVICE alcOpenDevice = nullptr;
LPALCCLOSEDEVICE alcCloseDevice = nullptr;
LPALCCREATECONTEXT alcCreateContext = nullptr;
LPALCMAKECONTEXTCURRENT alcMakeContextCurrent = nullptr;
LPALCDESTROYCONTEXT alcDestroyContext = nullptr;
LPALDISTANCEMODEL alDistanceModel = nullptr;
LPALGENBUFFERS alGenBuffers = nullptr;
LPALDELETEBUFFERS alDeleteBuffers = nullptr;
LPALBUFFERDATA alBufferData = nullptr;
LPALGENSOURCES alGenSources = nullptr;
LPALDELETESOURCES alDeleteSources = nullptr;
LPALSOURCEI alSourcei = nullptr;
LPALSOURCEF alSourcef = nullptr;
LPALSOURCE3F alSource3f = nullptr;
LPALSOURCEPLAY alSourcePlay = nullptr;
LPALSOURCESTOP alSourceStop = nullptr;
LPALGETSOURCEI alGetSourcei = nullptr;
LPALGETERROR alGetError = nullptr;
LPALLISTENERF alListenerf = nullptr;
LPALLISTENER3F alListener3f = nullptr;
LPALLISTENERFV alListenerfv = nullptr;
LPALGENEFFECTS alGenEffects = nullptr;
LPALDELETEEFFECTS alDeleteEffects = nullptr;
LPALEFFECTI alEffecti = nullptr;
LPALEFFECTF alEffectf = nullptr;
LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots = nullptr;
LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots = nullptr;
LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti = nullptr;
LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf = nullptr;
LPALISEXTENSIONPRESENT alIsExtensionPresent = nullptr;
LPALSOURCE3I alSource3i = nullptr;
LPALGETSTRING alGetString = nullptr;
LPALCGETSTRING alcGetString = nullptr;

static Platform::SharedLibrary hOpenAL = nullptr;

bool LoadOpenAL(const char* library) {
    if (hOpenAL) UnloadOpenAL();
#ifdef _WIN32
    const char* defaultLibrary = "openal32.dll";
#else
    const char* defaultLibrary = "libopenal.so.1";
#endif
    hOpenAL = Platform::OpenSharedLibrary(library ? library : defaultLibrary);
    if (!hOpenAL) return false;

    alcOpenDevice = (LPALCOPENDEVICE)Platform::SharedLibrarySymbol(hOpenAL, "alcOpenDevice");
    alcCloseDevice = (LPALCCLOSEDEVICE)Platform::SharedLibrarySymbol(hOpenAL, "alcCloseDevice");
    alcCreateContext = (LPALCCREATECONTEXT)Platform::SharedLibrarySymbol(hOpenAL, "alcCreateContext");
    alcMakeContextCurrent = (LPALCMAKECONTEXTCURRENT)Platform::SharedLibrarySymbol(hOpenAL, "alcMakeContextCurrent");
    alcDestroyContext = (LPALCDESTROYCONTEXT)Platform::SharedLibrarySymbol(hOpenAL, "alcDestroyContext");
    alDistanceModel = (LPALDISTANCEMODEL)Platform::SharedLibrarySymbol(hOpenAL, "alDistanceModel");
    alGenBuffers = (LPALGENBUFFERS)Platform::SharedLibrarySymbol(hOpenAL, "alGenBuffers");
    alDeleteBuffers = (LPALDELETEBUFFERS)Platform::SharedLibrarySymbol(hOpenAL, "alDeleteBuffers");
    alBufferData = (LPALBUFFERDATA)Platform::SharedLibrarySymbol(hOpenAL, "alBufferData");
    alGenSources = (LPALGENSOURCES)Platform::SharedLibrarySymbol(hOpenAL, "alGenSources");
    alDeleteSources = (LPALDELETESOURCES)Platform::SharedLibrarySymbol(hOpenAL, "alDeleteSources");
    alSourcei = (LPALSOURCEI)Platform::SharedLibrarySymbol(hOpenAL, "alSourcei");
    alSourcef = (LPALSOURCEF)Platform::SharedLibrarySymbol(hOpenAL, "alSourcef");
    alSource3f = (LPALSOURCE3F)Platform::SharedLibrarySymbol(hOpenAL, "alSource3f");
    alSourcePlay = (LPALSOURCEPLAY)Platform::SharedLibrarySymbol(hOpenAL, "alSourcePlay");
    alSourceStop = (LPALSOURCESTOP)Platform::SharedLibrarySymbol(hOpenAL, "alSourceStop");
    alGetSourcei = (LPALGETSOURCEI)Platform::SharedLibrarySymbol(hOpenAL, "alGetSourcei");
    alGetError = (LPALGETERROR)Platform::SharedLibrarySymbol(hOpenAL, "alGetError");
    alListenerf = (LPALLISTENERF)Platform::SharedLibrarySymbol(hOpenAL, "alListenerf");
    alListener3f = (LPALLISTENER3F)Platform::SharedLibrarySymbol(hOpenAL, "alListener3f");
    alListenerfv = (LPALLISTENERFV)Platform::SharedLibrarySymbol(hOpenAL, "alListenerfv");
    alGenEffects = (LPALGENEFFECTS)Platform::SharedLibrarySymbol(hOpenAL, "alGenEffects");
    alDeleteEffects = (LPALDELETEEFFECTS)Platform::SharedLibrarySymbol(hOpenAL, "alDeleteEffects");
    alEffecti = (LPALEFFECTI)Platform::SharedLibrarySymbol(hOpenAL, "alEffecti");
    alEffectf = (LPALEFFECTF)Platform::SharedLibrarySymbol(hOpenAL, "alEffectf");
    alGenAuxiliaryEffectSlots = (LPALGENAUXILIARYEFFECTSLOTS)Platform::SharedLibrarySymbol(hOpenAL, "alGenAuxiliaryEffectSlots");
    alDeleteAuxiliaryEffectSlots = (LPALDELETEAUXILIARYEFFECTSLOTS)Platform::SharedLibrarySymbol(hOpenAL, "alDeleteAuxiliaryEffectSlots");
    alAuxiliaryEffectSloti = (LPALAUXILIARYEFFECTSLOTI)Platform::SharedLibrarySymbol(hOpenAL, "alAuxiliaryEffectSloti");
    alAuxiliaryEffectSlotf = (LPALAUXILIARYEFFECTSLOTF)Platform::SharedLibrarySymbol(hOpenAL, "alAuxiliaryEffectSlotf");
    alIsExtensionPresent = (LPALISEXTENSIONPRESENT)Platform::SharedLibrarySymbol(hOpenAL, "alIsExtensionPresent");
    alSource3i = (LPALSOURCE3I)Platform::SharedLibrarySymbol(hOpenAL, "alSource3i");
    alGetString = (LPALGETSTRING)Platform::SharedLibrarySymbol(hOpenAL, "alGetString");
    alcGetString = (LPALCGETSTRING)Platform::SharedLibrarySymbol(hOpenAL, "alcGetString");

    // Core functions required (EFX optional)
    if (!alcOpenDevice || !alcCloseDevice || !alcCreateContext ||
        !alcMakeContextCurrent || !alcDestroyContext || !alDistanceModel ||
        !alGenBuffers || !alDeleteBuffers || !alBufferData ||
        !alGenSources || !alDeleteSources || !alSourcei || !alSourcef ||
        !alSource3f || !alSourcePlay || !alSourceStop || !alGetSourcei ||
        !alGetError || !alListenerf || !alListener3f || !alListenerfv) {
        UnloadOpenAL();
        return false;
    }

    return true;
}

void UnloadOpenAL() {
    if (hOpenAL) {
        Platform::CloseSharedLibrary(hOpenAL);
        hOpenAL = nullptr;

        alcOpenDevice = nullptr;
        alcCloseDevice = nullptr;
        alcCreateContext = nullptr;
        alcMakeContextCurrent = nullptr;
        alcDestroyContext = nullptr;
        alDistanceModel = nullptr;
        alGenBuffers = nullptr;
        alDeleteBuffers = nullptr;
        alBufferData = nullptr;
        alGenSources = nullptr;
        alDeleteSources = nullptr;
        alSourcei = nullptr;
        alSourcef = nullptr;
        alSource3f = nullptr;
        alSourcePlay = nullptr;
        alSourceStop = nullptr;
        alGetSourcei = nullptr;
        alGetError = nullptr;
        alListenerf = nullptr;
        alListener3f = nullptr;
        alListenerfv = nullptr;
        alGenEffects = nullptr;
        alDeleteEffects = nullptr;
        alEffecti = nullptr;
        alEffectf = nullptr;
        alGenAuxiliaryEffectSlots = nullptr;
        alDeleteAuxiliaryEffectSlots = nullptr;
        alAuxiliaryEffectSloti = nullptr;
        alAuxiliaryEffectSlotf = nullptr;
        alIsExtensionPresent = nullptr;
        alSource3i = nullptr;
        alGetString = nullptr;
        alcGetString = nullptr;
    }
}
