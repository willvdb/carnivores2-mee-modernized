#include <gtest/gtest.h>
#include "Hunt.h"
#include "Game/CharacterAwareness.h"
#include "Game/CharacterInternal.h"

// Production resolver and frame dispatcher. World placement, effects and
// asset-dependent species animators are stubbed; frame policy is not copied.
// No renderer, input or live assets required.
namespace { int targetSelections = 0; }
bool IsAILoggingEnabled() { return false; }
void PrintLogAI(const char*) {}
std::int32_t _HeapFree(Platform::HeapHandle, std::uint32_t, void* p) { std::free(p); return 1; }
float GetLandH(float, float) { return 0; }
float GetLandUpH(float, float) { return 0; }
int CheckPlaceCollisionP(Vector3d&, bool) { return 0; }
void SetNewTargetPlace(TCharacter* c, float r) {
    ++targetSelections;
    c->tgx = c->pos.x + r; c->tgz = c->pos.z; c->tgtime = 0;
}
void SetNewTargetPlace_Brahi(TCharacter* c, float r) { SetNewTargetPlace(c, r); }
void SetNewTargetPlaceFish(TCharacter* c, float r) { SetNewTargetPlace(c, r); }
void SetNewTargetPlace_Icth(TCharacter* c, float r) { SetNewTargetPlace(c, r); }

// Freeze movement to model a blocked leg while running the real frame loop.
void AnimateHuntDead(TCharacter*) {}
void AnimateDeadCommon(TCharacter*) {}
void AnimateHuntable(TCharacter*) {}
void AnimateTRex(TCharacter*) {}
void AnimateMClientCharacter(TCharacter*) {}
void AnimateClassicAmbient(TCharacter*) {}
void AnimateFish(TCharacter*) {}
void AnimateIcth(TCharacter*) {}
void AnimateDeadFish(TCharacter*) {}
void AnimateIcthDead(TCharacter*) {}
void AnimateBrahi(TCharacter*) {}
void AnimateBrahiOld(TCharacter*) {}
void AnimateDimor(TCharacter*) {}
void PlaceCharactersSurvival() {}
void AddBloodTrail(TCharacter*) {}
void MakeNoise(Vector3d, float) {}
void AddVoice3d(int, short int*, float, float, float) {}

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
        targetSelections = 0;
        Multiplayer = false; ChCount = 1; PackCount = 0;
        Packs[0] = {}; PackHuntTime[0] = 0;
        PlayerPos = {PlayerX, PlayerY, PlayerZ};
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

// These tests call the production dispatcher, including its pre-awareness
// target aging/timeout, contact, timer tick and navigation, in their real order.
class AwarenessFrame : public Awareness {
protected:
    TCharacter& StartFrameCharacter() {
        Characters[0] = c;
        return Characters[0];
    }
};

class FixedFleeFrame : public AwarenessFrame,
    public ::testing::WithParamInterface<HunterAwarenessState> {
protected:
    void SetUp() override {
        AwarenessFrame::SetUp();
        c.Clone = AI_PACH;
        AIInfo[AI_PACH] = {};
    }
};

TEST_P(FixedFleeFrame, BlockedLegRetriesOnlyAtFourSecondIntervals) {
    auto& actor = StartFrameCharacter();
    actor.hunterAwareness = GetParam(); actor.AfraidTime = 60000;
    actor.tgx = 18000; actor.tgz = 10000;
    for (int frame = 1; frame <= 39; ++frame) {
        AnimateCharacters();
        ASSERT_EQ(actor.tgtime, frame * TimeDt);
        ASSERT_FLOAT_EQ(actor.tgx, 18000); // Includes the old half-interval retry.
        ASSERT_FLOAT_EQ(actor.tgz, 10000);
    }
    AnimateCharacters();
    EXPECT_EQ(actor.tgtime, 4000);
    EXPECT_EQ(actor.AfraidTime, 56000);
    EXPECT_NE(actor.tgx, 18000);
    const float retryX = actor.tgx, retryZ = actor.tgz;
    PlayerX = 30000; PlayerZ = 30000;
    PlayerPos = {PlayerX, PlayerY, PlayerZ};
    for (int frame = 41; frame <= 79; ++frame) {
        AnimateCharacters();
        ASSERT_EQ(actor.tgtime, frame * TimeDt);
        ASSERT_FLOAT_EQ(actor.tgx, retryX);
        ASSERT_FLOAT_EQ(actor.tgz, retryZ);
    }
    AnimateCharacters();
    EXPECT_EQ(actor.tgtime, 8000);
    EXPECT_TRUE(actor.tgx != retryX || actor.tgz != retryZ);
    EXPECT_EQ(targetSelections, 0);

    // A genuinely reached leg resets progress for the next leg.
    actor.pos.x = actor.tgx; actor.pos.z = actor.tgz;
    AnimateCharacters();
    EXPECT_EQ(actor.tgtime, 0);
    AnimateCharacters();
    EXPECT_EQ(actor.tgtime, TimeDt);
}

TEST_P(FixedFleeFrame, RetryBoundaryCannotBeConsumedByOuterAging) {
    auto& actor = StartFrameCharacter();
    actor.hunterAwareness = GetParam(); actor.AfraidTime = 60000;
    actor.tgx = 18000; actor.tgz = 10000; actor.tgtime = 3900;
    AnimateCharacters();
    EXPECT_EQ(actor.tgtime, 4000);
    EXPECT_NE(actor.tgx, 18000);

    // Even a long blocked reaction must not fall through to wandering.
    actor.tgtime = 30000;
    const float targetX = actor.tgx, targetZ = actor.tgz;
    AnimateCharacters();
    EXPECT_EQ(actor.tgtime, 30100);
    EXPECT_EQ(targetSelections, 0);
    EXPECT_FLOAT_EQ(actor.tgx, targetX); EXPECT_FLOAT_EQ(actor.tgz, targetZ);
}

INSTANTIATE_TEST_SUITE_P(FixedReactions, FixedFleeFrame, ::testing::Values(
    HunterAwarenessState::FleeingFromShot, HunterAwarenessState::FleeingFromHit,
    HunterAwarenessState::FleeingFromCall));

class FixedPursuitFrame : public AwarenessFrame,
    public ::testing::WithParamInterface<HunterStimulusKind> {};

TEST_P(FixedPursuitFrame, RememberedTargetSurvivesWanderTimeoutAndReleasesNormally) {
    auto& actor = StartFrameCharacter();
    THunterStimulus event;
    event.kind = GetParam(); event.position = {15000, 0, 10000};
    event.soundRange = 10000;
    DinoInfo[0].runspd = 0.1f; // Authored slow travel gives a >30s investigation.
    ASSERT_TRUE(ApplyHunterStimulus(actor, event));
    ASSERT_TRUE(IsFixedHunterPursuit(&actor));
    const auto reaction = actor.hunterAwareness;
    const int lifetime = actor.AfraidTime;
    ASSERT_GT(lifetime, 31000);
    PlayerX = 20000; PlayerZ = 22000;
    PlayerPos = {PlayerX, PlayerY, PlayerZ};
    for (int frame = 1; frame <= 310; ++frame) {
        AnimateCharacters();
        ASSERT_EQ(actor.tgtime, frame * TimeDt);
        ASSERT_EQ(actor.AfraidTime, lifetime - frame * TimeDt);
        ASSERT_EQ(actor.hunterAwareness, reaction);
        ASSERT_FLOAT_EQ(actor.tgx, event.position.x);
        ASSERT_FLOAT_EQ(actor.tgz, event.position.z);
        ASSERT_EQ(targetSelections, 0);
    }

    // Arrival still starts the awareness-owned local search, using the picker.
    actor.pos = event.position;
    AnimateCharacters();
    EXPECT_EQ(targetSelections, 1);
    EXPECT_EQ(actor.tgtime, 0);
    EXPECT_EQ(actor.hunterAwareness, reaction);
    EXPECT_FLOAT_EQ(actor.tgx, actor.pos.x + kShotSearchRadius);
    const float searchX = actor.tgx, searchZ = actor.tgz;
    while (actor.AfraidTime > TimeDt) {
        AnimateCharacters();
        ASSERT_EQ(actor.hunterAwareness, reaction);
        ASSERT_FLOAT_EQ(actor.tgx, searchX); ASSERT_FLOAT_EQ(actor.tgz, searchZ);
        ASSERT_EQ(targetSelections, 1);
    }
    AnimateCharacters();
    EXPECT_EQ(actor.hunterAwareness, HunterAwarenessState::None);
    EXPECT_EQ(actor.State, 0); EXPECT_EQ(actor.AfraidTime, 0);
    EXPECT_EQ(actor.tgtime, 0);
    EXPECT_FLOAT_EQ(actor.tgx, actor.pos.x); EXPECT_FLOAT_EQ(actor.tgz, actor.pos.z);

    // With movement frozen, the ordinary timeout can select a target again.
    for (int frame = 0; frame < 300; ++frame) AnimateCharacters();
    EXPECT_EQ(targetSelections, 1); EXPECT_EQ(actor.tgtime, 30000);
    AnimateCharacters();
    EXPECT_EQ(targetSelections, 2); EXPECT_EQ(actor.tgtime, 0);
    EXPECT_FLOAT_EQ(actor.tgx, actor.pos.x + 2048);
}

TEST_P(FixedPursuitFrame, ContactStillPromotesToLiveTrackingAndPackAnchor) {
    auto& actor = StartFrameCharacter();
    THunterStimulus event;
    event.kind = GetParam(); event.position = {15000, 0, 10000};
    event.soundRange = 10000;
    ASSERT_TRUE(ApplyHunterStimulus(actor, event));
    actor.tgtime = 30000;
    actor.packId = 0; Packs[0].leader = &actor; PackCount = 1;
    RealTime = 1234;
    AnimateCharacters();
    EXPECT_TRUE(Packs[0].alert); EXPECT_EQ(PackHuntTime[0], 0);
    EXPECT_EQ(targetSelections, 0);
    PlayerX = actor.pos.x + 1; PlayerZ = actor.pos.z;
    PlayerPos = {PlayerX, PlayerY, PlayerZ};
    AnimateCharacters();
    EXPECT_EQ(actor.hunterAwareness, HunterAwarenessState::TrackingHunter);
    EXPECT_EQ(actor.AfraidTime, kCloseRangeAwarenessTime - TimeDt);
    EXPECT_EQ(actor.tgtime, 0);
    EXPECT_FLOAT_EQ(actor.tgx, PlayerX); EXPECT_FLOAT_EQ(actor.tgz, PlayerZ);
    EXPECT_EQ(PackHuntTime[0], RealTime);
    EXPECT_FLOAT_EQ(PackHuntX[0], actor.pos.x);
    PlayerX += 1000; PlayerPos.x = PlayerX;
    AnimateCharacters();
    EXPECT_FLOAT_EQ(actor.tgx, PlayerX); EXPECT_EQ(actor.tgtime, 0);
}

INSTANTIATE_TEST_SUITE_P(RememberedEvents, FixedPursuitFrame, ::testing::Values(
    HunterStimulusKind::GunshotHeard, HunterStimulusKind::DirectHit));

TEST_F(AwarenessFrame, TrophyAndInertCharactersBypassFramePolicy) {
    auto& actor = StartFrameCharacter();
    actor.hunterAwareness = HunterAwarenessState::FleeingFromHit;
    actor.AfraidTime = 60000; actor.tgtime = 3900;
    actor.tgx = 18000; actor.tgz = 10000;
    actor.StateF = 0xff;
    AnimateCharacters();
    EXPECT_EQ(actor.tgtime, 3900); EXPECT_EQ(actor.AfraidTime, 60000);
    actor.StateF = 0; g_GameMode = GameMode::TrophyMode;
    AnimateCharacters();
    EXPECT_EQ(actor.tgtime, 3900); EXPECT_EQ(actor.AfraidTime, 60000);
    EXPECT_FLOAT_EQ(actor.tgx, 18000); EXPECT_EQ(targetSelections, 0);
}
