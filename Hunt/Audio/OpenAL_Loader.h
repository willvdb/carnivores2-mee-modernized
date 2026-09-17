#ifndef OPENAL_LOADER_H
#define OPENAL_LOADER_H



// --- OpenAL types ---
typedef char ALchar;
typedef char ALCchar;
typedef unsigned char ALboolean;
typedef unsigned char ALCboolean;
typedef signed char ALbyte;
typedef short ALshort;
typedef unsigned short ALushort;
typedef int ALint;
typedef int ALCint;
typedef unsigned int ALuint;
typedef int ALsizei;
typedef int ALenum;
typedef int ALCenum;
typedef float ALfloat;
typedef double ALdouble;
typedef void ALvoid;
typedef struct ALCdevice_struct ALCdevice;
typedef struct ALCcontext_struct ALCcontext;

// --- Constants ---
#define AL_VERSION 0x1001
#define AL_RENDERER 0x1003
#define ALC_DEVICE_SPECIFIER 0x1005
#define ALC_ALL_DEVICES_SPECIFIER 0x1013
#define AL_TRUE 1
#define AL_FALSE 0
#define AL_NONE 0
#define AL_NO_ERROR 0
#define AL_BUFFER 0x1009
#define AL_SOURCE_RELATIVE 0x202
#define AL_SOURCE_STATE 0x1010
#define AL_PLAYING 0x1012
#define AL_STOPPED 0x1014
#define AL_POSITION 0x1004
#define AL_ORIENTATION 0x100F
#define AL_GAIN 0x100A
#define AL_LOOPING 0x1007
#define AL_REFERENCE_DISTANCE 0x1020
#define AL_ROLLOFF_FACTOR 0x1021
#define AL_MAX_DISTANCE 0x1023
#define AL_FORMAT_MONO16 0x1101
#define AL_DISTANCE_MODEL 0xD000
#define AL_INVERSE_DISTANCE_CLAMPED 0xD002
#define ALC_APIENTRY
#define AL_APIENTRY

// --- EFX extension (OpenAL Soft) ---
#define AL_EFFECT_TYPE 0x8001
#define AL_EFFECTSLOT_TYPE 0x8091
#define AL_FILTER_TYPE 0x8005
#define AL_EFFECT_REVERB 0x0001
#define AL_EFFECT_EAXREVERB 0x8000
#define AL_EFFECTSLOT_EFFECT 0x0001
#define AL_EFFECTSLOT_GAIN 0x0002
#define AL_AUXILIARY_SEND_FILTER 0x20006
#define AL_FILTER_NULL 0x0000
#define AL_EXT_EFX 1

// Standard REVERB params (for AL_EFFECT_REVERB type)
#define AL_REVERB_DENSITY 0x0001
#define AL_REVERB_DIFFUSION 0x0002
#define AL_REVERB_GAIN 0x0003
#define AL_REVERB_GAINHF 0x0004
#define AL_REVERB_DECAY_TIME 0x0005
#define AL_REVERB_DECAY_HFRATIO 0x0006
#define AL_REVERB_REFLECTIONS_GAIN 0x0007
#define AL_REVERB_REFLECTIONS_DELAY 0x0008
#define AL_REVERB_LATE_REVERB_GAIN 0x0009

// EAX REVERB params (for AL_EFFECT_EAXREVERB type)
#define AL_EAXREVERB_DENSITY 0x0001
#define AL_EAXREVERB_DIFFUSION 0x0002
#define AL_EAXREVERB_GAIN 0x0003
#define AL_EAXREVERB_GAINHF 0x0004
#define AL_EAXREVERB_GAINLF 0x0005
#define AL_EAXREVERB_DECAY_TIME 0x0006
#define AL_EAXREVERB_DECAY_HFRATIO 0x0007
#define AL_EAXREVERB_DECAY_LFRATIO 0x0008
#define AL_EAXREVERB_REFLECTIONS_GAIN 0x0009
#define AL_EAXREVERB_REFLECTIONS_DELAY 0x000A
#define AL_EAXREVERB_REFLECTIONS_PAN 0x000B
#define AL_EAXREVERB_LATE_REVERB_GAIN 0x000C
#define AL_EAXREVERB_LATE_REVERB_DELAY 0x000D
#define AL_EAXREVERB_LATE_REVERB_PAN 0x000E
#define AL_EAXREVERB_ECHO_TIME 0x000F
#define AL_EAXREVERB_ECHO_DEPTH 0x0010
#define AL_EAXREVERB_AIR_ABSORPTION_GAINHF 0x0013

// --- Function pointer types ---
typedef void (AL_APIENTRY *LPALDISTANCEMODEL)(ALenum distanceModel);
typedef ALCdevice* (ALC_APIENTRY *LPALCOPENDEVICE)(const ALCchar* devicename);
typedef ALCboolean (ALC_APIENTRY *LPALCCLOSEDEVICE)(ALCdevice* device);
typedef ALCcontext* (ALC_APIENTRY *LPALCCREATECONTEXT)(ALCdevice* device, const ALCint* attrlist);
typedef ALCboolean (ALC_APIENTRY *LPALCMAKECONTEXTCURRENT)(ALCcontext* context);
typedef void (ALC_APIENTRY *LPALCDESTROYCONTEXT)(ALCcontext* context);
typedef void (AL_APIENTRY *LPALGENBUFFERS)(ALsizei n, ALuint* buffers);
typedef void (AL_APIENTRY *LPALDELETEBUFFERS)(ALsizei n, const ALuint* buffers);
typedef void (AL_APIENTRY *LPALBUFFERDATA)(ALuint buffer, ALenum format, const ALvoid* data, ALsizei size, ALsizei freq);
typedef void (AL_APIENTRY *LPALGENSOURCES)(ALsizei n, ALuint* sources);
typedef void (AL_APIENTRY *LPALDELETESOURCES)(ALsizei n, const ALuint* sources);
typedef void (AL_APIENTRY *LPALSOURCEI)(ALuint source, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALSOURCEF)(ALuint source, ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALSOURCE3F)(ALuint source, ALenum param, ALfloat v1, ALfloat v2, ALfloat v3);
typedef void (AL_APIENTRY *LPALSOURCEPLAY)(ALuint source);
typedef void (AL_APIENTRY *LPALSOURCESTOP)(ALuint source);
typedef void (AL_APIENTRY *LPALGETSOURCEI)(ALuint source, ALenum param, ALint* value);
typedef ALenum (AL_APIENTRY *LPALGETERROR)(void);
typedef void (AL_APIENTRY *LPALLISTENERF)(ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALLISTENER3F)(ALenum param, ALfloat v1, ALfloat v2, ALfloat v3);
typedef void (AL_APIENTRY *LPALLISTENERFV)(ALenum param, const ALfloat* values);
typedef void (AL_APIENTRY *LPALGENEFFECTS)(ALsizei n, ALuint* effects);
typedef void (AL_APIENTRY *LPALDELETEEFFECTS)(ALsizei n, const ALuint* effects);
typedef void (AL_APIENTRY *LPALEFFECTI)(ALuint effect, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALEFFECTF)(ALuint effect, ALenum param, ALfloat value);
typedef void (AL_APIENTRY *LPALGENAUXILIARYEFFECTSLOTS)(ALsizei n, ALuint* slots);
typedef void (AL_APIENTRY *LPALDELETEAUXILIARYEFFECTSLOTS)(ALsizei n, const ALuint* slots);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTI)(ALuint slot, ALenum param, ALint value);
typedef void (AL_APIENTRY *LPALAUXILIARYEFFECTSLOTF)(ALuint slot, ALenum param, ALfloat value);
typedef ALCboolean (AL_APIENTRY *LPALISEXTENSIONPRESENT)(const ALchar* extname);
typedef void (AL_APIENTRY *LPALSOURCE3I)(ALuint source, ALenum param, ALint value1, ALint value2, ALint value3);
typedef const ALchar* (AL_APIENTRY *LPALGETSTRING)(ALenum param);
typedef const ALCchar* (ALC_APIENTRY *LPALCGETSTRING)(ALCdevice* device, ALCenum param);

// --- Global function pointers ---
extern LPALCOPENDEVICE alcOpenDevice;
extern LPALCCLOSEDEVICE alcCloseDevice;
extern LPALCCREATECONTEXT alcCreateContext;
extern LPALCMAKECONTEXTCURRENT alcMakeContextCurrent;
extern LPALCDESTROYCONTEXT alcDestroyContext;
extern LPALDISTANCEMODEL alDistanceModel;
extern LPALGENBUFFERS alGenBuffers;
extern LPALDELETEBUFFERS alDeleteBuffers;
extern LPALBUFFERDATA alBufferData;
extern LPALGENSOURCES alGenSources;
extern LPALDELETESOURCES alDeleteSources;
extern LPALSOURCEI alSourcei;
extern LPALSOURCEF alSourcef;
extern LPALSOURCE3F alSource3f;
extern LPALSOURCEPLAY alSourcePlay;
extern LPALSOURCESTOP alSourceStop;
extern LPALGETSOURCEI alGetSourcei;
extern LPALGETERROR alGetError;
extern LPALLISTENERF alListenerf;
extern LPALLISTENER3F alListener3f;
extern LPALLISTENERFV alListenerfv;
extern LPALGENEFFECTS alGenEffects;
extern LPALDELETEEFFECTS alDeleteEffects;
extern LPALEFFECTI alEffecti;
extern LPALEFFECTF alEffectf;
extern LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
extern LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
extern LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
extern LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
extern LPALISEXTENSIONPRESENT alIsExtensionPresent;
extern LPALSOURCE3I alSource3i;
extern LPALGETSTRING alGetString;
extern LPALCGETSTRING alcGetString;

bool LoadOpenAL(const char* library = nullptr);
void UnloadOpenAL();

#endif
