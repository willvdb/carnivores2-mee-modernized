// Constants.h — Numerical constants and scoped enums
// Extracted from Hunt.h (Phase 0.1 — Split god header into focused headers)
#pragma once

#include <cstdint>
#include <type_traits>
#include <math.h>

// ===================== Build / Version =====================

// 1.0.4   =4
// 1.0.5   =5
// 1.0.6   =6
// 1.0.6.1 =7
// 1.1     =8
inline constexpr int MODDERS_EDITION_VERSION_ID = 9; //1.1.1

// ===================== Network =====================

inline constexpr int DEFAULT_BUFLEN = 512;
inline constexpr char DEFAULT_PORT[] = "1986";

// ===================== World Scale =====================

inline constexpr int ctHScale = 64;
inline constexpr int ctMapSize = 1024;
inline constexpr int kViewGridCenter = 256;
inline constexpr int kViewGridSize = kViewGridCenter * 2;

// ===================== Renderer =====================

// Maximum value returned by GLRenderer::GetSunLight(), as per the cap in
// GLSky.cpp.  Keep in sync if that cap ever changes.
inline constexpr float kMaxSunLight = 140.0f;

// ===================== View Distance =====================

inline constexpr int kViewOptMin = 0;
inline constexpr int kViewOptMax = 255;
inline constexpr int kViewOptDefault = 128;
inline constexpr int kViewDistanceMin = 42;
inline constexpr int kViewDistanceMax = 230;
inline constexpr int kViewDistanceDefault = 72;

inline int ClampViewOpt(int value)
{
    if (value < kViewOptMin) return kViewOptMin;
    if (value > kViewOptMax) return kViewOptMax;
    return value;
}

inline int ViewOptToCtViewR(int opt)
{
    opt = ClampViewOpt(opt);

    // Preserve the legacy 0..127 OptViewR curve, then use the upper half
    // of the menu's 0..255 range for the extended view-distance cap.
    if (opt <= 127)
        return 42 + (opt / 8) * 2;

    return 72 + ((opt - 127) * (kViewDistanceMax - 72)) / (kViewOptMax - 127);
}

// Extended rendering distance must not enlarge legacy gameplay perception.
// Values below the old ceiling retain their original distance-dependent balance.
inline int GameplayViewRadiusCells(int renderViewRadiusCells)
{
    if (renderViewRadiusCells > kViewDistanceDefault)
        return kViewDistanceDefault;
    return renderViewRadiusCells;
}

inline float GunshotNoiseRangeWorld(int renderViewRadiusCells, float weaponLoudness)
{
    return static_cast<float>(GameplayViewRadiusCells(renderViewRadiusCells))
        * 200.0f * weaponLoudness;
}

inline constexpr int kShotInvestigationMinTime = 10 * 1024;
inline constexpr int kShotInvestigationMaxTime = 30 * 1024;
inline constexpr int kTRexShotInvestigationMaxTime = 60 * 1024;
inline constexpr float kShotInvestigationArrivalRadius = 512.0f;
// Once a fixed reaction reaches the stored event position, the creature keeps
// searching the area around it (instead of running past it or dropping
// straight to normal wander) until the reaction timer expires.
inline constexpr float kShotSearchRadius = 2048.0f;

// A flee leg is a direction, not a destination: it only has to carry the
// creature away before the next leg is aimed. The last stretch toward a leg
// point is often untraversable (shoreline, rocks, trees), and an escaping
// animal that must close to the investigation radius before re-aiming ends up
// orbiting the unreachable point for the rest of its reaction. Half a leg is
// close enough.
inline constexpr float kFleeLegArrivalRadius = 1024.0f;
// Fallback clock for a leg that cannot be closed at all (for example a point
// across an inlet): after this long without reaching it, the flee ray is
// re-aimed with a rotated heading so the same unreachable direction is not
// picked again. Accumulated in ms on TCharacter::tgtime, which doubles as the
// per-leg clock (reset to 0 whenever a target is set).
inline constexpr int kFleeLegStuckMs = 4000;

// Contact-range awareness (see ShouldPromotePursuitToTracking): a creature
// following a remembered event position notices the hunter when the hunter is
// physically inside its attack reach, and keeps tracking for this long.
// Matches the direct-hit reaction window so defending a contact-range
// intrusion lasts as long as an authored hit response.
inline constexpr int kCloseRangeAwarenessTime = 60 * 1000;

// A tracking pack member republishes its own position as the pack's hunt
// anchor every frame; the anchor expires this soon after it stops reporting,
// so packmates do not chase a stale position once the tracker loses the
// hunter.
inline constexpr int kPackHuntAnchorTime = 2 * 1024;

// A hunter event (heard shot or direct hit) is a stronger stimulus than
// passive detection, so the event reaction uses the species' authored
// aggression range multiplied by this scale. A predator with a large range
// (Carnotaurus) covers any shot it can hear; a low-aggression herbivore
// (Pachycephalosaurus, aggress 60) still flees from a genuinely distant event
// instead of charging the source. Tune this single value to change how far
// events carry: raise it for more pursuit, lower it for more flight.
inline constexpr float kHunterEventRangeScale = 2.5f;

inline int ShotInvestigationTime(float distance, float hearingRange, bool isTRex)
{
    if (hearingRange <= 0.0f)
        return kShotInvestigationMinTime;

    float proximity = 1.0f - distance / hearingRange;
    if (proximity < 0.0f) proximity = 0.0f;
    if (proximity > 1.0f) proximity = 1.0f;

    const int maximum = isTRex
        ? kTRexShotInvestigationMaxTime
        : kShotInvestigationMaxTime;
    return kShotInvestigationMinTime
        + static_cast<int>((maximum - kShotInvestigationMinTime) * proximity);
}

inline bool ShotInvestigationComplete(int remainingTime, float targetDistanceSquared)
{
    return remainingTime <= 0
        || targetDistanceSquared <= kShotInvestigationArrivalRadius
            * kShotInvestigationArrivalRadius;
}

// The proximity-based reaction time gets shorter the farther the event was,
// which can expire before a slow creature has walked to the stored position.
// Extend it to cover the travel plus a search window, capped so no event
// reaction outlives the longest authored investigation.
inline constexpr int kShotInvestigationTravelCap = 60 * 1024;

inline int ShotInvestigationTimeForTravel(int baseTime, float distance,
                                          float travelSpeed)
{
    if (travelSpeed <= 0.0f)
        return baseTime;

    // Clamp before narrowing: an authored near-zero speed can make the
    // quotient exceed int even though the final reaction is capped.
    const float travel = distance / travelSpeed;
    if (travel >= static_cast<float>(kShotInvestigationTravelCap - kShotInvestigationMinTime))
        return kShotInvestigationTravelCap;
    if (!(travel >= 0.0f))
        return baseTime;
    const int travelTime = static_cast<int>(travel);
    const int needed = travelTime + kShotInvestigationMinTime;
    int result = baseTime > needed ? baseTime : needed;
    if (result > kShotInvestigationTravelCap)
        result = kShotInvestigationTravelCap;
    return result;
}

// Recent direct damage is a stronger stimulus than passive detection. Species
// fear and awareness rules are still evaluated after this range check, so a
// wounded creature keeps engaging beyond its normal acquisition range.
inline bool OutsideNormalAggressionRange(float distance, float aggressionRange,
                                         bool recentlyDamaged)
{
    return !recentlyDamaged && distance > aggressionRange;
}

inline bool OutsideNormalAggressionRangeSquared(float distanceSquared,
                                                float aggressionRange,
                                                bool recentlyDamaged)
{
    return !recentlyDamaged
        && distanceSquared > aggressionRange * aggressionRange;
}

// ===================== Object Detail (LOD) =====================

inline constexpr int kObjectDetailMin = 24;
inline constexpr int kObjectDetailMax = 96;
inline constexpr int kObjectDetailStep = 4;
inline constexpr int kObjectDetailDefault = 48;

inline int ClampObjectDetail(int value)
{
    if (value < kObjectDetailMin) return kObjectDetailMin;
    if (value > kObjectDetailMax) return kObjectDetailMax;
    return value;
}

// ===================== Field of View =====================

inline constexpr int kFovMin = 35;
inline constexpr int kFovMax = 90;
inline constexpr int kFovStep = 2;
inline constexpr int kFovDefault = 62;

inline float FovScaleFromDegrees(int fovDeg)
{
    return 1.0f / tanf((float)fovDeg * 3.1415926535f / 360.0f);
}

// ===================== Misc Constants =====================

inline constexpr int PMORPHTIME = 256;

inline constexpr int HiColor(int R, int G, int B)
{
    return ((R) << 10) + ((G) << 5) + (B);
}

inline constexpr int TCMAX = (128 << 16) - 62024;
inline constexpr int TCMIN = (000 << 16) + 62024;

inline constexpr int DINOINFO_MAX = 128;
inline constexpr int TROPHY_COUNT = 24;
inline constexpr int TROPHY2_COUNT = 128; //.sab

inline constexpr float pi = 3.1415926535f;

inline constexpr int MAX_HEALTH = 128000;

// ===================== Audio Backend =====================

enum AudioSystemEnum {
    AUDIO_DIRECTSOUND = 0,
    AUDIO_OPENALSOFT = 1,
    AUDIO_BACKEND_COUNT = 2
};

inline int NormalizeAudioBackend(int driver)
{
    // Legacy saved values:
    // 0..3 = software / DirectSound / A3D / EAX -> DirectSound
    // 4..5 = OpenAL / XAudio2 -> OpenAL Soft
    if (driver >= 0 && driver < AUDIO_OPENALSOFT)
        return AUDIO_DIRECTSOUND;
    if (driver >= AUDIO_OPENALSOFT && driver < AUDIO_BACKEND_COUNT)
        return AUDIO_OPENALSOFT;
    return AUDIO_DIRECTSOUND; // fallback
}

// ===================== AI Type Constants =====================

inline constexpr int AI_MOSH = 1;
inline constexpr int AI_GALL = 2;
inline constexpr int AI_DIMOR = 3;
inline constexpr int AI_PTERA = 4;
inline constexpr int AI_DIMET = 5;
inline constexpr int AI_PIG = 6;
inline constexpr int AI_HUNTDOG = 9;
inline constexpr int AI_PARA = 10;
inline constexpr int AI_ANKY = 11;
inline constexpr int AI_STEGO = 12;
inline constexpr int AI_ALLO = 13;
inline constexpr int AI_CHASM = 14;
inline constexpr int AI_VELO = 15;
inline constexpr int AI_SPINO = 16;
inline constexpr int AI_CERAT = 17;
inline constexpr int AI_TREX = 18;
inline constexpr int AI_PACH = 19;
inline constexpr int AI_BRONT = 20;
inline constexpr int AI_HOG = 21;
inline constexpr int AI_WOLF = 22;
inline constexpr int AI_RHINO = 23;
inline constexpr int AI_DEER = 24;
inline constexpr int AI_SMILO = 25;
inline constexpr int AI_MAMM = 26;
inline constexpr int AI_BEAR = 27;
inline constexpr int AI_TITAN = 28;
inline constexpr int AI_MICRO = 29;
inline constexpr int AI_BRACH = 30;
inline constexpr int AI_ICTH = 31;
inline constexpr int AI_FISH = 32;
inline constexpr int AI_MOSA = 33;
inline constexpr int AI_BRACHDANGER = 34;
inline constexpr int AI_LANDBRACH = 35;
inline constexpr int AI_POACHER = 8;

// ===================== Key Flags =====================

inline constexpr unsigned int kfForward  = 0x00000001;
inline constexpr unsigned int kfBackward = 0x00000002;
inline constexpr unsigned int kfLeft     = 0x00000004;
inline constexpr unsigned int kfRight    = 0x00000008;
inline constexpr unsigned int kfLookUp   = 0x00000010;
inline constexpr unsigned int kfLookDn   = 0x00000020;
inline constexpr unsigned int kfJump     = 0x00000040;
inline constexpr unsigned int kfDown     = 0x00000080;
inline constexpr unsigned int kfCall     = 0x00000100;
inline constexpr unsigned int kfSLeft    = 0x00001000;
inline constexpr unsigned int kfSRight   = 0x00002000;
inline constexpr unsigned int kfStrafe   = 0x00004000;

// ===================== Map Flags =====================

inline constexpr unsigned int fmWater   = 0x0080;
inline constexpr unsigned int fmWater2  = 0x8000;
inline constexpr unsigned int fmReverse = 0x0010;
inline constexpr unsigned int fmNOWAY   = 0x0020;
inline constexpr unsigned int fmWaterA  = 0x8080;

// ===================== Resource Type Constants =====================

inline constexpr int tresGround = 1;
inline constexpr int tresWater  = 2;
inline constexpr int tresModel  = 3;
inline constexpr int tresHunter = 4;
inline constexpr int tresChar   = 5;

// ===================== Surface Flags =====================

inline constexpr unsigned int sfDoubleSide  = 1;
inline constexpr unsigned int sfDarkBack    = 2;
inline constexpr unsigned int sfOpacity     = 4;
inline constexpr unsigned int sfTransparent = 8;
inline constexpr unsigned int sfMortal      = 0x0010;
inline constexpr unsigned int sfNeedVC      = 0x0080;
inline constexpr unsigned int sfDark        = 0x8000;

// ===================== Object Flags =====================

inline constexpr unsigned int ofPLACEWATER  = 1;
inline constexpr unsigned int ofPLACEGROUND = 2;
inline constexpr unsigned int ofPLACEUSER   = 4;
inline constexpr unsigned int ofCIRCLE     = 8;
inline constexpr unsigned int ofBOUND      = 16;
inline constexpr unsigned int ofNOBMP      = 32;
inline constexpr unsigned int ofNOLIGHT    = 64;
inline constexpr unsigned int ofDEFLIGHT   = 128;
inline constexpr unsigned int ofGRNDLIGHT  = 256;
inline constexpr unsigned int ofNOSOFT     = 512;
inline constexpr unsigned int ofNOSOFT2    = 1024;
inline constexpr unsigned int ofANIMATED   = 0x80000000;

// ===================== Surface Flags (Extended) =====================

inline constexpr unsigned int sfPhong   = 0x0030;
inline constexpr unsigned int sfEnvMap  = 0x0050;

// ===================== Character State Flags =====================

inline constexpr unsigned int csONWATER = 0x00010000;

// ===================== Hunt State =====================

inline constexpr int HUNT_EAT    = 0;
inline constexpr int HUNT_BREATH = 1;
inline constexpr int HUNT_FALL   = 2;
inline constexpr int HUNT_KILL   = 3;

// ===================== MIN / MAX Templates =====================

template <typename T, typename U>
inline constexpr std::common_type_t<T, U> MIN(T a, U b)
{
    return (a < b) ? a : b;
}

template <typename T, typename U>
inline constexpr std::common_type_t<T, U> MAX(T a, U b)
{
    return (a > b) ? a : b;
}
