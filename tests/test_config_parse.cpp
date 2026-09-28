// test_config_parse.cpp
// Unit tests for Hunt/Core/ConfigParse.h -- the strict scalar rules behind
// config.cfg's sky_mode / sky_dome_scale / sky_horizon_drop.  A bare
// strtol/strtof maps garbage to 0/0.0 and accepts trailing junk, which would
// silently select a valid-looking sky mode; these tests pin that down.
// The header is pure, so no engine linkage is needed.

#include <gtest/gtest.h>

#include "Core/ConfigParse.h"

namespace {

// Inclusive range used by config.cfg "sky_mode".
constexpr int kModeMin = 0;
constexpr int kModeMax = 2;

}  // namespace

TEST(ConfigParseInt, AcceptsFullTokensInsideTheRange) {
    int out = -1;
    EXPECT_TRUE(ParseConfigInt("0", kModeMin, kModeMax, out));
    EXPECT_EQ(out, 0);
    EXPECT_TRUE(ParseConfigInt("1", kModeMin, kModeMax, out));
    EXPECT_EQ(out, 1);
    EXPECT_TRUE(ParseConfigInt("2", kModeMin, kModeMax, out));
    EXPECT_EQ(out, 2);
    EXPECT_TRUE(ParseConfigInt("+2", kModeMin, kModeMax, out));
    EXPECT_EQ(out, 2);
}

TEST(ConfigParseInt, RejectsGarbageThatBareStrtoWouldCoerce) {
    int out = 77;
    EXPECT_FALSE(ParseConfigInt("", kModeMin, kModeMax, out));
    EXPECT_FALSE(ParseConfigInt(nullptr, kModeMin, kModeMax, out));
    EXPECT_FALSE(ParseConfigInt("abc", kModeMin, kModeMax, out));     // strtol -> 0
    EXPECT_FALSE(ParseConfigInt("1abc", kModeMin, kModeMax, out));    // strtol -> 1
    EXPECT_FALSE(ParseConfigInt("0x2", kModeMin, kModeMax, out));
    EXPECT_FALSE(ParseConfigInt("1.0", kModeMin, kModeMax, out));
    EXPECT_EQ(out, 77);  // rejected input never writes the output
}

TEST(ConfigParseInt, RejectsOutOfRangeAndOverflow) {
    int out = 77;
    EXPECT_FALSE(ParseConfigInt("-1", kModeMin, kModeMax, out));
    EXPECT_FALSE(ParseConfigInt("3", kModeMin, kModeMax, out));
    EXPECT_FALSE(ParseConfigInt("99999999999999999999", kModeMin, kModeMax, out));
    EXPECT_EQ(out, 77);
}

TEST(ConfigParseFloat, AcceptsFullTokensInsideTheRange) {
    // Inclusive range used by config.cfg "sky_dome_scale".
    float out = -1.0f;
    EXPECT_TRUE(ParseConfigFloat("32", 32.0f, 4096.0f, out));
    EXPECT_FLOAT_EQ(out, 32.0f);
    EXPECT_TRUE(ParseConfigFloat("384", 32.0f, 4096.0f, out));
    EXPECT_FLOAT_EQ(out, 384.0f);
    EXPECT_TRUE(ParseConfigFloat("384.5", 32.0f, 4096.0f, out));
    EXPECT_FLOAT_EQ(out, 384.5f);
    EXPECT_TRUE(ParseConfigFloat("4096", 32.0f, 4096.0f, out));  // upper bound included
    EXPECT_FLOAT_EQ(out, 4096.0f);
}

TEST(ConfigParseFloat, RejectsGarbageAndNonFiniteValues) {
    float out = -1.0f;
    EXPECT_FALSE(ParseConfigFloat("", 0.0f, 30.0f, out));
    EXPECT_FALSE(ParseConfigFloat(nullptr, 0.0f, 30.0f, out));
    EXPECT_FALSE(ParseConfigFloat("abc", 0.0f, 30.0f, out));
    EXPECT_FALSE(ParseConfigFloat("12abc", 0.0f, 30.0f, out));
    // NaN fails the inverted range test; infinities fail the upper bound.
    EXPECT_FALSE(ParseConfigFloat("nan", 0.0f, 30.0f, out));
    EXPECT_FALSE(ParseConfigFloat("inf", 0.0f, 30.0f, out));
    EXPECT_FALSE(ParseConfigFloat("-inf", 0.0f, 30.0f, out));
    EXPECT_EQ(out, -1.0f);
}

TEST(ConfigParseFloat, RejectsOutOfRangeAndOverflow) {
    float out = -1.0f;
    EXPECT_FALSE(ParseConfigFloat("-0.1", 0.0f, 30.0f, out));
    EXPECT_FALSE(ParseConfigFloat("30.1", 0.0f, 30.0f, out));
    EXPECT_FALSE(ParseConfigFloat("1e30", 0.0f, 30.0f, out));
    EXPECT_FALSE(ParseConfigFloat("1e999", 0.0f, 30.0f, out));  // ERANGE
    EXPECT_EQ(out, -1.0f);
}
