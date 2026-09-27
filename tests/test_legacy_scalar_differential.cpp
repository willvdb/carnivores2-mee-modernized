// test_legacy_scalar_differential.cpp -- recovered values vs the legacy oracle
//
// The hardening regressions happened because the new readers narrowed the
// accepted grammar instead of preserving it. The contract these tests pin is
// stronger than "the parser does not halt":
//
//   For every input the original reader accepted, the recovered value must
//   equal what atoi()/atof() produced in the original engine.
//
// The oracle implementations below are the CRT functions the legacy engine
// called, so a future change that "validates more strictly" fails here before
// it can reach a mod. Overflow inputs are excluded from the equality sweep
// because the legacy call was undefined there; they are pinned separately to
// a clamped value.

#include <gtest/gtest.h>

#include <cstdlib>
#include <limits>

#include "Loaders/ScriptValueParse.h"

namespace {

const int kIntFallback = 0;
const float kFloatFallback = 0.0f;

}  // namespace

TEST(LegacyScalarDifferential, IntRecoveryMatchesAtoi)
{
    const char* cases[] = {
        "0",      "42",     "-7",       "+5",    " 42", "42abc",
        "1000.0", "13.5",   "1.0f",     "7L",    "4oops",
        "0x10",   "7 // comment", "",   "   ",   "oops",
        "- 5",    "1e2",    ".5",       "+",     "-",
    };

    for (const char* text : cases)
    {
        const int oracle = std::atoi(text);
        const ScriptIntResult result = ParseScriptIntStatus(text, kIntFallback);
        EXPECT_EQ(result.value, oracle) << "input: [" << text << "]";
    }
}

TEST(LegacyScalarDifferential, LegacyIntRecoveryMatchesAtofTruncation)
{
    // Integer-backed gameplay fields (health, kill distance) were authored
    // with decimal literals and C-style suffixes; the legacy path read them
    // through the float parser and truncated.
    const char* cases[] = {
        "13.5", "5.5oops", "1l", "7L // legacy suffix", "13 // comment",
        "1ll",  "-2.75",   "1e2", "oops", "", "   ", "- 5",
    };

    for (const char* text : cases)
    {
        const int oracle = static_cast<int>(std::atof(text));
        const ScriptIntResult result =
            ParseScriptLegacyIntStatus(text, kIntFallback);
        EXPECT_EQ(result.value, oracle) << "input: [" << text << "]";
    }
}

TEST(LegacyScalarDifferential, FloatRecoveryMatchesAtof)
{
    const char* cases[] = {
        "0",     "1.25",  "-3.5",  "1.5f", "1.0e2", "0.5abc",
        " 2.5",  "",      "   ",   "oops", "- 5",   ".5",
    };

    for (const char* text : cases)
    {
        const float oracle = static_cast<float>(std::atof(text));
        const ScriptFloatResult result =
            ParseScriptFloatStatus(text, kFloatFallback);
        EXPECT_FLOAT_EQ(result.value, oracle) << "input: [" << text << "]";
    }
}

TEST(LegacyScalarDifferential, BlankAndMissingValuesKeepTheFallback)
{
    const ScriptIntResult integer = ParseScriptIntStatus("  \r\n", 17);
    EXPECT_EQ(integer.status, ScriptScalarStatus::Missing);
    EXPECT_EQ(integer.value, 17);

    const ScriptFloatResult decimal = ParseScriptFloatStatus(nullptr, 2.5f);
    EXPECT_EQ(decimal.status, ScriptScalarStatus::Missing);
    EXPECT_FLOAT_EQ(decimal.value, 2.5f);
}

TEST(LegacyScalarDifferential, OverflowClampsInsteadOfWrapping)
{
    const ScriptIntResult integer =
        ParseScriptIntStatus("99999999999999999999", kIntFallback);
    EXPECT_EQ(integer.status, ScriptScalarStatus::OutOfRange);
    EXPECT_EQ(integer.value, (std::numeric_limits<int>::max)());

    const ScriptIntResult negative =
        ParseScriptIntStatus("-99999999999999999999", kIntFallback);
    EXPECT_EQ(negative.status, ScriptScalarStatus::OutOfRange);
    EXPECT_EQ(negative.value, (std::numeric_limits<int>::min)());

    const ScriptFloatResult decimal =
        ParseScriptFloatStatus("1e999", kFloatFallback);
    EXPECT_EQ(decimal.status, ScriptScalarStatus::OutOfRange);
    EXPECT_FLOAT_EQ(decimal.value, (std::numeric_limits<float>::max)());
}

TEST(LegacyScalarDifferential, NonFiniteValuesFallBack)
{
    const ScriptFloatResult nan = ParseScriptFloatStatus("nan", 7.0f);
    EXPECT_EQ(nan.status, ScriptScalarStatus::NotFinite);
    EXPECT_FLOAT_EQ(nan.value, 7.0f);

    const ScriptFloatResult positiveInf = ParseScriptFloatStatus("inf", 7.0f);
    EXPECT_EQ(positiveInf.status, ScriptScalarStatus::NotFinite);
    EXPECT_FLOAT_EQ(positiveInf.value, 7.0f);

    const ScriptFloatResult negativeInf = ParseScriptFloatStatus("-inf", 7.0f);
    EXPECT_EQ(negativeInf.status, ScriptScalarStatus::NotFinite);
    EXPECT_FLOAT_EQ(negativeInf.value, 7.0f);
}

TEST(LegacyScalarDifferential, AllOrNothingVariantsStillReject)
{
    int integer = 99;
    EXPECT_FALSE(ParseScriptInt("oops", integer));
    EXPECT_FALSE(ParseScriptInt("99999999999999999999", integer));
    EXPECT_EQ(integer, 99);

    float decimal = 2.0f;
    EXPECT_FALSE(ParseScriptFloat("nan", decimal));
    EXPECT_FALSE(ParseScriptFloat("inf", decimal));
    EXPECT_FLOAT_EQ(decimal, 2.0f);

    EXPECT_FALSE(ParseScriptLegacyInt("oops", integer));
    EXPECT_EQ(integer, 99);

    // The documented legacy forms still load through the all-or-nothing API.
    EXPECT_TRUE(ParseScriptInt("4oops", integer));
    EXPECT_EQ(integer, 4);
    EXPECT_TRUE(ParseScriptLegacyInt("13.5", integer));
    EXPECT_EQ(integer, 13);
}
