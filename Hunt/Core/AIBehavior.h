// ==========================================================================
// AIBehavior.h -- Dependency-free hunter awareness state helpers
// ==========================================================================
#pragma once

#include <cstdint>

#include "Constants.h"

enum class HunterAwarenessState : std::uint8_t
{
    None,
    InvestigatingShot,
    FleeingFromShot,
    RetaliatingHit,
    FleeingFromHit,
    FleeingFromCall,
    TrackingHunter
};

inline HunterAwarenessState HeardShotReactionState(bool flees)
{
    return flees
        ? HunterAwarenessState::FleeingFromShot
        : HunterAwarenessState::InvestigatingShot;
}

inline const char* HunterAwarenessStateName(HunterAwarenessState state)
{
    switch (state)
    {
    case HunterAwarenessState::None: return "None";
    case HunterAwarenessState::InvestigatingShot: return "InvestigatingShot";
    case HunterAwarenessState::FleeingFromShot: return "FleeingFromShot";
    case HunterAwarenessState::RetaliatingHit: return "RetaliatingHit";
    case HunterAwarenessState::FleeingFromHit: return "FleeingFromHit";
    case HunterAwarenessState::FleeingFromCall: return "FleeingFromCall";
    case HunterAwarenessState::TrackingHunter: return "TrackingHunter";
    }
    return "?";
}

inline HunterAwarenessState DirectHitReactionState(bool flees)
{
    return flees
        ? HunterAwarenessState::FleeingFromHit
        : HunterAwarenessState::RetaliatingHit;
}

inline HunterAwarenessState UpdatedDirectHitAwarenessState(
    HunterAwarenessState current, bool flees)
{
    return current == HunterAwarenessState::TrackingHunter && !flees
        ? current
        : DirectHitReactionState(flees);
}

inline bool ShouldInitializeDirectHitAlert(bool survived, bool wasAware)
{
    return survived && !wasAware;
}

// One authored-fear rule shared by the hunter-event classifiers and the
// per-frame flee decision (structural issue S1 in hunter-awareness-review.md):
// a defensive species at full health, a species that fears injury once hurt,
// and a species that fears shot sounds always flee from the stimulus instead
// of investigating or retaliating. Evaluating the same inputs at event time
// and per frame keeps the two decisions from disagreeing -- a disagreement
// used to leave an injured fearShotHit species fleeing from the live hunter
// coordinates while its state said "fixed shot investigation".
inline bool FearsHunterEvent(bool fearsShotSound, bool defensiveAtFullHealth,
                             bool injuredAndFearsShot)
{
    return fearsShotSound || defensiveAtFullHealth || injuredAndFearsShot;
}

// A hunter event (heard shot or direct hit) is a stronger stimulus than
// passive detection: the species' authored aggression range is scaled up for
// the reaction (see GetCharacterHunterEventRange). Authored fear and
// passivity always flee. A species whose authored range does not cover the
// event -- a low-aggression herbivore, for example -- also flees instead of
// charging the source. `alwaysRespondAggressively` is the T-Rex exception
// (its `aggress` value is intentionally omitted).
inline bool ShouldFleeFromHunterEvent(int aggression, bool fearsEvent,
                                      float eventDistance, float eventRange,
                                      bool alwaysRespondAggressively = false)
{
    return !alwaysRespondAggressively
        && (fearsEvent || aggression <= 0 || eventDistance > eventRange);
}

inline bool IsFixedHunterPursuitState(HunterAwarenessState state)
{
    return state == HunterAwarenessState::InvestigatingShot
        || state == HunterAwarenessState::RetaliatingHit;
}

inline bool IsFixedHunterFleeState(HunterAwarenessState state)
{
    return state == HunterAwarenessState::FleeingFromShot
        || state == HunterAwarenessState::FleeingFromHit
        || state == HunterAwarenessState::FleeingFromCall;
}

inline bool IsTimedHunterReactionState(HunterAwarenessState state)
{
    return IsFixedHunterPursuitState(state) || IsFixedHunterFleeState(state);
}

// A member in a fixed shot or hit reaction alarms its pack the same way a
// live flee or a tracking member does. The per-animator alarm writes are
// guarded by !fixedReaction, so the navigator owns this case; the alarm only
// wakes packmates (leader-follow or scatter) and never publishes hunter
// coordinates or kill authority.
inline bool ShouldAlarmPack(HunterAwarenessState state)
{
    return IsFixedHunterPursuitState(state) || IsFixedHunterFleeState(state);
}

// Hunter-stimulus eligibility. The handlers used to repeat these clone and
// species checks; they live here as pure rules so the matrix is testable and
// each caller stays one line.
//
// Gunshots: aquatic species cannot react to them on land, except the T-Rex
// (which swims and hunts); the hunt dog follows its own search logic instead.
inline bool HunterHearsShots(int clone, bool aquatic)
{
    return (!aquatic || clone == AI_TREX) && clone != AI_HUNTDOG;
}

// Hunter calls are ignored by the flying families and by Brachiosaurus.
inline bool HunterHearsCalls(int clone)
{
    return clone != AI_DIMOR && clone != AI_PTERA && clone != AI_BRACH;
}

// A species-level override of the AI-family aggression multiplier wins when
// authored in _RES.TXT ("agressMulti"); otherwise the clone table default
// applies. Values below 1 count as absent, so a bad mod value cannot divide
// by zero or invert the formula.
inline int EffectiveAggressMulti(int speciesOverride, int cloneDefault)
{
    return speciesOverride >= 1 ? speciesOverride : cloneDefault;
}

// A tracking pack member publishes its own position as the pack's hunt
// anchor. The anchor is fresh while the tracker keeps reporting; once it goes
// stale the pack falls back to the leader position. reportedAt == 0 means no
// member has reported since the pack was created.
inline bool IsPackHuntAnchorFresh(int now, int reportedAt, int maxAge)
{
    return reportedAt != 0 && now - reportedAt < maxAge;
}

// The pack follows a fresh hunt anchor -- the position of the member that
// found the hunter -- and falls back to the leader position without one. The
// leader itself only follows an anchor: it never takes a leader-relative
// target, or it would freeze on its own position.
inline bool ShouldFollowPackTarget(bool anchorFresh, bool isLeader)
{
    return anchorFresh || !isLeader;
}

// The single owner of the untimed reaction timers (exact tracking and the
// morale timer). Timed fixed reactions keep their dedicated central tick so
// they can clear the awareness state on expiry; everything else ticks exactly
// once per frame here, so no tracking lock can live forever.
inline int TickUntimedReaction(int afraidTime, int elapsed)
{
    return afraidTime > elapsed ? afraidTime - elapsed : 0;
}

// A kill always requires the hunter to be physically inside the attack
// reach; nothing authorizes a kill at a distance. Exact tracking authorizes
// it during a chase. A fixed flee reaction also authorizes it at contact:
// a huge body crossing the hunter's position crushes them (trample) even
// though the creature has no intent to attack. Fixed pursuits are not listed
// because contact-range awareness promotes them to tracking first.
inline bool HunterAwarenessAllowsKill(HunterAwarenessState state)
{
    return state == HunterAwarenessState::TrackingHunter
        || IsFixedHunterFleeState(state);
}

inline bool IsWithinSquaredReach(float distanceSquared, float reach)
{
    return reach > 0.0f && distanceSquared < reach * reach;
}

inline bool IsWithinLinearReach(float distance, float reach)
{
    return reach > 0.0f && distance < reach;
}

inline bool IsWithinKillAltitude(float verticalDifference, float verticalReach)
{
    return verticalDifference < verticalReach;
}

// A direct hit must cancel any pending look/roar notice and start the charge.
// Repeated hits during an active retaliation or exact tracking keep the
// current pursuit instead of restarting it.
inline bool ShouldRestartTRexHitPursuit(HunterAwarenessState priorAwareness)
{
    return priorAwareness != HunterAwarenessState::RetaliatingHit
        && priorAwareness != HunterAwarenessState::TrackingHunter;
}

// The look/smell notice animation plays only for a new detection. Any active
// reaction -- a timed shot/hit response or an established tracking lock -- must
// not be interrupted by a replay of the notice; the awareness upgrade or the
// timer refresh still happens underneath. This is what lets the T-Rex keep
// re-perceiving (and so refresh its tracking lock) mid-pursuit without
// stopping to roar at the hunter it is already chasing.
inline bool ShouldScheduleNoticeAnimation(HunterAwarenessState awareness)
{
    return awareness == HunterAwarenessState::None;
}

// The authored look offset moves the hunter distance to the creature's
// reaction point (snout or head) for a few families. Kept as a pure table so
// the shared hunter-geometry function and the animators cannot drift. Pass
// AIInfo[clone].carnivore for the fallback rule.
inline float HunterLookOffset(int clone, float scale, bool carnivore)
{
    switch (clone)
    {
    case AI_ALLO:
    case AI_MOSA:
    case AI_FISH:
        return 100.0f * scale;
    case AI_CHASM:
    case AI_HOG:
    case AI_BRONT:
    case AI_BEAR:
    case AI_WOLF:
    case AI_RHINO:
    case AI_SMILO:
        return 300.0f * scale;
    case AI_BRACHDANGER:
    case AI_LANDBRACH:
    case AI_TREX:
        return 108.0f;
    default:
        return carnivore ? 108.0f : 0.0f;
    }
}

// The authored flee/pursue rule shared by the standard predator family
// (AnimateHuntable, AnimateBrahi). The engine wrapper derives the inputs from
// the species flags and the current reaction, so the rule itself stays a pure,
// testable predicate instead of a per-animator if-chain. Any authored fear
// response -- a defensive species at full health, a shot-fearing species that
// is already hurt, or a creature told to flee the shot -- overrides aggression.
inline bool ShouldFleeFromAuthoredThreat(bool outsideAggressionRange,
                                         bool passive,
                                         bool aware,
                                         bool defensiveAtFullHealth,
                                         bool injuredAndFearsShot,
                                         bool fleeingFromShot)
{
    return outsideAggressionRange || passive || !aware
        || defensiveAtFullHealth || injuredAndFearsShot || fleeingFromShot;
}

// A creature following a remembered event position (shot investigation or hit
// retaliation) still notices a hunter who physically enters its attack reach.
// Contact-range presence is stronger evidence than the stored event point, so
// the fixed pursuit upgrades to exact tracking. The stored point alone never
// authorizes a kill at a distance, and flee reactions are not affected. This
// grants no active sight or smell: a species authored not to look or smell for
// the hunter still ignores it while idle.
inline bool ShouldPromotePursuitToTracking(bool isFixedPursuit,
                                           bool verticalInRange,
                                           float hunterDistanceSquared,
                                           float attackReachSquared)
{
    return isFixedPursuit && verticalInRange && attackReachSquared > 0.0f
        && hunterDistanceSquared <= attackReachSquared;
}

// A fixed flee reaction stores a destination point and re-aims the next leg
// only once the creature has reached it. Extending along the creature's own
// heading instead let a creature that had overshot its point turn back toward
// the hunter: the heading at the moment of arrival is the direction of the
// turn, not the escape, so the next leg marched sideways or straight back at
// the hunter. Returns true when the point is inside the arrival radius; the
// caller then picks a fresh away-from-hunter destination through the
// placement check.
inline bool FleeDestinationReached(float positionX, float positionZ,
                                   float destinationX, float destinationZ,
                                   float arrivalRadius)
{
    const float dx = destinationX - positionX;
    const float dz = destinationZ - positionZ;
    return dx * dx + dz * dz < arrivalRadius * arrivalRadius;
}

// Rotation bias, in radians, for the Nth stuck flee re-aim (1-based):
// alternately left and right in 60-degree steps, capped at 90 degrees so a
// retry can never turn the escape ray back toward the hunter.
inline float FleeLegStuckRotationAngle(int attempt)
{
    if (attempt < 1) attempt = 1;
    const int tier = (attempt + 1) / 2;
    const float degrees = (tier * 60.0f > 90.0f) ? 90.0f : tier * 60.0f;
    const float radians = degrees * 3.14159265f / 180.0f;
    return (attempt % 2) ? radians : -radians;
}
