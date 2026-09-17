#pragma once
// Include after Menu/Hunt.h, whose legacy declarations have no include guard.
#include "../Shared/LegacyProfile.h"

namespace MenuProfile {
// Field adapters only: runtime offsets and sizes are not file contracts.
template<class T>
inline LegacyProfile::Item FromItem(const T& t)
{
    LegacyProfile::Item v;
    v.type = t.m_CType;
    v.weapon = t.m_Weapon;
    v.phase = t.m_Phase;
    v.height = t.m_Height;
    v.weight = t.m_Weight;
    v.score = t.m_Score;
    v.date = t.m_Date;
    v.time = t.m_Time;
    v.scale = LegacyProfile::FloatBits(t.m_Scale);
    v.range = LegacyProfile::FloatBits(t.m_Range);
    v.reserved[0] = t.m_Reserved[0];
    v.reserved[1] = t.m_Reserved[1];
    v.reserved[2] = t.m_Reserved[2];
    v.reserved[3] = t.m_Reserved[3];
    return v;
}
template<class T>
inline void ToItem(const LegacyProfile::Item& v, T& t)
{
    t.m_CType = v.type;
    t.m_Weapon = v.weapon;
    t.m_Phase = v.phase;
    t.m_Height = v.height;
    t.m_Weight = v.weight;
    t.m_Score = v.score;
    t.m_Date = v.date;
    t.m_Time = v.time;
    LegacyProfile::SetFloat(t.m_Scale, v.scale);
    LegacyProfile::SetFloat(t.m_Range, v.range);
    t.m_Reserved[0] = v.reserved[0];
    t.m_Reserved[1] = v.reserved[1];
    t.m_Reserved[2] = v.reserved[2];
    t.m_Reserved[3] = v.reserved[3];
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
inline LegacyProfile::Prefix FromRuntime(const Profile& p)
{
    LegacyProfile::Prefix v;
    std::memcpy(v.header.name.data(), p.Name, LegacyProfile::NameSize);
    v.header.registration = p.RegNumber; v.header.score = p.Score; v.header.rank = p.Rank;
    v.last = FromStats(p.Last); v.total = FromStats(p.Total);
    for (unsigned i = 0; i < 24; ++i) v.items[i] = FromItem(p.Body[i]);
    return v;
}
inline void ToRuntime(const LegacyProfile::Prefix& v, Profile& p)
{
    std::memcpy(p.Name, v.header.name.data(), LegacyProfile::NameSize);
    p.RegNumber = v.header.registration; p.Score = v.header.score; p.Rank = v.header.rank;
    ToStats(v.last, p.Last); ToStats(v.total, p.Total);
    for (unsigned i = 0; i < 24; ++i) ToItem(v.items[i], p.Body[i]);
}
inline LegacyProfile::Options CaptureOptions(const Options& o)
{
    LegacyProfile::Options v;
    v.aggression = o.Aggression;
    v.density = o.Density;
    v.sensitivity = o.Sensitivity;
    v.resolution = o.Resolution;
    v.fog = o.Fog;
    v.textures = o.Textures;
    v.viewRange = o.ViewRange;
    v.shadows = o.Shadows;
    v.mouseSensitivity = o.MouseSensitivity;
    v.brightness = o.Brightness;
    v.mouseInvert = o.MouseInvert;
    v.scent = o.ScentMode;
    v.camo = o.CamoMode;
    v.radar = o.RadarMode;
    v.tranq = o.TranqMode;
    v.alphaColorKey = o.AlphaColorKey;
    v.system = o.OptSys;
    v.sound = o.SoundAPI;
    v.renderer = o.RenderAPI;
    v.keys[0] = o.KeyMap.fkForward;
    v.keys[1] = o.KeyMap.fkBackward;
    v.keys[2] = o.KeyMap.fkReload;
    v.keys[3] = o.KeyMap.fkResupply;
    v.keys[4] = o.KeyMap.fkHoldBreath;
    v.keys[5] = o.KeyMap.fkFiringMode;
    v.keys[6] = o.KeyMap.fkFire;
    v.keys[7] = o.KeyMap.fkShow;
    v.keys[8] = o.KeyMap.fkSLeft;
    v.keys[9] = o.KeyMap.fkSRight;
    v.keys[10] = o.KeyMap.fkStrafe;
    v.keys[11] = o.KeyMap.fkJump;
    v.keys[12] = o.KeyMap.fkRun;
    v.keys[13] = o.KeyMap.fkCrouch;
    v.keys[14] = o.KeyMap.fkCall;
    v.keys[15] = o.KeyMap.fkCCall;
    v.keys[16] = o.KeyMap.fkBinoc;
    return v;
}
inline void ApplyOptions(const LegacyProfile::Options& v, Options& o)
{
    o.Aggression = v.aggression;
    o.Density = v.density;
    o.Sensitivity = v.sensitivity;
    o.Resolution = v.resolution;
    o.Fog = v.fog != 0;
    o.Textures = v.textures;
    o.ViewRange = v.viewRange;
    o.Shadows = v.shadows != 0;
    o.MouseSensitivity = v.mouseSensitivity;
    o.Brightness = v.brightness;
    o.MouseInvert = v.mouseInvert != 0;
    o.ScentMode = v.scent != 0;
    o.CamoMode = v.camo != 0;
    o.RadarMode = v.radar != 0;
    o.TranqMode = v.tranq != 0;
    o.AlphaColorKey = v.alphaColorKey;
    o.OptSys = v.system;
    o.SoundAPI = v.sound;
    o.RenderAPI = v.renderer;
    o.KeyMap.fkForward = v.keys[0];
    o.KeyMap.fkBackward = v.keys[1];
    o.KeyMap.fkReload = v.keys[2];
    o.KeyMap.fkResupply = v.keys[3];
    o.KeyMap.fkHoldBreath = v.keys[4];
    o.KeyMap.fkFiringMode = v.keys[5];
    o.KeyMap.fkFire = v.keys[6];
    o.KeyMap.fkShow = v.keys[7];
    o.KeyMap.fkSLeft = v.keys[8];
    o.KeyMap.fkSRight = v.keys[9];
    o.KeyMap.fkStrafe = v.keys[10];
    o.KeyMap.fkJump = v.keys[11];
    o.KeyMap.fkRun = v.keys[12];
    o.KeyMap.fkCrouch = v.keys[13];
    o.KeyMap.fkCall = v.keys[14];
    o.KeyMap.fkCCall = v.keys[15];
    o.KeyMap.fkBinoc = v.keys[16];
}
inline void UpdateRank(Profile& p)
{
    p.Rank = RANK_BEGINNER;
    if (p.Score >= 100) p.Rank = RANK_ADVANCED;
    if (p.Score >= 300) p.Rank = RANK_MASTER;
    if (p.Score >= 10000) p.Rank = 1000;
}
inline bool LoadProfile(const std::uint8_t* bytes, std::size_t size, Profile& p, Options& o)
{
    LegacyProfile::Save v;
    if (!LegacyProfile::DecodeSave(bytes, size, v)) return false;
    ToRuntime(v.profile, p);
    ApplyOptions(v.options, o);
    return true;
}
} // namespace MenuProfile
