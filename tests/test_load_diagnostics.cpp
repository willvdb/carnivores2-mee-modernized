// test_load_diagnostics.cpp -- load policy and recovery diagnostics
//
// The Lenient/Strict policy is what keeps legacy mod data loadable while
// still giving CI and mod authors a way to fail on the first recoverable
// problem. These tests cover mode selection (config text + environment),
// diagnostic recording/deduplication, and the summary format.

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "Loaders/LoadDiagnostics.h"

namespace {
void SetEnvironment(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value);
#else
    if (*value) setenv(key, value, 1); else unsetenv(key);
#endif
}


class LoadDiagnosticsTest : public ::testing::Test
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
        SetEnvironment("C2_STRICT_DATA", "");
    }
};

}  // namespace

TEST_F(LoadDiagnosticsTest, DefaultsToLenient)
{
    LoadDiagnostics& diagnostics = LoadDiagnostics::Instance();
    EXPECT_EQ(diagnostics.Mode(), LoadMode::Lenient);
    EXPECT_FALSE(diagnostics.Strict());
}

TEST_F(LoadDiagnosticsTest, ParsesModeNames)
{
    LoadMode mode = LoadMode::Lenient;
    EXPECT_TRUE(ParseLoadMode("strict", mode));
    EXPECT_EQ(mode, LoadMode::Strict);
    EXPECT_TRUE(ParseLoadMode("STRICT", mode));
    EXPECT_EQ(mode, LoadMode::Strict);
    EXPECT_TRUE(ParseLoadMode("lenient", mode));
    EXPECT_EQ(mode, LoadMode::Lenient);
    EXPECT_TRUE(ParseLoadMode("legacy", mode));
    EXPECT_EQ(mode, LoadMode::Lenient);

    // Unknown text must not silently select strict mode.
    mode = LoadMode::Lenient;
    EXPECT_FALSE(ParseLoadMode("srtict", mode));
    EXPECT_EQ(mode, LoadMode::Lenient);
    EXPECT_FALSE(ParseLoadMode("", mode));
    EXPECT_FALSE(ParseLoadMode(nullptr, mode));
}

TEST_F(LoadDiagnosticsTest, ConfigTextSelectsStrict)
{
    const char text[] =
        "# Carnivores 2 config\r\n"
        "fov 62\r\n"
        "load_mode strict\r\n"
        "verbose_logging 0\r\n";
    EXPECT_TRUE(InitLoadPolicyFromConfigText(text));
    EXPECT_TRUE(LoadDiagnostics::Instance().Strict());
}

TEST_F(LoadDiagnosticsTest, ConfigTextAcceptsSpacelessAssignmentAndCase)
{
    LoadDiagnostics& diagnostics = LoadDiagnostics::Instance();
    EXPECT_TRUE(InitLoadPolicyFromConfigText("load_mode=strict\n"));
    EXPECT_TRUE(diagnostics.Strict());

    SetUp();
    EXPECT_TRUE(InitLoadPolicyFromConfigText("LOAD_MODE lenient\n"));
    EXPECT_FALSE(diagnostics.Strict());
}

TEST_F(LoadDiagnosticsTest, ConfigTextIgnoresUnrelatedAndMisspelledKeys)
{
    LoadDiagnostics& diagnostics = LoadDiagnostics::Instance();
    EXPECT_FALSE(InitLoadPolicyFromConfigText("# load_mode strict\nfov 62\n"));
    EXPECT_EQ(diagnostics.Mode(), LoadMode::Lenient);

    // A misspelled key must not flip the policy.
    EXPECT_FALSE(InitLoadPolicyFromConfigText("load_movde strict\n"));
    EXPECT_EQ(diagnostics.Mode(), LoadMode::Lenient);
}

TEST_F(LoadDiagnosticsTest, EnvironmentOverridesConfig)
{
    InitLoadPolicyFromConfigText("load_mode lenient\n");
    ASSERT_FALSE(LoadDiagnostics::Instance().Strict());

    SetEnvironment("C2_STRICT_DATA", "strict");
    EXPECT_TRUE(InitLoadPolicyFromEnvironment());
    EXPECT_TRUE(LoadDiagnostics::Instance().Strict());

    SetEnvironment("C2_STRICT_DATA", "lenient");
    EXPECT_TRUE(InitLoadPolicyFromEnvironment());
    EXPECT_FALSE(LoadDiagnostics::Instance().Strict());
}

TEST_F(LoadDiagnosticsTest, RecordsDeduplicatesAndSummarizes)
{
    LoadDiagnostics& diagnostics = LoadDiagnostics::Instance();
    diagnostics.Report("ScriptParser", "health", "non-numeric value",
                       "  health = oops\r\n");
    diagnostics.Report("ScriptParser", "health", "non-numeric value",
                       "  health = oops\r\n");
    diagnostics.Report("ScriptParser", "mass", "non-finite value",
                       "mass = nan\n");

    ASSERT_EQ(diagnostics.Count(), 2u);
    const LoadDiagnostic& first = diagnostics.Entries()[0];
    EXPECT_EQ(first.group, "ScriptParser");
    EXPECT_EQ(first.field, "health");
    EXPECT_EQ(first.line, "health = oops");

    const std::string summary = diagnostics.Summary();
    EXPECT_NE(summary.find("2 recovered value(s)"), std::string::npos);
    EXPECT_NE(summary.find("health"), std::string::npos);
    EXPECT_NE(summary.find("non-finite value"), std::string::npos);
    EXPECT_NE(summary.find("mass = nan"), std::string::npos);
}

TEST_F(LoadDiagnosticsTest, SummaryCapsListedEntries)
{
    LoadDiagnostics& diagnostics = LoadDiagnostics::Instance();
    for (int i = 0; i < 25; ++i)
    {
        const std::string field = "field" + std::to_string(i);
        diagnostics.Report("ScriptParser", field.c_str(), "value out of range",
                           nullptr);
    }

    const std::string summary = diagnostics.Summary(10);
    EXPECT_NE(summary.find("field9"), std::string::npos);
    EXPECT_EQ(summary.find("field10"), std::string::npos);
    EXPECT_NE(summary.find("15 more"), std::string::npos);
}

TEST_F(LoadDiagnosticsTest, NumericEnvironmentSwitchMatchesDocumentedCIContract) {
    SetEnvironment("C2_STRICT_DATA", "1");
    EXPECT_TRUE(InitLoadPolicyFromEnvironment());
    EXPECT_TRUE(LoadDiagnostics::Instance().Strict());
    SetEnvironment("C2_STRICT_DATA", "0");
    EXPECT_TRUE(InitLoadPolicyFromEnvironment());
    EXPECT_FALSE(LoadDiagnostics::Instance().Strict());
}
