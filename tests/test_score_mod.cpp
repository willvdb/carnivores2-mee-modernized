// test_score_mod.cpp
// Unit tests for Hunt/Core/ScoreMod.h -- the smod= wire order shared by the
// menu (which writes the string when launching the render exe) and the engine
// (which reads it back into the ScoreMod_* globals).
//
// The order is the whole contract because no accessory name appears in the
// string. A reorder on either side would silently give an accessory another
// one's multiplier, so the first test pins the literal payload rather than
// just checking that the two helpers agree with each other. The header is
// self-contained, so no engine linkage is needed.

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>

#include "Core/ScoreMod.h"

namespace {

// Index by slot so the tests never depend on the wire order themselves.
void SetAll(float* values, float camo, float radar, float scent,
            float doubleScore, float tranq, float observer) {
    values[static_cast<int>(ScoreModSlot::Camo)] = camo;
    values[static_cast<int>(ScoreModSlot::Radar)] = radar;
    values[static_cast<int>(ScoreModSlot::Scent)] = scent;
    values[static_cast<int>(ScoreModSlot::Double)] = doubleScore;
    values[static_cast<int>(ScoreModSlot::Tranq)] = tranq;
    values[static_cast<int>(ScoreModSlot::Observer)] = observer;
}

TEST(ScoreModTest, PayloadOrderIsTheContract) {
    // These are the shipped defaults from InitEngine(). The literal string is
    // what menu builds have always emitted and what the smod= usage line
    // documents, so changing it is a deliberate act, not a side effect.
    float values[kScoreModSlotCount] = {};
    SetAll(values, 0.85f, 0.70f, 0.80f, 1.00f, 1.25f, 1.00f);

    char payload[128];
    ASSERT_GT(BuildScoreModPayload(values, payload, sizeof payload), 0u);
    EXPECT_STREQ(payload, "0.85,0.7,0.8,1,1.25,1");
}

TEST(ScoreModTest, SlotNamesFollowTheSameOrder) {
    EXPECT_STREQ(kScoreModSlotName[static_cast<int>(ScoreModSlot::Camo)], "camo");
    EXPECT_STREQ(kScoreModSlotName[static_cast<int>(ScoreModSlot::Radar)], "radar");
    EXPECT_STREQ(kScoreModSlotName[static_cast<int>(ScoreModSlot::Scent)], "scent");
    EXPECT_STREQ(kScoreModSlotName[static_cast<int>(ScoreModSlot::Double)], "double");
    EXPECT_STREQ(kScoreModSlotName[static_cast<int>(ScoreModSlot::Tranq)], "tranq");
    EXPECT_STREQ(kScoreModSlotName[static_cast<int>(ScoreModSlot::Observer)], "observer");

    // The names array is indexed by wire position, matching the slots array.
    for (int i = 0; i < kScoreModSlotCount; ++i) {
        EXPECT_EQ(static_cast<int>(kScoreModWireOrder[i]), i)
            << "wire position " << i << " should carry slot " << i;
    }
}

TEST(ScoreModTest, RoundTripKeepsEachValueInItsOwnSlot) {
    float written[kScoreModSlotCount] = {};
    SetAll(written, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f);

    char payload[128];
    ASSERT_GT(BuildScoreModPayload(written, payload, sizeof payload), 0u);

    float read[kScoreModSlotCount] = {};
    EXPECT_EQ(ParseScoreModPayload(payload, read), kScoreModSlotCount);
    for (int i = 0; i < kScoreModSlotCount; ++i) {
        EXPECT_FLOAT_EQ(read[i], written[i]) << "slot " << i;
    }
}

TEST(ScoreModTest, DistinguishableValuesLandOnDistinguishableGlobals) {
    // The failure this guards is a swap: with six different values, any
    // reordering shows up as a mismatch rather than cancelling out.
    float values[kScoreModSlotCount] = {};
    SetAll(values, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f);

    char payload[128];
    ASSERT_GT(BuildScoreModPayload(values, payload, sizeof payload), 0u);

    float read[kScoreModSlotCount] = {};
    ParseScoreModPayload(payload, read);

    EXPECT_FLOAT_EQ(read[static_cast<int>(ScoreModSlot::Camo)], 0.1f);
    EXPECT_FLOAT_EQ(read[static_cast<int>(ScoreModSlot::Radar)], 0.2f);
    EXPECT_FLOAT_EQ(read[static_cast<int>(ScoreModSlot::Scent)], 0.3f);
    EXPECT_FLOAT_EQ(read[static_cast<int>(ScoreModSlot::Double)], 0.4f);
    EXPECT_FLOAT_EQ(read[static_cast<int>(ScoreModSlot::Tranq)], 0.5f);
    EXPECT_FLOAT_EQ(read[static_cast<int>(ScoreModSlot::Observer)], 0.6f);
}

TEST(ScoreModTest, ShortPayloadLeavesTheRemainingSlotsAlone) {
    // The engine assigned each score only when sscanf had reported that many
    // conversions, so a short smod= must not zero the later multipliers.
    float values[kScoreModSlotCount];
    for (int i = 0; i < kScoreModSlotCount; ++i) values[i] = 9.0f;

    EXPECT_EQ(ParseScoreModPayload("1.5,2", values), 2);
    EXPECT_FLOAT_EQ(values[static_cast<int>(ScoreModSlot::Camo)], 1.5f);
    EXPECT_FLOAT_EQ(values[static_cast<int>(ScoreModSlot::Radar)], 2.0f);
    EXPECT_FLOAT_EQ(values[static_cast<int>(ScoreModSlot::Scent)], 9.0f);
    EXPECT_FLOAT_EQ(values[static_cast<int>(ScoreModSlot::Observer)], 9.0f);
}

TEST(ScoreModTest, MalformedPayloadStopsAtTheFirstBadField) {
    float values[kScoreModSlotCount];
    for (int i = 0; i < kScoreModSlotCount; ++i) values[i] = 9.0f;

    EXPECT_EQ(ParseScoreModPayload("", values), 0);
    EXPECT_EQ(ParseScoreModPayload(nullptr, values), 0);
    EXPECT_EQ(ParseScoreModPayload("abc", values), 0);
    EXPECT_FLOAT_EQ(values[static_cast<int>(ScoreModSlot::Camo)], 9.0f);

    // A good field followed by junk keeps the good one, as sscanf did.
    EXPECT_EQ(ParseScoreModPayload("0.5,junk", values), 1);
    EXPECT_FLOAT_EQ(values[static_cast<int>(ScoreModSlot::Camo)], 0.5f);
    EXPECT_FLOAT_EQ(values[static_cast<int>(ScoreModSlot::Radar)], 9.0f);
}

TEST(ScoreModTest, MatchesWhatTheMenuEmittedBefore) {
    // The menu used to build this string with operator<< on a stringstream.
    // Routing it through BuildScoreModPayload must not change a byte of it,
    // including the default six-significant-digit float formatting.
    float values[kScoreModSlotCount] = {};
    SetAll(values, 0.85f, 0.70f, 0.80f, 1.00f, 1.25f, 1.00f);

    std::ostringstream legacy;
    legacy << values[static_cast<int>(ScoreModSlot::Camo)] << ","
           << values[static_cast<int>(ScoreModSlot::Radar)] << ","
           << values[static_cast<int>(ScoreModSlot::Scent)] << ","
           << values[static_cast<int>(ScoreModSlot::Double)] << ","
           << values[static_cast<int>(ScoreModSlot::Tranq)] << ","
           << values[static_cast<int>(ScoreModSlot::Observer)];

    char payload[128];
    ASSERT_GT(BuildScoreModPayload(values, payload, sizeof payload), 0u);
    EXPECT_EQ(std::string(payload), legacy.str());
}

TEST(ScoreModTest, BuilderRefusesABufferItCannotFill) {
    float values[kScoreModSlotCount] = {};
    SetAll(values, 0.85f, 0.70f, 0.80f, 1.00f, 1.25f, 1.00f);

    char payload[128];
    payload[0] = 'x';
    EXPECT_EQ(BuildScoreModPayload(values, payload, 4), 0u);
    EXPECT_EQ(payload[0], '\0') << "a failed build should leave an empty string";
}

}  // namespace
