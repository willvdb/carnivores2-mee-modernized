#include <gtest/gtest.h>
#include "Hunt.h"
#include "Game/CharacterAwareness.h"
#include "Game/CharacterInternal.h"

// The production resolver and timer owner, with only world placement, random
// selection and logging replaced. No renderer, input or live assets required.
bool IsAILoggingEnabled() { return false; }
void PrintLogAI(const char*) {}
std::int32_t _HeapFree(Platform::HeapHandle, std::uint32_t, void* p) { std::free(p); return 1; }
float GetLandH(float, float) { return 0; }
float GetLandUpH(float, float) { return 0; }
int CheckPlaceCollisionP(Vector3d&, bool) { return 0; }
void SetNewTargetPlace(TCharacter* c, float r) { c->tgx = c->pos.x + r; c->tgz = c->pos.z; }
void SetNewTargetPlace_Brahi(TCharacter* c, float r) { SetNewTargetPlace(c, r); }
void SetNewTargetPlaceFish(TCharacter* c, float r) { SetNewTargetPlace(c, r); }
void SetNewTargetPlace_Icth(TCharacter* c, float r) { SetNewTargetPlace(c, r); }

class Awareness : public ::testing::Test {
protected:
    TCharacter c{};
    void SetUp() override {
        c.CType = 0; c.Clone = AI_TREX; c.Health = 100; c.scale = 1;
        c.State = 1; c.packId = -1; c.lookx = 1;
        c.pos = {10000, 0, 10000};
        DinoInfo[0] = {}; DinoInfo[0].Health0 = 100;
        DinoInfo[0].killDist = 100; DinoInfo[0].aggress = 10; DinoInfo[0].HearK = 1;
        DinoAggressMulti[0] = 0;
        AIInfo[AI_TREX] = {}; AIInfo[AI_TREX].agressMulti = 1; AIInfo[AI_TREX].carnivore = true;
        PlayerX = 12000; PlayerY = 0; PlayerZ = 10000;
        MyHealth = 100; ObservMode = false; TimeDt = 100;
        g_GameMode = GameMode::Normal;
    }
};
TEST_F(Awareness, RememberedHitDoesNotTrackMovingHunter) {
    THunterStimulus hit; hit.kind = HunterStimulusKind::DirectHit;
    hit.position = {12000, 0, 10000};
    ASSERT_TRUE(ApplyHunterStimulus(c, hit));
    ASSERT_EQ(c.hunterAwareness, HunterAwarenessState::RetaliatingHit);
    PlayerX = 20000; PlayerZ = 22000;
    TickHunterAwareness(c); UpdateHunterNavigation(c);
    EXPECT_FLOAT_EQ(c.tgx, 12000); EXPECT_FLOAT_EQ(c.tgz, 10000);
    THunterGeometry contact; contact.distance = 1; contact.distanceSquared = 1;
    EXPECT_FALSE(CanKillHunter(c, contact));
}
TEST_F(Awareness, ExpiredTrackingCannotKillOrPublishPackAnchor) {
    c.hunterAwareness = HunterAwarenessState::TrackingHunter; c.AfraidTime = TimeDt;
    c.packId = 0; PackHuntTime[0] = 0; RealTime = 1234;
    TickHunterAwareness(c); UpdateHunterNavigation(c);
    EXPECT_EQ(c.hunterAwareness, HunterAwarenessState::None);
    EXPECT_EQ(PackHuntTime[0], 0);
    THunterGeometry contact; contact.distance = 1; contact.distanceSquared = 1;
    EXPECT_FALSE(CanKillHunter(c, contact));
    c.hunterAwareness = HunterAwarenessState::TrackingHunter;
    EXPECT_FALSE(CanKillHunter(c, contact));
    c.AfraidTime = 1000; EXPECT_TRUE(CanKillHunter(c, contact));
    c.StateF = 0xff; EXPECT_FALSE(CanKillHunter(c, contact));
}
TEST_F(Awareness, BlockedFleeLegAccumulatesAcrossCentralTicks) {
    c.hunterAwareness = HunterAwarenessState::FleeingFromHit; c.AfraidTime = 60000;
    c.tgx = 18000; c.tgz = 10000;
    for (int i = 0; i < 40; ++i) { TickHunterAwareness(c); UpdateHunterNavigation(c); }
    EXPECT_GE(c.tgtime, 4000);
    EXPECT_NE(c.tgx, 18000); // A stuck leg actually re-aimed.
    EXPECT_EQ(c.AfraidTime, 56000);
    const float targetX = c.tgx, targetZ = c.tgz;
    PlayerX = 30000; PlayerZ = 30000;
    TickHunterAwareness(c); UpdateHunterNavigation(c);
    EXPECT_FLOAT_EQ(c.tgx, targetX); EXPECT_FLOAT_EQ(c.tgz, targetZ);
    EXPECT_EQ(c.tgtime, 4100); // Re-aim once per interval, not every frame.
}
TEST_F(Awareness, FixedReactionExpiresOnceAndExhibitDoesNotReact) {
    c.hunterAwareness = HunterAwarenessState::FleeingFromHit; c.AfraidTime = TimeDt;
    TickHunterAwareness(c); UpdateHunterNavigation(c);
    EXPECT_EQ(c.hunterAwareness, HunterAwarenessState::None);
    c.StateF = 0xff; c.AfraidTime = 500;
    THunterStimulus hit; hit.kind = HunterStimulusKind::DirectHit; hit.position = c.pos;
    ApplyHunterStimulus(c, hit); TickHunterAwareness(c); UpdateHunterNavigation(c);
    EXPECT_EQ(c.hunterAwareness, HunterAwarenessState::None);
    EXPECT_EQ(c.AfraidTime, 500);
}
TEST_F(Awareness, ShotAndCallDoNotReplaceExactTracking) {
    THunterStimulus shot; shot.position = {12000, 0, 10000}; shot.soundRange = 10000;
    ASSERT_TRUE(ApplyHunterStimulus(c, shot));
    EXPECT_EQ(c.hunterAwareness, HunterAwarenessState::InvestigatingShot);
    THunterStimulus contact; contact.kind = HunterStimulusKind::Contact; contact.position = c.pos;
    ASSERT_TRUE(ApplyHunterStimulus(c, contact));
    ASSERT_EQ(c.hunterAwareness, HunterAwarenessState::TrackingHunter);
    const int remaining = c.AfraidTime;
    EXPECT_FALSE(ApplyHunterStimulus(c, shot));
    THunterStimulus call; call.kind = HunterStimulusKind::HunterCall;
    call.position = shot.position; call.callIndex = 0; DinoInfo[0].fearCall[0] = true;
    EXPECT_FALSE(ApplyHunterStimulus(c, call));
    EXPECT_EQ(c.hunterAwareness, HunterAwarenessState::TrackingHunter);
    EXPECT_EQ(c.AfraidTime, remaining);
}
TEST_F(Awareness, PackAlarmSharesNoHunterCoordinatesOrKillAuthority) {
    c.packId = 0; c.hunterAwareness = HunterAwarenessState::InvestigatingShot;
    c.AfraidTime = 5000; c.tgx = 15000; c.tgz = 10000;
    Packs[0].alert = false; PackHuntTime[0] = 0;
    TickHunterAwareness(c); UpdateHunterNavigation(c);
    EXPECT_TRUE(Packs[0].alert); EXPECT_EQ(PackHuntTime[0], 0);
    c.hunterAwareness = HunterAwarenessState::TrackingHunter; RealTime = 500;
    TickHunterAwareness(c); UpdateHunterNavigation(c);
    EXPECT_EQ(PackHuntTime[0], RealTime);
    EXPECT_FLOAT_EQ(PackHuntX[0], c.pos.x); EXPECT_FLOAT_EQ(PackHuntZ[0], c.pos.z);
}
