// test_menu_resource_entry.cpp -- production menu _RES.TXT/_MENU.TXT reader contracts
//
// The menu prefers HUNTDAT/_MENU.TXT and falls back to the legacy _RES.TXT.
// These tests drive the real ReadCharacters()/ReadWeapons()/ReadPrices()
// readers with the line shapes the shipped files use, so a field that is
// strict-parsed can never be reached by a line whose assignment key is
// something else (the stock `file = 'models/main_hunt/...'` line and a
// decimal `health = 13.5` are both represented here).

#include <gtest/gtest.h>

#include <cstdio>
#include <exception>

#include "Hunt.h"
#include "Loaders/LoadDiagnostics.h"

// Production entry points defined in Menu/Resources.cpp.
void ReadCharacters(FILE* stream);
void ReadWeapons(FILE* stream);
void ReadPrices(FILE* stream);

namespace {

class TempScript final
{
public:
    explicit TempScript(const char* text)
        : stream(tmpfile())
    {
        if (stream) {
            fputs(text, stream);
            rewind(stream);
        }
    }

    ~TempScript()
    {
        if (stream)
            fclose(stream);
    }

    FILE* stream;
};

void ResetMenuState()
{
    g_DinoInfo.clear();
    g_WeapInfo.clear();
    g_AreaInfo.clear();
    g_AccessoryPrices.clear();
    g_AccessoryScoreMods.clear();
    g_StartCredits = 0;
}

}  // namespace

TEST(MenuResourceEntry, LegacyResCharactersBlockParsesStockLineShapes)
{
    ResetMenuState();
    // ReadCharacters is called after the caller consumed the `characters {`
    // line, so the stream starts with the first dino block opener.
    TempScript script(
        "{\n"
        " name    = 'Massospondylus'\n"
        " file    = 'models/main_hunt/para.car'\n"
        " ai      = 10\n"
        " health  = 13.5\n"
        " basescore = 9\n"
        " smellK  = 0.8\n"
        " hearK   = 0.75\n"
        " lookK   = 0.5\n"
        " scale0  = 980\n"
        "}\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    // The name and path values contain numeric field keys as substrings
    // ("Massospondylus" -> mass, "main_hunt" -> ai); health is a decimal
    // literal. None of them may be read as a numeric field.
    EXPECT_NO_THROW(ReadCharacters(script.stream));

    ASSERT_EQ(g_DinoInfo.size(), 1u);
    const DinoInfo& dino = g_DinoInfo[0];
    EXPECT_EQ(dino.m_Name, "Massospondylus");
    EXPECT_EQ(dino.m_FilePath, "models/main_hunt/para.car");
    EXPECT_EQ(dino.m_AI, 10);
    EXPECT_EQ(dino.m_BaseHealth, 13);  // legacy atoi truncation of 13.5
    EXPECT_EQ(dino.m_BaseScore, 9);
    EXPECT_FLOAT_EQ(dino.m_SmellK, 0.8f);
    EXPECT_FLOAT_EQ(dino.m_HearK, 0.75f);
    EXPECT_FLOAT_EQ(dino.m_LookK, 0.5f);
    EXPECT_EQ(dino.m_BaseScale, 980);
}

TEST(MenuResourceEntry, MenuTxtAcceptsBothSensitivityKeySpellings)
{
    ResetMenuState();
    // _MENU.TXT uses the bare keys plus capital-K variants (Velociraptor),
    // while _RES.TXT uses only the capital-K variants.
    TempScript script(
        "{\n"
        " name = 'Velociraptor'\n"
        " ai = 15\n"
        " smell = 1.0\n"
        " hearK = 0.7\n"
        " lookK = 0.5\n"
        "}\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    EXPECT_NO_THROW(ReadCharacters(script.stream));

    ASSERT_EQ(g_DinoInfo.size(), 1u);
    const DinoInfo& dino = g_DinoInfo[0];
    EXPECT_EQ(dino.m_AI, 15);
    EXPECT_FLOAT_EQ(dino.m_SmellK, 1.0f);
    EXPECT_FLOAT_EQ(dino.m_HearK, 0.7f);
    EXPECT_FLOAT_EQ(dino.m_LookK, 0.5f);
}

TEST(MenuResourceEntry, LegacyHealthAcceptsLongSuffix)
{
    ResetMenuState();
    TempScript script(
        "{\n"
        " name = 'Dsungaripterus'\n"
        " ai = 3\n"
        " health = 1l\n"
        "}\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    EXPECT_NO_THROW(ReadCharacters(script.stream));
    EXPECT_TRUE(g_DinoInfo.empty());  // Ambient AI is parsed but not listed.
}

TEST(MenuResourceEntry, WeaponValuesDoNotSelectOtherFields)
{
    ResetMenuState();
    TempScript script(
        "{\n"
        " name = 'Pirate Rifle'\n"
        " power = 5\n"
        " prec = 1.8\n"
        " loud = 1.2\n"
        " rate = 0.45\n"
        " file = 'models/pirateship/rifle_fall.car'\n"
        " pic1 = 'ammo/bullet1.tga'\n"
        "}\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    // 'Pirate' contains "rate" and the path contains "fall"; both keys are
    // strict-parsed fields.
    EXPECT_NO_THROW(ReadWeapons(script.stream));

    ASSERT_EQ(g_WeapInfo.size(), 1u);
    const WeapInfo& weapon = g_WeapInfo[0];
    EXPECT_EQ(weapon.m_Name, "Pirate Rifle");
    EXPECT_FLOAT_EQ(weapon.m_Power, 5.0f);
    EXPECT_FLOAT_EQ(weapon.m_Rate, 0.45f);
    EXPECT_EQ(weapon.m_FilePath, "models/pirateship/rifle_fall.car");
    EXPECT_EQ(weapon.m_BulletFilePath, "ammo/bullet1.tga");
}

TEST(MenuResourceEntry, PricesBlockKeepsLegacyKeys)
{
    ResetMenuState();
    TempScript script(
        "prices {\n"
        " start = 100\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    EXPECT_NO_THROW(ReadPrices(script.stream));
    EXPECT_EQ(g_StartCredits, 100u);
}

TEST(MenuResourceEntry, PricesWithinCapacityAreAssigned)
{
    ResetMenuState();
    g_DinoInfo.resize(2);
    g_WeapInfo.resize(1);
    g_DinoInfo[0].m_AI = 10;  // CurD starts at the first huntable index
    TempScript script(
        "prices {\n"
        " dino = 10\n"
        " dino = 30\n"
        " weapon = 20\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    EXPECT_NO_THROW(ReadPrices(script.stream));
    EXPECT_EQ(g_DinoInfo[0].m_Price, 10);
    EXPECT_EQ(g_DinoInfo[1].m_Price, 30);
    EXPECT_EQ(g_WeapInfo[0].m_Price, 20);
}

TEST(MenuResourceEntry, ExcessPriceEntriesAreRejected)
{
    ResetMenuState();
    g_DinoInfo.resize(1);
    TempScript script(
        "prices {\n"
        " dino = 10\n"
        " dino = 30\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    // The prices block lists one entry per roster item; a malformed script with
    // more entries than loaded dinos must fail instead of indexing past the
    // vector.
    EXPECT_THROW(ReadPrices(script.stream), std::exception);
}

TEST(MenuResourceEntry, ExcessWeaponPricesAreRejected)
{
    ResetMenuState();
    g_WeapInfo.resize(1);
    TempScript script(
        "prices {\n"
        " weapon = 10\n"
        " weapon = 30\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    EXPECT_THROW(ReadPrices(script.stream), std::exception);
}

TEST(MenuResourceEntry, MalformedNumericValuesFailInStrictMode)
{
    ResetMenuState();
    LoadDiagnostics::Instance().Clear();
    LoadDiagnostics::Instance().SetMode(LoadMode::Strict);
    TempScript script(
        "{\n"
        " ai = ten\n"
        "}\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    // Exact key matching must not weaken the value validation itself: strict
    // mode (CI / mod authoring) still stops at the first bad value.
    EXPECT_THROW(ReadCharacters(script.stream), std::exception);

    LoadDiagnostics::Instance().SetMode(LoadMode::Lenient);
    LoadDiagnostics::Instance().Clear();
}

TEST(MenuResourceEntry, MalformedNumericValuesRecoverInLenientMode)
{
    ResetMenuState();
    LoadDiagnostics::Instance().Clear();
    TempScript script(
        "{\n"
        " ai = ten\n"
        "}\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    // The player default accepts the file, falls back to 0 for the bad value
    // and records a diagnostic instead of aborting the menu.
    EXPECT_NO_THROW(ReadCharacters(script.stream));
    EXPECT_GE(LoadDiagnostics::Instance().Count(), 1u);
    LoadDiagnostics::Instance().Clear();
}

TEST(MenuResourceEntry, HuntableThumbnailFollowsListPositionNotAiSlot)
{
    ResetMenuState();
    // The stock _MENU.TXT roster: ten huntables whose AI values are not
    // unique (Iguanodon and Carnotaurus both use 17, T-Rex uses 18). Each
    // entry must resolve to its own numbered picture, not its predecessor's.
    TempScript script(
        "{\n name = 'Parasaurolophus'\n ai = 10\n}\n"
        "{\n name = 'Pachycephalosaurus'\n ai = 11\n}\n"
        "{\n name = 'Stegosaurus'\n ai = 12\n}\n"
        "{\n name = 'Allosaurus'\n ai = 13\n}\n"
        "{\n name = 'Triceratops'\n ai = 14\n}\n"
        "{\n name = 'Velociraptor'\n ai = 15\n}\n"
        "{\n name = 'Dilophosaurus'\n ai = 16\n}\n"
        "{\n name = 'Iguanodon'\n ai = 17\n}\n"
        "{\n name = 'Carnotaurus'\n ai = 17\n}\n"
        "{\n name = 'Tyrannosaurus Rex'\n ai = 18\n}\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    EXPECT_NO_THROW(ReadCharacters(script.stream));

    ASSERT_EQ(g_DinoInfo.size(), 10u);
    EXPECT_EQ(g_DinoInfo[7].m_PicturePath, "huntdat/menu/pics/dino8.tga");
    EXPECT_EQ(g_DinoInfo[8].m_PicturePath, "huntdat/menu/pics/dino9.tga");
    EXPECT_EQ(g_DinoInfo[9].m_PicturePath, "huntdat/menu/pics/dino10.tga");
}

TEST(MenuResourceEntry, ExplicitPicturePathOverridesTheDefaultThumbnail)
{
    ResetMenuState();
    TempScript script(
        "{\n"
        " name = 'Custom'\n"
        " ai = 10\n"
        " pic = 'huntdat/menu/pics/custom.tga'\n"
        "}\n"
        "}\n");
    ASSERT_NE(script.stream, nullptr);

    EXPECT_NO_THROW(ReadCharacters(script.stream));

    ASSERT_EQ(g_DinoInfo.size(), 1u);
    EXPECT_EQ(g_DinoInfo[0].m_PicturePath, "huntdat/menu/pics/custom.tga");
}
