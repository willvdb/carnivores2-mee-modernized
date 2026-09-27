// ==========================================================================
// test_ai_behavior_math.cpp -- Regression tests for rendering-independent AI range
// ==========================================================================

#include <gtest/gtest.h>

#include "AIBehavior.h"
#include "Constants.h"

TEST(AIBehaviorMathTest, LegacyViewDistancesRemainUnchanged)
{
    EXPECT_EQ(GameplayViewRadiusCells(42), 42);
    EXPECT_EQ(GameplayViewRadiusCells(64), 64);
    EXPECT_EQ(GameplayViewRadiusCells(72), 72);
}

TEST(AIBehaviorMathTest, ExtendedRenderingDoesNotExtendGameplayRange)
{
    EXPECT_EQ(GameplayViewRadiusCells(120), 72);
    EXPECT_EQ(GameplayViewRadiusCells(196), 72);
    EXPECT_EQ(GameplayViewRadiusCells(230), 72);
}

TEST(AIBehaviorMathTest, GunshotNoiseUsesTheLegacyGameplayCeiling)
{
    constexpr float shotgunLoudness = 1.7f;
    constexpr float legacyShotgunRange = 72.0f * 200.0f * shotgunLoudness;

    EXPECT_FLOAT_EQ(GunshotNoiseRangeWorld(72, shotgunLoudness), legacyShotgunRange);
    EXPECT_FLOAT_EQ(GunshotNoiseRangeWorld(196, shotgunLoudness), legacyShotgunRange);
    EXPECT_FLOAT_EQ(GunshotNoiseRangeWorld(230, shotgunLoudness), legacyShotgunRange);
}

TEST(AIBehaviorMathTest, ShotInvestigationTimeIsFinite)
{
    EXPECT_EQ(ShotInvestigationTime(1000.0f, 1000.0f, false),
              kShotInvestigationMinTime);
    EXPECT_EQ(ShotInvestigationTime(0.0f, 1000.0f, false),
              kShotInvestigationMaxTime);
    EXPECT_EQ(ShotInvestigationTime(0.0f, 1000.0f, true),
              kTRexShotInvestigationMaxTime);
}

TEST(AIBehaviorMathTest, ShotInvestigationEndsAtTargetOrTimeout)
{
    EXPECT_TRUE(ShotInvestigationComplete(0, 10000.0f * 10000.0f));
    EXPECT_TRUE(ShotInvestigationComplete(1000,
                                         kShotInvestigationArrivalRadius
                                             * kShotInvestigationArrivalRadius));
    EXPECT_FALSE(ShotInvestigationComplete(1000, 10000.0f * 10000.0f));
}

TEST(AIBehaviorMathTest, AwarenessEventsSelectSpeciesReaction)
{
    EXPECT_EQ(HeardShotReactionState(false),
              HunterAwarenessState::InvestigatingShot);
    EXPECT_EQ(HeardShotReactionState(true),
              HunterAwarenessState::FleeingFromShot);
    EXPECT_EQ(DirectHitReactionState(false),
              HunterAwarenessState::RetaliatingHit);
    EXPECT_EQ(DirectHitReactionState(true),
              HunterAwarenessState::FleeingFromHit);
}

TEST(AIBehaviorMathTest, DirectHitDoesNotDowngradeExactTracking)
{
    EXPECT_EQ(UpdatedDirectHitAwarenessState(
                  HunterAwarenessState::TrackingHunter, false),
              HunterAwarenessState::TrackingHunter);
    EXPECT_EQ(UpdatedDirectHitAwarenessState(
                  HunterAwarenessState::TrackingHunter, true),
              HunterAwarenessState::FleeingFromHit);
    EXPECT_EQ(UpdatedDirectHitAwarenessState(
                  HunterAwarenessState::InvestigatingShot, false),
              HunterAwarenessState::RetaliatingHit);
}

TEST(AIBehaviorMathTest, TRexHitRestartsPursuitOnlyWhenNotAlreadyEngaged)
{
    EXPECT_TRUE(ShouldRestartTRexHitPursuit(HunterAwarenessState::None));
    EXPECT_TRUE(ShouldRestartTRexHitPursuit(
        HunterAwarenessState::InvestigatingShot));
    EXPECT_FALSE(ShouldRestartTRexHitPursuit(
        HunterAwarenessState::RetaliatingHit));
    EXPECT_FALSE(ShouldRestartTRexHitPursuit(
        HunterAwarenessState::TrackingHunter));
}

TEST(AIBehaviorMathTest, NoticeAnimationOnlyPlaysForANewDetection)
{
    // An active reaction must never be interrupted by a replayed notice:
    // this is what lets a tracking T-Rex keep perceiving (and so refresh its
    // lock) mid-pursuit without stopping to roar at the hunter it chases.
    EXPECT_TRUE(ShouldScheduleNoticeAnimation(HunterAwarenessState::None));
    EXPECT_FALSE(ShouldScheduleNoticeAnimation(
        HunterAwarenessState::InvestigatingShot));
    EXPECT_FALSE(ShouldScheduleNoticeAnimation(
        HunterAwarenessState::RetaliatingHit));
    EXPECT_FALSE(ShouldScheduleNoticeAnimation(
        HunterAwarenessState::FleeingFromShot));
    EXPECT_FALSE(ShouldScheduleNoticeAnimation(
        HunterAwarenessState::FleeingFromHit));
    EXPECT_FALSE(ShouldScheduleNoticeAnimation(
        HunterAwarenessState::FleeingFromCall));
    EXPECT_FALSE(ShouldScheduleNoticeAnimation(
        HunterAwarenessState::TrackingHunter));
}

TEST(AIBehaviorMathTest, DirectHitAlertOnlyStartsOnFirstAwareness)
{
    EXPECT_TRUE(ShouldInitializeDirectHitAlert(true, false));
    EXPECT_FALSE(ShouldInitializeDirectHitAlert(true, true));
    EXPECT_FALSE(ShouldInitializeDirectHitAlert(false, false));
}

TEST(AIBehaviorMathTest, AwarenessStatesDistinguishFixedReactions)
{
    EXPECT_TRUE(IsFixedHunterPursuitState(
        HunterAwarenessState::InvestigatingShot));
    EXPECT_TRUE(IsFixedHunterPursuitState(
        HunterAwarenessState::RetaliatingHit));
    EXPECT_TRUE(IsFixedHunterFleeState(
        HunterAwarenessState::FleeingFromShot));
    EXPECT_TRUE(IsFixedHunterFleeState(
        HunterAwarenessState::FleeingFromHit));
    EXPECT_TRUE(IsFixedHunterFleeState(
        HunterAwarenessState::FleeingFromCall));
    EXPECT_FALSE(IsTimedHunterReactionState(
        HunterAwarenessState::TrackingHunter));
    EXPECT_FALSE(IsTimedHunterReactionState(HunterAwarenessState::None));
}

TEST(AIBehaviorMathTest, NormalAwarenessRespectsAggressionRange)
{
    EXPECT_FALSE(OutsideNormalAggressionRange(99.0f, 100.0f, false));
    EXPECT_TRUE(OutsideNormalAggressionRange(101.0f, 100.0f, false));
    EXPECT_TRUE(OutsideNormalAggressionRangeSquared(101.0f * 101.0f,
                                                    100.0f, false));
}

TEST(AIBehaviorMathTest, RecentDamageBypassesNormalAggressionRange)
{
    // A creature that was just shot keeps engaging beyond its authored
    // acquisition range; the pre-622e50b reaction model relied on this.
    EXPECT_FALSE(OutsideNormalAggressionRange(1000.0f, 100.0f, true));
    EXPECT_FALSE(OutsideNormalAggressionRangeSquared(1000.0f * 1000.0f,
                                                     100.0f, true));
    EXPECT_TRUE(OutsideNormalAggressionRange(1000.0f, 100.0f, false));
    EXPECT_TRUE(OutsideNormalAggressionRangeSquared(1000.0f * 1000.0f,
                                                    100.0f, false));
}

TEST(AIBehaviorMathTest, HunterEventsUseTheScaledAggressionRange)
{
    // Event ranges are the authored aggression range scaled by
    // kHunterEventRangeScale. Carnotaurus: 72 * 200 * 2.5.
    constexpr float carnoEventRange =
        72.0f * 200.0f * kHunterEventRangeScale;
    // Pachycephalosaurus: 72 * 60 * 2.5 -- a low-aggression herbivore, even
    // though it reuses the Allosaurus AI clone.
    constexpr float pachyEventRange =
        72.0f * 60.0f * kHunterEventRangeScale;

    // A predator's scaled range covers any shot it can hear.
    EXPECT_FALSE(ShouldFleeFromHunterEvent(200, false, 25000.0f,
                                           carnoEventRange));
    // The same distant event makes a low-aggression herbivore flee...
    EXPECT_TRUE(ShouldFleeFromHunterEvent(60, false, 25000.0f,
                                          pachyEventRange));
    // ...while an event inside its scaled range still provokes a reaction.
    EXPECT_FALSE(ShouldFleeFromHunterEvent(60, false, 5000.0f,
                                           pachyEventRange));
}

TEST(AIBehaviorMathTest, AuthoredFearAndPassivityAlwaysFleeHunterEvents)
{
    constexpr float eventRange = 40000.0f;
    EXPECT_TRUE(ShouldFleeFromHunterEvent(200, true, 10.0f, eventRange));
    EXPECT_TRUE(ShouldFleeFromHunterEvent(0, false, 10.0f, eventRange));
    EXPECT_TRUE(ShouldFleeFromHunterEvent(-1, false, 10.0f, eventRange));
    EXPECT_FALSE(ShouldFleeFromHunterEvent(200, false, 10.0f, eventRange));
}

TEST(AIBehaviorMathTest, EventFearMatchesThePerFrameFleeReasons)
{
    // The event-time classifier and the per-frame flee decision share one
    // authored-fear rule (S1 in the hunter-awareness review), so an injured
    // fearShotHit species hears a shot and flees from it instead of entering
    // a fixed investigation its per-frame rule immediately overrode with a
    // live flee.
    EXPECT_FALSE(FearsHunterEvent(false, false, false));
    EXPECT_TRUE(FearsHunterEvent(true, false, false));   // fears shot sounds
    EXPECT_TRUE(FearsHunterEvent(false, true, false));   // defensive at full health
    EXPECT_TRUE(FearsHunterEvent(false, false, true));   // injured and fears being shot
    EXPECT_TRUE(FearsHunterEvent(true, true, true));
}

TEST(AIBehaviorMathTest, DedicatedPredatorWithoutFleeStateRespondsAggressively)
{
    constexpr bool alwaysRespondAggressively = true;
    // The T-Rex has no authored aggress value and no flee path.
    EXPECT_FALSE(ShouldFleeFromHunterEvent(0, true, 25000.0f, 0.0f,
                                           alwaysRespondAggressively));
}

TEST(AIBehaviorMathTest, DistantReactionsGetEnoughTravelTime)
{
    // A slow creature at the edge of hearing gets the travel time plus the
    // minimum search window instead of the proximity-only minimum.
    const int base = ShotInvestigationTime(1000.0f, 1000.0f, false);
    EXPECT_EQ(base, kShotInvestigationMinTime);
    const int extended = ShotInvestigationTimeForTravel(base, 5000.0f, 1.0f);
    EXPECT_EQ(extended, 5000 + kShotInvestigationMinTime);
    // A fast creature only needs the travel floor when it exceeds the base.
    EXPECT_EQ(ShotInvestigationTimeForTravel(base, 5000.0f, 100.0f),
              50 + kShotInvestigationMinTime);
    // A long proximity-based reaction is never shortened.
    const int longBase = ShotInvestigationTime(0.0f, 1000.0f, false);
    EXPECT_EQ(ShotInvestigationTimeForTravel(longBase, 5000.0f, 100.0f),
              longBase);
    // The floor is capped so an event reaction stays finite.
    EXPECT_EQ(ShotInvestigationTimeForTravel(base, 1.0e7f, 1.0f),
              kShotInvestigationTravelCap);
}

TEST(AIBehaviorMathTest, ContactRangePromotesFixedPursuitToTracking)
{
    constexpr float reach = 400.0f;
    constexpr float reachSquared = reach * reach;

    // A hunter inside the reach of an investigating or retaliating creature
    // is treated as detected, so species without sight/scent stay dangerous.
    EXPECT_TRUE(ShouldPromotePursuitToTracking(
        true, true, 399.0f * 399.0f, reachSquared));
    // Exactly at the reach still counts as contact.
    EXPECT_TRUE(ShouldPromotePursuitToTracking(
        true, true, reachSquared, reachSquared));
    // Outside the reach, a remembered event point still never kills.
    EXPECT_FALSE(ShouldPromotePursuitToTracking(
        true, true, 401.0f * 401.0f, reachSquared));
    // Vertical separation (a hunter below a cliff) blocks contact awareness.
    EXPECT_FALSE(ShouldPromotePursuitToTracking(
        true, false, 10.0f, reachSquared));
    // Flee reactions keep their stored direction and stay non-lethal.
    EXPECT_FALSE(ShouldPromotePursuitToTracking(
        false, true, 10.0f, reachSquared));
    // Species without an authored attack reach never promote this way.
    EXPECT_FALSE(ShouldPromotePursuitToTracking(true, true, 10.0f, 0.0f));
}

TEST(AIBehaviorMathTest, AuthoredThreatFleesForEveryAuthoredReason)
{
    // No authored reason: pursue.
    EXPECT_FALSE(ShouldFleeFromAuthoredThreat(
        false, false, true, false, false, false));
    // Outside the aggression range, passive, or unaware of the hunter.
    EXPECT_TRUE(ShouldFleeFromAuthoredThreat(
        true, false, true, false, false, false));
    EXPECT_TRUE(ShouldFleeFromAuthoredThreat(
        false, true, true, false, false, false));
    EXPECT_TRUE(ShouldFleeFromAuthoredThreat(
        false, false, false, false, false, false));
    // Authored fear responses: defensive at full health, injured and fearing
    // shots, or already fleeing the shot.
    EXPECT_TRUE(ShouldFleeFromAuthoredThreat(
        false, false, true, true, false, false));
    EXPECT_TRUE(ShouldFleeFromAuthoredThreat(
        false, false, true, false, true, false));
    EXPECT_TRUE(ShouldFleeFromAuthoredThreat(
        false, false, true, false, false, true));
}

TEST(AIBehaviorMathTest, HunterLookOffsetMatchesTheAnimatorTable)
{
    // 100 x scale: Allosaurus and the aquatic predators.
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_ALLO, 2.0f, false), 200.0f);
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_MOSA, 1.5f, true), 150.0f);
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_FISH, 1.0f, true), 100.0f);
    // 300 x scale: the large-bodied Huntable species.
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_CHASM, 1.5f, false), 450.0f);
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_BRONT, 2.0f, false), 600.0f);
    // Fixed head offset for the predators and the Brachiosaurus family.
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_TREX, 1.0f, true), 108.0f);
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_BRACHDANGER, 1.0f, false), 108.0f);
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_LANDBRACH, 1.0f, false), 108.0f);
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_VELO, 1.0f, true), 108.0f);
    // No authored head offset: herbivores and herd animals.
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_DEER, 1.0f, false), 0.0f);
    EXPECT_FLOAT_EQ(HunterLookOffset(AI_MAMM, 1.0f, false), 0.0f);
}

TEST(AIBehaviorMathTest, TrackingOrContactFleeAllowsAKill)
{
    EXPECT_TRUE(HunterAwarenessAllowsKill(HunterAwarenessState::TrackingHunter));
    // A fixed flee reaction may still crush a hunter at contact range; the
    // reach check in CanKillHunter keeps it non-lethal at a distance.
    EXPECT_TRUE(HunterAwarenessAllowsKill(HunterAwarenessState::FleeingFromShot));
    EXPECT_TRUE(HunterAwarenessAllowsKill(HunterAwarenessState::FleeingFromHit));
    EXPECT_TRUE(HunterAwarenessAllowsKill(HunterAwarenessState::FleeingFromCall));
    EXPECT_FALSE(HunterAwarenessAllowsKill(HunterAwarenessState::None));
    EXPECT_FALSE(HunterAwarenessAllowsKill(HunterAwarenessState::InvestigatingShot));
    EXPECT_FALSE(HunterAwarenessAllowsKill(HunterAwarenessState::RetaliatingHit));
}

TEST(AIBehaviorMathTest, AttackReachBoundaries)
{
    // The hunter must be strictly inside the authored reach.
    EXPECT_TRUE(IsWithinSquaredReach(99.0f * 99.0f, 100.0f));
    EXPECT_FALSE(IsWithinSquaredReach(100.0f * 100.0f, 100.0f));
    EXPECT_TRUE(IsWithinLinearReach(99.0f, 100.0f));
    EXPECT_FALSE(IsWithinLinearReach(100.0f, 100.0f));

    // A species without an authored attack reach never kills.
    EXPECT_FALSE(IsWithinSquaredReach(1.0f, 0.0f));
    EXPECT_FALSE(IsWithinLinearReach(1.0f, 0.0f));

    EXPECT_TRUE(IsWithinKillAltitude(255.0f, 256.0f));
    EXPECT_FALSE(IsWithinKillAltitude(256.0f, 256.0f));
}

TEST(AIBehaviorMathTest, UntimedReactionTicksOnceAndClamps)
{
    EXPECT_EQ(TickUntimedReaction(1000, 16), 984);
    EXPECT_EQ(TickUntimedReaction(16, 16), 0);
    EXPECT_EQ(TickUntimedReaction(15, 16), 0);
    EXPECT_EQ(TickUntimedReaction(0, 16), 0);
    EXPECT_EQ(TickUntimedReaction(1000, 0), 1000);
}

TEST(AIBehaviorMathTest, FixedReactionsAlarmThePack)
{
    EXPECT_TRUE(ShouldAlarmPack(HunterAwarenessState::InvestigatingShot));
    EXPECT_TRUE(ShouldAlarmPack(HunterAwarenessState::RetaliatingHit));
    EXPECT_TRUE(ShouldAlarmPack(HunterAwarenessState::FleeingFromShot));
    EXPECT_TRUE(ShouldAlarmPack(HunterAwarenessState::FleeingFromHit));
    EXPECT_TRUE(ShouldAlarmPack(HunterAwarenessState::FleeingFromCall));
    EXPECT_FALSE(ShouldAlarmPack(HunterAwarenessState::TrackingHunter));
    EXPECT_FALSE(ShouldAlarmPack(HunterAwarenessState::None));
}

TEST(AIBehaviorMathTest, SpeciesAggressMultiOverrideWinsWhenPresent)
{
    EXPECT_EQ(EffectiveAggressMulti(2, 8), 2);
    EXPECT_EQ(EffectiveAggressMulti(8, 8), 8);
    EXPECT_EQ(EffectiveAggressMulti(0, 8), 8);
    EXPECT_EQ(EffectiveAggressMulti(-4, 8), 8);
}

TEST(AIBehaviorMathTest, HunterStimulusEligibilityRules)
{
    // Gunshots: aquatic species ignore them, except the T-Rex; the hunt dog
    // never reacts to them.
    EXPECT_TRUE(HunterHearsShots(AI_ALLO, false));
    EXPECT_TRUE(HunterHearsShots(AI_TREX, true));
    EXPECT_FALSE(HunterHearsShots(AI_MOSA, true));
    EXPECT_FALSE(HunterHearsShots(AI_HUNTDOG, false));

    // Calls: the flying families and Brachiosaurus ignore hunter calls.
    EXPECT_TRUE(HunterHearsCalls(AI_ALLO));
    EXPECT_TRUE(HunterHearsCalls(AI_MOSA));
    EXPECT_FALSE(HunterHearsCalls(AI_DIMOR));
    EXPECT_FALSE(HunterHearsCalls(AI_PTERA));
    EXPECT_FALSE(HunterHearsCalls(AI_BRACH));
}

TEST(AIBehaviorMathTest, PackHuntAnchorExpiresWhenTheTrackerStopsReporting)
{
    constexpr int kMaxAge = 2048;
    EXPECT_FALSE(IsPackHuntAnchorFresh(10000, 0, kMaxAge));        // never reported
    EXPECT_TRUE(IsPackHuntAnchorFresh(10000, 9000, kMaxAge));      // fresh
    EXPECT_TRUE(IsPackHuntAnchorFresh(10000, 10000 - 2047, kMaxAge));
    EXPECT_FALSE(IsPackHuntAnchorFresh(10000, 10000 - 2048, kMaxAge)); // just stale
    EXPECT_FALSE(IsPackHuntAnchorFresh(10000, 7000, kMaxAge));     // stale
}

TEST(AIBehaviorMathTest, PackLeaderFollowsOnlyAFreshHuntAnchor)
{
    EXPECT_TRUE(ShouldFollowPackTarget(true, false));   // follower, anchor
    EXPECT_TRUE(ShouldFollowPackTarget(true, true));    // leader, anchor
    EXPECT_TRUE(ShouldFollowPackTarget(false, false));  // follower, leader fallback
    EXPECT_FALSE(ShouldFollowPackTarget(false, true));  // leader keeps its own target
}

TEST(AIBehaviorMathTest, EveryAwarenessStateHasAName)
{
    const HunterAwarenessState states[] = {
        HunterAwarenessState::None,
        HunterAwarenessState::InvestigatingShot,
        HunterAwarenessState::FleeingFromShot,
        HunterAwarenessState::RetaliatingHit,
        HunterAwarenessState::FleeingFromHit,
        HunterAwarenessState::FleeingFromCall,
        HunterAwarenessState::TrackingHunter,
    };
    for (HunterAwarenessState state : states)
        EXPECT_STRNE("?", HunterAwarenessStateName(state));
}

TEST(AIBehaviorMathTest, FixedFleeReroutesOnlyAfterReachingItsDestination)
{
    // Far away: the stored point stands and the creature keeps running at it.
    EXPECT_FALSE(FleeDestinationReached(0.0f, 0.0f, 0.0f, 2048.0f, 512.0f));
    // Exactly on the arrival radius: still approaching.
    EXPECT_FALSE(FleeDestinationReached(0.0f, 0.0f, 512.0f, 0.0f, 512.0f));
    // Inside the radius: the caller re-aims the next leg away from the hunter.
    EXPECT_TRUE(FleeDestinationReached(0.0f, 0.0f, 511.0f, 0.0f, 512.0f));
    EXPECT_TRUE(FleeDestinationReached(100.0f, -40.0f, 120.0f, -40.0f, 512.0f));
}

TEST(AIBehaviorMathTest, FleeLegRadiusTreatsALegAsADirection)
{
    // A flee leg only has to carry the creature away; the last stretch toward
    // an unreachable point must not be required before re-aiming.
    EXPECT_FALSE(FleeDestinationReached(0.0f, 0.0f, 900.0f, 0.0f,
        kShotInvestigationArrivalRadius));
    EXPECT_TRUE(FleeDestinationReached(0.0f, 0.0f, 900.0f, 0.0f,
        kFleeLegArrivalRadius));
    EXPECT_FALSE(FleeDestinationReached(0.0f, 0.0f, 1100.0f, 0.0f,
        kFleeLegArrivalRadius));
}

TEST(AIBehaviorMathTest, StuckFleeReaimsSweepSidesWithoutTurningBack)
{
    const float quarterPi = 3.14159265f / 2.0f;
    const float sixtyDegrees = 3.14159265f / 3.0f;
    EXPECT_NEAR(FleeLegStuckRotationAngle(1), sixtyDegrees, 1e-4f);
    EXPECT_NEAR(FleeLegStuckRotationAngle(2), -sixtyDegrees, 1e-4f);
    EXPECT_NEAR(FleeLegStuckRotationAngle(3), quarterPi, 1e-4f);
    EXPECT_NEAR(FleeLegStuckRotationAngle(4), -quarterPi, 1e-4f);
    EXPECT_NEAR(FleeLegStuckRotationAngle(9), quarterPi, 1e-4f);
    for (int attempt = 1; attempt <= 12; ++attempt)
    {
        const float angle = FleeLegStuckRotationAngle(attempt);
        const float magnitude = angle < 0.0f ? -angle : angle;
        EXPECT_LE(magnitude, quarterPi + 1e-4f);
        EXPECT_GE(magnitude, 1e-4f);
    }
}
