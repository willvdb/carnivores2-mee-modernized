#pragma once
#include "../Hunt.h"
#include "../../Shared/LegacyProfile.h"

namespace EngineProfile {
// Field adapters only: runtime offsets and sizes are not file contracts.
template<class T>
inline LegacyProfile::Item FromItem(const T& t)
{
    LegacyProfile::Item v;
    v.type = t.ctype;
    v.weapon = t.weapon;
    v.phase = t.phase;
    v.height = t.height;
    v.weight = t.weight;
    v.score = t.score;
    v.date = t.date;
    v.time = t.time;
    v.scale = LegacyProfile::FloatBits(t.scale);
    v.range = LegacyProfile::FloatBits(t.range);
    v.reserved[0] = t.r1;
    v.reserved[1] = t.r2;
    v.reserved[2] = t.r3;
    v.reserved[3] = t.r4;
    return v;
}
template<class T>
inline void ToItem(const LegacyProfile::Item& v, T& t)
{
    t.ctype = v.type;
    t.weapon = v.weapon;
    t.phase = v.phase;
    t.height = v.height;
    t.weight = v.weight;
    t.score = v.score;
    t.date = v.date;
    t.time = v.time;
    LegacyProfile::SetFloat(t.scale, v.scale);
    LegacyProfile::SetFloat(t.range, v.range);
    t.r1 = v.reserved[0];
    t.r2 = v.reserved[1];
    t.r3 = v.reserved[2];
    t.r4 = v.reserved[3];
}
template<class T>
inline LegacyProfile::Stats FromStats(const T& t)
{
    return {t.smade, t.success, LegacyProfile::FloatBits(t.path), LegacyProfile::FloatBits(t.time)};
}
template<class T>
inline void ToStats(const LegacyProfile::Stats& v, T& t)
{
    t.smade = v.shots; t.success = v.hits;
    LegacyProfile::SetFloat(t.path, v.path); LegacyProfile::SetFloat(t.time, v.time);
}
inline LegacyProfile::Prefix FromRuntime(const TTrophyRoom& p)
{
    LegacyProfile::Prefix v;
    std::memcpy(v.header.name.data(), p.PlayerName, LegacyProfile::NameSize);
    v.header.registration = p.RegNumber; v.header.score = p.Score; v.header.rank = p.Rank;
    v.last = FromStats(p.Last); v.total = FromStats(p.Total);
    for (unsigned i = 0; i < 24; ++i) v.items[i] = FromItem(p.Body[i]);
    return v;
}
inline void ToRuntime(const LegacyProfile::Prefix& v, TTrophyRoom& p)
{
    std::memcpy(p.PlayerName, v.header.name.data(), LegacyProfile::NameSize);
    p.RegNumber = v.header.registration; p.Score = v.header.score; p.Rank = v.header.rank;
    ToStats(v.last, p.Last); ToStats(v.total, p.Total);
    for (unsigned i = 0; i < 24; ++i) ToItem(v.items[i], p.Body[i]);
}
inline LegacyProfile::Options CaptureOptions()
{
    LegacyProfile::Options v;
    v.aggression = OptAgres;
    v.density = OptDens;
    v.sensitivity = OptSens;
    v.resolution = OptRes;
    v.fog = FOGENABLE;
    v.textures = OptText;
    v.viewRange = OptViewR;
    v.shadows = SHADOWS3D;
    v.mouseSensitivity = OptMsSens;
    v.brightness = OptBrightness;
    v.mouseInvert = REVERSEMS;
    v.scent = ScentMode;
    v.camo = CamoMode;
    v.radar = RadarMode;
    v.tranq = Tranq;
    v.alphaColorKey = OPT_ALPHA_COLORKEY;
    v.system = OptSys;
    v.sound = OptSound;
    v.renderer = OptRender;
    v.keys[0] = KeyMap.fkForward;
    v.keys[1] = KeyMap.fkBackward;
    v.keys[2] = KeyMap.fkReload;
    v.keys[3] = KeyMap.fkResupply;
    v.keys[4] = KeyMap.fkHoldBreath;
    v.keys[5] = KeyMap.fkFiringMode;
    v.keys[6] = KeyMap.fkFire;
    v.keys[7] = KeyMap.fkShow;
    v.keys[8] = KeyMap.fkSLeft;
    v.keys[9] = KeyMap.fkSRight;
    v.keys[10] = KeyMap.fkStrafe;
    v.keys[11] = KeyMap.fkJump;
    v.keys[12] = KeyMap.fkRun;
    v.keys[13] = KeyMap.fkCrouch;
    v.keys[14] = KeyMap.fkCall;
    v.keys[15] = KeyMap.fkCCall;
    v.keys[16] = KeyMap.fkBinoc;
    return v;
}
inline void ApplyOptions(const LegacyProfile::Options& v, bool completeKeys)
{
    OptAgres = v.aggression;
    OptDens = v.density;
    OptSens = v.sensitivity;
    OptRes = v.resolution;
    FOGENABLE = v.fog;
    OptText = v.textures;
    OptViewR = v.viewRange;
    SHADOWS3D = v.shadows;
    OptMsSens = v.mouseSensitivity;
    OptBrightness = v.brightness;
    REVERSEMS = v.mouseInvert;
    OPT_ALPHA_COLORKEY = v.alphaColorKey;
    OptSys = v.system;
    OptSound = v.sound;
    OptRender = v.renderer;
    KeyMap.fkForward = v.keys[0];
    KeyMap.fkBackward = v.keys[1];
    KeyMap.fkReload = v.keys[2];
    KeyMap.fkResupply = v.keys[3];
    KeyMap.fkHoldBreath = v.keys[4];
    KeyMap.fkFiringMode = v.keys[5];
    KeyMap.fkFire = v.keys[6];
    KeyMap.fkShow = v.keys[7];
    KeyMap.fkSLeft = v.keys[8];
    KeyMap.fkSRight = v.keys[9];
    KeyMap.fkStrafe = v.keys[10];
    KeyMap.fkJump = v.keys[11];
    KeyMap.fkRun = v.keys[12];
    KeyMap.fkCrouch = v.keys[13];
    KeyMap.fkCall = v.keys[14];
    KeyMap.fkCCall = v.keys[15];
    KeyMap.fkBinoc = v.keys[16];
    // Equipment is supplied by the session, never restored from the save.
    if (Multiplayer) OptDens = 128;
    OptViewR = ClampViewOpt(OptViewR);
    if (completeKeys) OptSound = NormalizeAudioBackend(OptSound);
}
inline LegacyProfile::Room FromRuntime(const TTrophyRoom2& p)
{
    LegacyProfile::Room v;
    v.version = p.versionID; v.highScore = p.survivalHighScore;
    for (unsigned i = 0; i < 128; ++i) v.items[i] = FromItem(p.Body[i]);
    return v;
}
inline void ToRuntime(const LegacyProfile::Room& v, TTrophyRoom2& p)
{
    p.versionID = v.version; p.survivalHighScore = v.highScore;
    for (unsigned i = 0; i < 128; ++i) ToItem(v.items[i], p.Body[i]);
}
inline void UpdateRank(TTrophyRoom& p)
{
    p.Rank = 0;
    if (p.Score >= 100) p.Rank = 1;
    if (p.Score >= 300) p.Rank = 2;
}
// The loaded version is deliberately regenerated by the engine.
inline bool LoadRoom(const std::uint8_t* bytes, std::size_t size, TTrophyRoom2& p)
{
    LegacyProfile::Room v;
    if (!LegacyProfile::DecodeRoom(bytes, size, v)) return false;
    ToRuntime(v, p);
    p.versionID = MODDERS_EDITION_VERSION_ID;
    return true;
}
inline bool LoadProfile(const std::uint8_t* bytes, std::size_t size, TTrophyRoom& p)
{
    LegacyProfile::Prefix v;
    if (!LegacyProfile::DecodePrefix(bytes, size, v)) return false;
    const int registration = p.RegNumber;
    ToRuntime(v, p);
    p.RegNumber = registration;
    auto options = CaptureOptions();
    if (size < 1544) options.viewRange = kViewOptDefault;
    LegacyProfile::DecodeAvailableOptions(bytes + LegacyProfile::PrefixSize,
                                         size - LegacyProfile::PrefixSize, options);
    ApplyOptions(options, size >= LegacyProfile::KeyEnd);
    return true;
}
} // namespace EngineProfile
