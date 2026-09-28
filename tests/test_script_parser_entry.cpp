// test_script_parser_entry.cpp -- production _RES.TXT line-reader contracts
//
// ReadWeaponLine()/ReadCharacterLine() dispatch free-form text fields
// (name/file/gunshot/pic1/picc/bModel) before the numeric/flag dispatch and
// before the block openers. A modded name or path can contain a field key or
// a block name as a substring; these tests drive the real readers with such
// values so a text line can neither be strict-parsed as a number nor open a
// block that consumes the rest of the stream.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "Hunt.h"
#include "Loaders/LoadDiagnostics.h"

// Production entry points defined in Hunt/Loaders/ScriptParser.cpp.
void ReadWeaponLine(FILE* stream, char* _value, char line[256]);
void ReadCharacterLine(FILE* stream, char* _value, char line[256],
                       bool& spawnInfoOverwrite, bool& spawnGroupOverwrite,
                       bool& idleOverwrite, bool& idle2Overwrite,
                       bool& roarOverwrite, bool& killOverwrite,
                       bool& waterDieOverwrite, bool& deathTypeOverwrite,
                       bool& trophyTypeOverwrite, bool& idleGroupOverwrite,
                       bool& idle2GroupOverwrite, bool& memberOverwrite);

namespace {

// Keeps a line and its post-'=' value pointer in stable mutable storage; the
// production signatures take char* even though they only read the text.
class ScriptLine final
{
public:
    explicit ScriptLine(const char* text)
        : m_text(text, text + std::strlen(text) + 1)
    {}

    char* data() { return m_text.data(); }

    char* value()
    {
        char* equals = std::strchr(m_text.data(), '=');
        return equals ? equals + 1 : m_text.data();
    }

private:
    std::vector<char> m_text;
};

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

void CallWeaponLine(FILE* stream, const char* text)
{
    ScriptLine line(text);
    ReadWeaponLine(stream, line.value(), line.data());
}

void CallCharacterLine(FILE* stream, const char* text)
{
    ScriptLine line(text);
    bool spawnInfoFlag = false, spawnGroupFlag = false, idleFlag = false,
         idle2Flag = false, roarFlag = false, killFlag = false,
         waterDieFlag = false, deathTypeFlag = false, trophyTypeFlag = false,
         idleGroupFlag = false, idle2GroupFlag = false, memberFlag = false;
    ReadCharacterLine(stream, line.value(), line.data(),
                      spawnInfoFlag, spawnGroupFlag, idleFlag, idle2Flag,
                      roarFlag, killFlag, waterDieFlag, deathTypeFlag,
                      trophyTypeFlag, idleGroupFlag, idle2GroupFlag, memberFlag);
}

void ResetWeapon()
{
    TotalW = 0;
    WeapInfo[0] = TWeapInfo{};
}

void ResetCharacter()
{
    TotalC = 0;
    DinoInfo[0] = TDinoInfo{};
}

}  // namespace

void PrintLog(const char*)
{
}

void PrintLogVerbose(const char* message)
{
    PrintLog(message);
}

void* _HeapAlloc(Platform::HeapHandle, std::uint32_t, std::size_t)
{
    return nullptr;
}

void* _HeapAlloc(Platform::HeapHandle, std::uint32_t, std::size_t, MemoryTag)
{
    return nullptr;
}

void* _HeapAllocImpl(Platform::HeapHandle, std::uint32_t, std::size_t, MemoryTag, const char*, int)
{
    return nullptr;
}

std::int32_t _HeapFree(Platform::HeapHandle, std::uint32_t, void*)
{
    return 1;
}

[[noreturn]] void DoHalt(const char* message)
{
    throw std::runtime_error(message ? message : "halt");
}

TEST(ScriptParserEntry, WeaponTextValuesReachOnlyTextFields)
{
    ResetWeapon();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    // Every value below contains a numeric/flag key as a lowercase substring
    // ('crossbow' -> cross, 'separated' -> rate, 'reload_click' -> reload).
    // None of them may consume the text as a number.
    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "name = 'crossbow'"));
    EXPECT_STREQ(WeapInfo[0].Name, "crossbow");
    EXPECT_FALSE(WeapInfo[0].cross);
    EXPECT_EQ(WeapInfo[0].crossRed, 0);

    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "file = 'models/separated/rifle.car'"));
    EXPECT_STREQ(WeapInfo[0].FName, "models/separated/rifle.car");
    EXPECT_FLOAT_EQ(WeapInfo[0].Rate, 0.0f);

    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "gunshot = 'sounds/reload_click.wav'"));
    EXPECT_STREQ(WeapInfo[0].SFXName, "sounds/reload_click.wav");
    EXPECT_TRUE(WeapInfo[0].MGSSound);
    EXPECT_EQ(WeapInfo[0].Reload, 0);

    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "bModel = 'proj/rate_thing.car'"));
    EXPECT_STREQ(WeapInfo[0].BLName, "proj/rate_thing.car");
    EXPECT_TRUE(WeapInfo[0].bullet);
    EXPECT_FLOAT_EQ(WeapInfo[0].Rate, 0.0f);

    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "pic1 = 'ammo/bullet1.tga'"));
    EXPECT_STREQ(WeapInfo[0].BFName, "ammo/bullet1.tga");

    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "picc = 'ammo/chamb1.tga'"));
    EXPECT_STREQ(WeapInfo[0].CFName, "ammo/chamb1.tga");
    EXPECT_TRUE(WeapInfo[0].picch);
}

TEST(ScriptParserEntry, WeaponNumericDispatchIsUnchanged)
{
    ResetWeapon();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "rate = 0.45"));
    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "land_power = 5"));
    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "reload = 3"));
    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "cross = TRUE"));
    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "recoil = 0.35"));

    EXPECT_FLOAT_EQ(WeapInfo[0].Rate, 0.45f);
    EXPECT_FLOAT_EQ(WeapInfo[0].Power, 5.0f);
    EXPECT_EQ(WeapInfo[0].Reload, 3);
    EXPECT_TRUE(WeapInfo[0].cross);
    EXPECT_FLOAT_EQ(WeapInfo[0].recoil, 0.35f);
}

TEST(ScriptParserEntry, CharacterTextValuesReachOnlyTextFields)
{
    ResetCharacter();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    // 'massive' contains the mass key; the name below contains it too.
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "file = 'models/massive/trex.car'"));
    EXPECT_STREQ(DinoInfo[0].FName, "models/massive/trex.car");
    EXPECT_FLOAT_EQ(DinoInfo[0].Mass, 0.0f);

    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "name = 'the mass'"));
    EXPECT_STREQ(DinoInfo[0].Name, "the mass");
    EXPECT_FLOAT_EQ(DinoInfo[0].Mass, 0.0f);
}

TEST(ScriptParserEntry, CharacterTextValuesDoNotOpenBlocks)
{
    ResetCharacter();
    // A block opener in a file path used to consume everything up to the next
    // '}'. The stream contains one such body per opener so the old behavior
    // would have advanced the corresponding count.
    TempScript stream(
        "spawnratio = 1\n"
        "}\n"
        "hunterAnim = 3\n"
        "}\n"
        "startChance = 0.5\n"
        "}\n"
        "sentinel = 7\n");
    ASSERT_NE(stream.stream, nullptr);

    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "file = 'models/spawninfo/trex.car'"));
    EXPECT_STREQ(DinoInfo[0].FName, "models/spawninfo/trex.car");
    EXPECT_EQ(DinoInfo[0].SpawnInfoCh, 0);

    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "file = 'models/killtype/trex.car'"));
    EXPECT_EQ(DinoInfo[0].killTypeCount, 0);

    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "file = 'models/idlegroup/trex.car'"));
    EXPECT_EQ(DinoInfo[0].idleGroupCount, 0);

    char line[256];
    ASSERT_NE(fgets(line, sizeof(line), stream.stream), nullptr);
    // The stream must still be at its first line: no text line may consume a
    // block body before the outer loop resumes.
    EXPECT_STREQ(line, "spawnratio = 1\n");
}

TEST(ScriptParserEntry, CharacterNumericDispatchIsUnchanged)
{
    ResetCharacter();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "health = 13.5"));
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "ai = 10"));
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "mass = 120.5"));
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "smellK = 0.8"));

    EXPECT_EQ(DinoInfo[0].Health0, 13);  // legacy truncation of 13.5
    EXPECT_EQ(DinoInfo[0].Clone, 10);
    EXPECT_FLOAT_EQ(DinoInfo[0].Mass, 120.5f);
    EXPECT_FLOAT_EQ(DinoInfo[0].SmellK, 0.8f);
}

namespace {

// Policy tests: the same legacy corpus that must keep loading in lenient mode
// (the player default) must be rejected by strict mode (CI / mod authoring).
class ScriptPolicyTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        LoadDiagnostics::Instance().Clear();
        LoadDiagnostics::Instance().SetMode(LoadMode::Lenient);
    }

    void TearDown() override
    {
        LoadDiagnostics::Instance().Clear();
        LoadDiagnostics::Instance().SetMode(LoadMode::Lenient);
    }
};

}  // namespace

TEST_F(ScriptPolicyTest, LegacyScalarCorpusLoadsWithoutDiagnostics)
{
    ResetCharacter();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "health = 13.5"));
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "scale0 = 1000.0"));
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "mass = 120.5"));
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "smellK = 0.8"));
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "ai = 10"));

    EXPECT_EQ(DinoInfo[0].Health0, 13);
    EXPECT_EQ(DinoInfo[0].Scale0, 1000);
    EXPECT_FLOAT_EQ(DinoInfo[0].Mass, 120.5f);
    EXPECT_FLOAT_EQ(DinoInfo[0].SmellK, 0.8f);
    EXPECT_EQ(DinoInfo[0].Clone, 10);
    EXPECT_EQ(LoadDiagnostics::Instance().Count(), 0u);
}

TEST_F(ScriptPolicyTest, MalformedScalarsRecoverWithDiagnostics)
{
    ResetCharacter();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "health = oops"));
    EXPECT_EQ(DinoInfo[0].Health0, 0);
    EXPECT_GE(LoadDiagnostics::Instance().Count(), 1u);

    const std::size_t afterFirst = LoadDiagnostics::Instance().Count();
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "mass = nan"));
    EXPECT_FLOAT_EQ(DinoInfo[0].Mass, 0.0f);
    EXPECT_GT(LoadDiagnostics::Instance().Count(), afterFirst);

    EXPECT_NO_THROW(CallCharacterLine(
        stream.stream, "ai = 99999999999999999999"));
    EXPECT_EQ(DinoInfo[0].Clone, (std::numeric_limits<int>::max)());
}

TEST_F(ScriptPolicyTest, OverlongTextFieldTruncatesWithDiagnostic)
{
    ResetWeapon();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    const std::string longPath(120, 'a');
    const std::string line = "file = '" + longPath + "'";

    EXPECT_NO_THROW(CallWeaponLine(stream.stream, line.c_str()));
    EXPECT_EQ(std::strlen(WeapInfo[0].FName), sizeof(WeapInfo[0].FName) - 1);
    EXPECT_GE(LoadDiagnostics::Instance().Count(), 1u);
}

TEST_F(ScriptPolicyTest, MissingQuotedValueClearsFieldWithDiagnostic)
{
    ResetWeapon();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    EXPECT_NO_THROW(CallWeaponLine(stream.stream, "name = unquoted"));
    EXPECT_STREQ(WeapInfo[0].Name, "");
    EXPECT_GE(LoadDiagnostics::Instance().Count(), 1u);
}

TEST_F(ScriptPolicyTest, OutOfRangeIndexClampsWithDiagnostic)
{
    ResetCharacter();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "fearCall = 999"));
    EXPECT_TRUE(DinoInfo[0].fearCall[63]);
    EXPECT_GE(LoadDiagnostics::Instance().Count(), 1u);
}

TEST_F(ScriptPolicyTest, StrictModeHaltsOnRecoverableValues)
{
    LoadDiagnostics::Instance().SetMode(LoadMode::Strict);
    ResetCharacter();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    EXPECT_THROW(CallCharacterLine(stream.stream, "health = oops"),
                 std::runtime_error);
    EXPECT_THROW(CallCharacterLine(stream.stream, "mass = nan"),
                 std::runtime_error);
    EXPECT_THROW(CallCharacterLine(
                     stream.stream, "ai = 99999999999999999999"),
                 std::runtime_error);
    EXPECT_THROW(CallCharacterLine(stream.stream, "fearCall = 999"),
                 std::runtime_error);

    ResetWeapon();
    EXPECT_THROW(CallWeaponLine(stream.stream, "file = unquoted"),
                 std::runtime_error);
}

TEST_F(ScriptPolicyTest, StrictModeStillAcceptsLegacyForms)
{
    LoadDiagnostics::Instance().SetMode(LoadMode::Strict);
    ResetCharacter();
    TempScript stream("");
    ASSERT_NE(stream.stream, nullptr);

    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "health = 13.5"));
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "scale0 = 1000.0"));
    EXPECT_NO_THROW(CallCharacterLine(stream.stream, "mass = 120.5"));
    EXPECT_EQ(DinoInfo[0].Health0, 13);
    EXPECT_EQ(DinoInfo[0].Scale0, 1000);
    EXPECT_EQ(LoadDiagnostics::Instance().Count(), 0u);
}
