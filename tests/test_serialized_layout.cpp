#include <gtest/gtest.h>
#include "Hunt.h"
#include "serialized_layout_checks.h"

// .CAR/.3DF model records, read before runtime UV conversion.
RECORD_SIZE(TPoint3d, 16);
RECORD_OFFSET(TPoint3d, x, 0);
RECORD_OFFSET(TPoint3d, y, 4);
RECORD_OFFSET(TPoint3d, z, 8);
RECORD_OFFSET(TPoint3d, owner, 12);
RECORD_OFFSET(TPoint3d, hide, 14);
RECORD_SIZE(TFace, 64);
RECORD_OFFSET(TFace, v1, 0);
RECORD_OFFSET(TFace, v2, 4);
RECORD_OFFSET(TFace, v3, 8);
RECORD_OFFSET(TFace, tax, 12);
RECORD_OFFSET(TFace, tbx, 16);
RECORD_OFFSET(TFace, tcx, 20);
RECORD_OFFSET(TFace, tay, 24);
RECORD_OFFSET(TFace, tby, 28);
RECORD_OFFSET(TFace, tcy, 32);
RECORD_OFFSET(TFace, Flags, 36);
RECORD_OFFSET(TFace, DMask, 38);
RECORD_OFFSET(TFace, Distant, 40);
RECORD_OFFSET(TFace, Next, 44);
RECORD_OFFSET(TFace, group, 48);
RECORD_OFFSET(TFace, reserv, 52);
RECORD_SIZE(TObj, 48);
RECORD_OFFSET(TObj, OName, 0);
RECORD_OFFSET(TObj, ox, 32);
RECORD_OFFSET(TObj, oy, 36);
RECORD_OFFSET(TObj, oz, 40);
RECORD_OFFSET(TObj, owner, 44);
RECORD_OFFSET(TObj, hide, 46);

// .rsc records (not TObject, TAmbient, TSFX or TEXTURE runtime objects).
RECORD_SIZE(TObjInfo, 64);
RECORD_OFFSET(TObjInfo, Radius, 0);
RECORD_OFFSET(TObjInfo, YLo, 4);
RECORD_OFFSET(TObjInfo, YHi, 8);
RECORD_OFFSET(TObjInfo, linelenght, 12);
RECORD_OFFSET(TObjInfo, lintensity, 16);
RECORD_OFFSET(TObjInfo, circlerad, 20);
RECORD_OFFSET(TObjInfo, cintensity, 24);
RECORD_OFFSET(TObjInfo, flags, 28);
RECORD_OFFSET(TObjInfo, GrRad, 32);
RECORD_OFFSET(TObjInfo, DefLight, 36);
RECORD_OFFSET(TObjInfo, LastAniTime, 40);
RECORD_OFFSET(TObjInfo, BoundR, 44);
RECORD_OFFSET(TObjInfo, res, 48);
RECORD_SIZE(TFogEntity, 20);
RECORD_OFFSET(TFogEntity, fogRGB, 0);
RECORD_OFFSET(TFogEntity, YBegin, 4);
RECORD_OFFSET(TFogEntity, Mortal, 8);
RECORD_OFFSET(TFogEntity, Transp, 12);
RECORD_OFFSET(TFogEntity, FLimit, 16);
RECORD_SIZE(TRD, 16);
RECORD_OFFSET(TRD, RNumber, 0);
RECORD_OFFSET(TRD, RVolume, 4);
RECORD_OFFSET(TRD, RFreq, 8);
RECORD_OFFSET(TRD, REnvir, 12);
RECORD_OFFSET(TRD, Flags, 14);
RECORD_SIZE(TWaterEntity, 16);
RECORD_OFFSET(TWaterEntity, tindex, 0);
RECORD_OFFSET(TWaterEntity, wlevel, 4);
RECORD_OFFSET(TWaterEntity, transp, 8);
RECORD_OFFSET(TWaterEntity, fogRGB, 12);

static_assert(SerializedLayout::CheckProfile<TTrophyRoom, TStats, TTrophyItem, decltype(KeyMap)>());
RECORD_OFFSET(TTrophyRoom, PlayerName, 0);
#define TROPHY_OFFSETS(type) \
    RECORD_OFFSET(type, ctype, 0); \
    RECORD_OFFSET(type, weapon, 4); \
    RECORD_OFFSET(type, phase, 8); \
    RECORD_OFFSET(type, height, 12); \
    RECORD_OFFSET(type, weight, 16); \
    RECORD_OFFSET(type, score, 20); \
    RECORD_OFFSET(type, date, 24); \
    RECORD_OFFSET(type, time, 28); \
    RECORD_OFFSET(type, scale, 32); \
    RECORD_OFFSET(type, range, 36); \
    RECORD_OFFSET(type, r1, 40); \
    RECORD_OFFSET(type, r2, 44); \
    RECORD_OFFSET(type, r3, 48); \
    RECORD_OFFSET(type, r4, 52)
TROPHY_OFFSETS(TTrophyItem);
TROPHY_OFFSETS(TTrophyItem2);
#undef TROPHY_OFFSETS
RECORD_SIZE(TTrophyItem2, 56);
RECORD_SIZE(TTrophyRoom2, 7176);
RECORD_OFFSET(TTrophyRoom2, versionID, 0);
RECORD_OFFSET(TTrophyRoom2, survivalHighScore, 4);
RECORD_OFFSET(TTrophyRoom2, Body, 8);

// Bulk map planes and samples retain their file widths even on x64.
static_assert(sizeof(HMap) == 1048576 && sizeof(WMap) == 1048576);
static_assert(sizeof(HMapO) == 1048576 && sizeof(OMap) == 1048576);
static_assert(sizeof(LMap) == 1048576);
static_assert(sizeof(TMap1) == 2097152 && sizeof(TMap2) == 2097152);
static_assert(sizeof(FMap) == 2097152);
static_assert(sizeof(FogsMap) == 262144 && sizeof(AmbMap) == 262144);
static_assert(sizeof(short[3]) == 6); // animation XYZ triplet, not sizeof(TAni)
static_assert(sizeof(TEXTURE::DataA) == 32768); // only base mip is read
static_assert(sizeof(TCharacterInfo::Anifx) == 256);
RECORD_SIZE(BITMAPFILEHEADER, 14);
RECORD_OFFSET(BITMAPFILEHEADER, bfOffBits, 10);
RECORD_SIZE(BITMAPINFOHEADER, 40);
RECORD_OFFSET(BITMAPINFOHEADER, biWidth, 4);
RECORD_OFFSET(BITMAPINFOHEADER, biBitCount, 14);

TEST(SerializedLayout, EngineProfileMatchesLegacyBytes)
{
    const auto profile = SerializedLayout::CheckProfileBytes<TTrophyRoom>();
    EXPECT_STREQ(profile.PlayerName, "T");
    EXPECT_EQ(profile.Body[0].ctype, 10);
    EXPECT_FLOAT_EQ(profile.Body[0].scale, 2.0f);
    EXPECT_EQ(profile.Body[23].r4, 0x76543210);
}

TEST(SerializedLayout, FaceUVsAreIntegerBitsBeforeConversion)
{
    std::array<std::uint8_t, 64> bytes{};
    SerializedLayout::Put32(bytes, 0, 1);
    SerializedLayout::Put32(bytes, 4, 2);
    SerializedLayout::Put32(bytes, 8, 3);
    for (unsigned i = 0; i < 6; ++i)
        SerializedLayout::Put32(bytes, 12 + 4 * i, 16 + i);
    bytes[36] = 0x34;
    bytes[37] = 0x12;
    bytes[63] = 0x5a;
    TFace face{};
    std::memcpy(&face, bytes.data(), bytes.size());
    EXPECT_EQ(face.v1, 1);
    EXPECT_EQ(face.v2, 2);
    EXPECT_EQ(face.v3, 3);
    // GL's float members are raw integer storage until ModelLoader converts
    // them. Do not "fix" the file by writing float UVs or widening indices.
    const auto rawUV = [](const auto& uv) {
        std::int32_t value;
        std::memcpy(&value, &uv, sizeof(value));
        return value;
    };
    EXPECT_EQ(rawUV(face.tax), 16);
    EXPECT_EQ(rawUV(face.tbx), 17);
    EXPECT_EQ(rawUV(face.tcx), 18);
    EXPECT_EQ(rawUV(face.tay), 19);
    EXPECT_EQ(rawUV(face.tby), 20);
    EXPECT_EQ(rawUV(face.tcy), 21);
    EXPECT_EQ(face.Flags, 0x1234);
    EXPECT_EQ(face.reserv[11], 0x5a);
}

TEST(SerializedLayout, VertexAndAnimationUseLegacyScalarEncoding)
{
    const std::array<std::uint8_t, 16> bytes = {
        0, 0, 0x80, 0x3f, 0, 0, 0x20, 0xc0, 0, 0, 0, 0x3f, 0xfe, 0xff, 1, 0
    };
    TPoint3d vertex{};
    std::memcpy(&vertex, bytes.data(), bytes.size());
    EXPECT_FLOAT_EQ(vertex.x, 1.0f);
    EXPECT_FLOAT_EQ(vertex.y, -2.5f);
    EXPECT_FLOAT_EQ(vertex.z, 0.5f);
    EXPECT_EQ(vertex.owner, -2);
    EXPECT_EQ(vertex.hide, 1);
    const std::array<std::uint8_t, 6> samples = {0, 0x80, 0xff, 0x7f, 0xff, 0xff};
    short xyz[3];
    std::memcpy(xyz, samples.data(), samples.size());
    EXPECT_EQ(xyz[0], -32768);
    EXPECT_EQ(xyz[1], 32767);
    EXPECT_EQ(xyz[2], -1);
}

#include "legacy_profile_fixtures.h"
TEST(SerializedLayout, CompleteLegacyProfileFixture)
{
    ProfileGolden::CheckLegacySave<TTrophyRoom, decltype(KeyMap)>();
}

TEST(SerializedLayout, CompleteLegacyTrophyRoomFixture)
{
    const auto bytes = ProfileGolden::Room();
    TTrophyRoom2 room{};
    std::memcpy(&room, bytes.data(), bytes.size());
    EXPECT_EQ(room.versionID, 0x12345678);
    EXPECT_EQ(room.survivalHighScore, 98765);
    EXPECT_EQ(room.Body[0].ctype, 1000);
    EXPECT_EQ(room.Body[127].ctype, 13700);
    EXPECT_EQ(room.Body[127].phase, -2);
    EXPECT_EQ(room.Body[127].r4, 13713);
    EXPECT_EQ(std::memcmp(&room, bytes.data(), bytes.size()), 0);
}

#include "../Hunt/Game/ProfileSerialization.h"

// Only the option globals used by the production adapter.
decltype(OptAgres) OptAgres{};
decltype(OptDens) OptDens{};
decltype(OptSens) OptSens{};
decltype(OptRes) OptRes{};
decltype(FOGENABLE) FOGENABLE{};
decltype(OptText) OptText{};
decltype(OptViewR) OptViewR{};
decltype(SHADOWS3D) SHADOWS3D{};
decltype(OptMsSens) OptMsSens{};
decltype(OptBrightness) OptBrightness{};
decltype(REVERSEMS) REVERSEMS{};
decltype(ScentMode) ScentMode{};
decltype(CamoMode) CamoMode{};
decltype(RadarMode) RadarMode{};
decltype(Tranq) Tranq{};
decltype(OPT_ALPHA_COLORKEY) OPT_ALPHA_COLORKEY{};
decltype(OptSys) OptSys{};
decltype(OptSound) OptSound{};
decltype(OptRender) OptRender{};
decltype(KeyMap) KeyMap{};
decltype(Multiplayer) Multiplayer{};

TEST(EngineProfile, PrefixAndRoomAdaptersMatchLegacyRuntime)
{
    auto b = ProfileGolden::Prefix();
    LegacyProfile::Prefix v;
    ASSERT_TRUE(LegacyProfile::DecodePrefix(b.data(), b.size(), v));
    TTrophyRoom p;
    EngineProfile::ToRuntime(v, p);
    // Independent legacy-layout oracle; the adapter itself does not use sizeof.
    EXPECT_EQ(std::memcmp(&p, b.data(), b.size()), 0);
    EXPECT_EQ(LegacyProfile::EncodePrefix(EngineProfile::FromRuntime(p)), b);
    auto rb = ProfileGolden::Room();
    LegacyProfile::Room r;
    ASSERT_TRUE(LegacyProfile::DecodeRoom(rb.data(), rb.size(), r));
    TTrophyRoom2 room;
    EngineProfile::ToRuntime(r, room);
    EXPECT_EQ(std::memcmp(&room, rb.data(), rb.size()), 0);
    EXPECT_EQ(LegacyProfile::EncodeRoom(EngineProfile::FromRuntime(room)), rb);
    ASSERT_TRUE(EngineProfile::LoadRoom(rb.data(), rb.size(), room));
    EXPECT_EQ(room.versionID, MODDERS_EDITION_VERSION_ID);
    ProfileGolden::Put32(rb, 0, MODDERS_EDITION_VERSION_ID);
    EXPECT_EQ(LegacyProfile::EncodeRoom(EngineProfile::FromRuntime(room)), rb);
}
TEST(EngineProfile, CompleteSavePreservesIgnoredEquipmentAndStoredBoolWidths)
{
    auto b = ProfileGolden::Save();
    TTrophyRoom p{}; p.RegNumber = 3;
    ScentMode = 17; CamoMode = 18; RadarMode = 19; Tranq = 20;
    Multiplayer = FALSE;
    ASSERT_TRUE(EngineProfile::LoadProfile(b.data(), b.size(), p));
    EXPECT_EQ(p.RegNumber, 3);
    EXPECT_EQ(OptAgres, 30); EXPECT_EQ(OptDens, 31); EXPECT_EQ(OptSens, 32);
    EXPECT_EQ(OptRes, 5); EXPECT_EQ(OptText, 35); EXPECT_EQ(OptViewR, 36);
    EXPECT_EQ(OptMsSens, 38); EXPECT_EQ(OptBrightness, 39); EXPECT_EQ(REVERSEMS, 0);
    EXPECT_EQ(OPT_ALPHA_COLORKEY, 62); EXPECT_EQ(OptSys, 63); EXPECT_EQ(OptRender, 1);
    EXPECT_EQ(ScentMode, 17); EXPECT_EQ(CamoMode, 18);
    EXPECT_EQ(RadarMode, 19); EXPECT_EQ(Tranq, 20);
    EXPECT_EQ(FOGENABLE, -1); EXPECT_EQ(SHADOWS3D, 2);
    EXPECT_EQ(KeyMap.fkForward, 40); EXPECT_EQ(KeyMap.fkBinoc, 56);
    EXPECT_EQ(std::memcmp(&KeyMap, b.data() + 1556, 68), 0);
    EXPECT_EQ(OptSound, 0); // engine's existing normalization of stored 2
    ProfileGolden::Put32(b, 128, 3);
    for (unsigned i = 0; i < 4; ++i) ProfileGolden::Put32(b, 1628 + 4*i, 17+i);
    ProfileGolden::Put32(b, 1652, 0);
    EXPECT_EQ(LegacyProfile::EncodeSave({EngineProfile::FromRuntime(p), EngineProfile::CaptureOptions()}), b);
    Multiplayer = TRUE;
    ASSERT_TRUE(EngineProfile::LoadProfile(b.data(), b.size(), p));
    EXPECT_EQ(OptDens, 128);
    Multiplayer = FALSE;
}
TEST(EngineProfile, ShortProfileFallbackAndMalformedPrefix)
{
    auto b = ProfileGolden::Save();
    TTrophyRoom p{}; p.RegNumber = 7;
    KeyMap.fkForward = 'W'; KeyMap.fkBinoc = 'B';
    OptSound = 99; REVERSEMS = 7;
    ASSERT_TRUE(EngineProfile::LoadProfile(b.data(), 1623, p));
    EXPECT_EQ(KeyMap.fkForward, 'W'); EXPECT_EQ(KeyMap.fkBinoc, 'B');
    EXPECT_EQ(OptAgres, 30); EXPECT_EQ(OptBrightness, 39);
    EXPECT_EQ(REVERSEMS, 7); EXPECT_EQ(OptSound, 99);
    ASSERT_TRUE(EngineProfile::LoadProfile(b.data(), 1516, p));
    EXPECT_EQ(OptViewR, kViewOptDefault);
    const auto before = LegacyProfile::EncodePrefix(EngineProfile::FromRuntime(p));
    EXPECT_FALSE(EngineProfile::LoadProfile(b.data(), 1515, p));
    EXPECT_EQ(LegacyProfile::EncodePrefix(EngineProfile::FromRuntime(p)), before);
    auto roomBytes = ProfileGolden::Room(); TTrophyRoom2 room{};
    room.versionID = -5;
    EXPECT_FALSE(EngineProfile::LoadRoom(roomBytes.data(), 7175, room));
    EXPECT_EQ(room.versionID, -5);
}

TEST(EngineProfile, SaveRegeneratesRankAtLegacyThresholds)
{
    for (auto score : {-1, 0, 99, 100, 299, 300, 9999, 10000}) {
        TTrophyRoom p{}; p.Score = score; p.Rank = -99;
        EngineProfile::UpdateRank(p);
        EXPECT_EQ(p.Rank, score >= 300 ? 2 : score >= 100 ? 1 : 0);
    }
}
