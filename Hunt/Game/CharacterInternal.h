// ==========================================================================
// CharacterInternal.h — Internal declarations shared across Character module
// ==========================================================================
// Functions declared here are used by multiple Character/*.cpp files but are
// not exposed to the rest of the engine (not in EngineAPI.h).

#pragma once

#include <cmath>

#include "Core/GameTypes.h"

// Collision / placement checks
int CheckPlaceCollisionP(Vector3d &v, bool aquatic);
int CheckPlaceCollisionFishP(Vector3d &v, int minDepth, int maxDepth);
int CheckPlaceCollisionFish(TCharacter *cptr, Vector3d &v, float mosaDepth, int maxDepth, int minDepth);
int CheckPlaceCollisionMosasaurus(TCharacter *cptr, Vector3d &v, float mosaDepth);
bool jumpCollision(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc);
int CheckPlaceCollision(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc);
int CheckPlaceCollisionMicro(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc);
int CheckPlaceCollisionLandBrahi(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc);
int CheckPlaceCollisionBrahi(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc);
int CheckPlaceCollisionBrahiP(Vector3d &v);
int CheckPlaceCollision2(TCharacter *cptr, Vector3d &v, std::int32_t wc);
int CheckPossiblePath(TCharacter *cptr, std::int32_t wc, std::int32_t mc);

// Movement
void MoveCharacter(TCharacter *cptr, float dx, float dz, std::int32_t wc, std::int32_t mc);
void MoveCharacter2(TCharacter *cptr, float dx, float dz);
void MoveCharacterFish(TCharacter *cptr, float dx, float dz);
void MoveCharacterMosasaurus(TCharacter *cptr, float dx, float dz);
void Characters_AddSecondaryOne(TCharacter *cptr);
void LookForAWay(TCharacter *cptr, std::int32_t wc, std::int32_t mc);

// AI helpers
// Diagnostic event line for the AI trace (ai_logging 1): one call per
// awareness acquisition or stimulus, with the numbers that decided it.
// Kinds in use: shot, hit, call, contact, sight, scent, startle, proximity.
void TraceHunterEvent(const TCharacter *cptr, const char *kind, float distance,
                      float eventRange, float hearingRange);
void SetNewTargetPlace(TCharacter *cptr, float R);
void SetNewTargetPlaceVanilla(TCharacter *cptr, float R);
void SetNewTargetPlaceRegion(TCharacter *cptr, float R);
void SetNewTargetPlace_Icth(TCharacter *cptr, float R);
void SetNewTargetPlace_IcthOld(TCharacter *cptr, float R);
void SetNewTargetPlace_Brahi(TCharacter *cptr, float R);
void SetNewTargetPlaceFish(TCharacter *cptr, float R);
void SetNewTargetPlaceMosasaurus(TCharacter *cptr, float R);
std::int32_t ReplaceCharacterForward(TCharacter *cptr);
std::uint8_t huntDogSearch(TCharacter *cptr);

// Character lifecycle
void ResetCharacter(TCharacter *cptr);
void ProcessPrevPhase(TCharacter *cptr);
void ActivateCharacterFx(TCharacter *cptr);
void ActivateCharacterFxAquatic(TCharacter *cptr);
void AddDeadBody(TCharacter *cptr, int phase, bool scream);

// Animation helpers
void ThinkY_Beta_Gamma(TCharacter *cptr, float blook, float glook, float blim, float glim);
Vector3d LookForATree(TCharacter *cptr);
Vector3d CheckForATree(TCharacter *cptr);
float AngleDifference(float a, float b);
float CorrectedAlpha(float a, float b);

// Inline helpers
inline float GetAngleDifference(float a, float b) { return AngleDifference(a, b); }

// Keep normal tracking and fixed event reactions on the same authored range.
inline float GetCharacterAggressionRange(const TCharacter* cptr)
{
    const TDinoInfo& dino = DinoInfo[cptr->CType];
    const TAIInfo& behavior = AIInfo[cptr->Clone];
    // A species-level "agressMulti" override replaces the clone table value;
    // absent (0) keeps the legacy AI-family default. The Brachiosaurus branch
    // is a legacy formula and is not affected by this override.
    const int aggressMulti = EffectiveAggressMulti(
        DinoAggressMulti[cptr->CType], behavior.agressMulti);
    float aggressionRange;

    if (cptr->Clone == AI_BRACH) {
        aggressionRange = 128.0f * dino.aggress + OptAgres / 8.0f;
    } else if (dino.Aquatic && cptr->Clone != AI_TREX) {
        const int optionAggression = dino.DangerFish ? OptAgres : 0;
        aggressionRange = GameplayViewRadiusCells(ctViewR) * dino.aggress
            + optionAggression / static_cast<float>(aggressMulti);
    } else if (behavior.carnivore
               && (!behavior.iceAge || cptr->Clone == AI_WOLF)) {
        aggressionRange = GameplayViewRadiusCells(ctViewR) * dino.aggress
            + OptAgres / static_cast<float>(aggressMulti);
    } else {
        aggressionRange = aggressMulti * dino.aggress
            + OptAgres / 8.0f;
    }

    return cptr->gliding ? aggressionRange * 2.0f : aggressionRange;
}

// A hunter event is a stronger stimulus than passive detection, so the event
// reaction uses a scaled copy of the authored aggression range (see
// kHunterEventRangeScale in Constants.h).
inline float GetCharacterHunterEventRange(const TCharacter* cptr)
{
    return GetCharacterAggressionRange(cptr) * kHunterEventRangeScale;
}

inline bool IsInvestigatingShot(const TCharacter* cptr)
{
    return cptr->hunterAwareness == HunterAwarenessState::InvestigatingShot;
}

inline bool IsFixedHunterPursuit(const TCharacter* cptr)
{
    return IsFixedHunterPursuitState(cptr->hunterAwareness);
}

inline bool IsFixedHunterFlee(const TCharacter* cptr)
{
    return IsFixedHunterFleeState(cptr->hunterAwareness);
}

inline bool IsTimedHunterReaction(const TCharacter* cptr)
{
    return IsTimedHunterReactionState(cptr->hunterAwareness);
}

inline bool TracksHunterExactly(const TCharacter* cptr)
{
    return cptr->hunterAwareness == HunterAwarenessState::TrackingHunter;
}

// The single awareness state: the hunter is relevant to this creature while
// any reaction is active. Replaces the legacy awareHunter boolean, which was
// always written in lockstep with hunterAwareness.
inline bool IsHunterAware(const TCharacter* cptr)
{
    return cptr->hunterAwareness != HunterAwarenessState::None;
}

// Contact-range awareness: while a creature follows a remembered event
// position it still notices a hunter who physically enters its attack reach.
// The horizontal reach is the authored attack distance (scaled, matching the
// aquatic kill check); the vertical tolerance mirrors the animator kill
// branches (at least 256 units of altitude difference, or the species water
// depth, plus the aquatic jumping allowance) so a creature on a cliff does not
// notice a hunter standing far below. This grants no active sight or smell.
inline bool ShouldPromoteFixedPursuitToTracking(const TCharacter* cptr,
                                                const Vector3d& hunterPosition)
{
    const TDinoInfo& dino = DinoInfo[cptr->CType];
    const float attackReach = dino.killDist * cptr->scale;
    if (attackReach <= 0.0f)
        return false;

    float verticalReach = dino.Aquatic
        ? static_cast<float>(cptr->spcDepth)
        : static_cast<float>(dino.waterLevel);
    if (verticalReach < 256.0f)
        verticalReach = 256.0f;
    if (dino.Aquatic && AIInfo[cptr->Clone].jumper
        && cptr->Phase == dino.jumpAnim)
        verticalReach += 80.0f;

    const float dx = hunterPosition.x - cptr->pos.x;
    const float dz = hunterPosition.z - cptr->pos.z;

    return ShouldPromotePursuitToTracking(
        IsFixedHunterPursuit(cptr),
        fabs(hunterPosition.y - cptr->pos.y) <= verticalReach + 20.0f,
        dx * dx + dz * dz, attackReach * attackReach);
}

// A pack member without its own tracking follows the pack's hunt anchor --
// the position of the packmate that is currently tracking the hunter, while
// it is fresh -- and falls back to the leader's live position; when afraid it
// flees radially away from that point. The leader itself only follows a fresh
// anchor: without one it keeps its own event or wander destination instead of
// freezing on its own position. The anchor is a packmate's position, never
// the hunter's.
inline void SetPackLeaderTarget(TCharacter* cptr, bool flee)
{
    if (cptr->packId < 0 || !Packs[cptr->packId].leader) return;

    const TPack& pack = Packs[cptr->packId];
    const bool anchorFresh = IsPackHuntAnchorFresh(
        RealTime, PackHuntTime[cptr->packId], kPackHuntAnchorTime);
    if (!ShouldFollowPackTarget(anchorFresh, pack.leader == cptr)) return;

    const float targetX = anchorFresh ? PackHuntX[cptr->packId] : pack.leader->pos.x;
    const float targetZ = anchorFresh ? PackHuntZ[cptr->packId] : pack.leader->pos.z;
    if (!flee) {
        cptr->tgx = targetX;
        cptr->tgz = targetZ;
        cptr->tgtime = 0;
        return;
    }

    Vector3d away;
    away.x = cptr->pos.x - targetX;
    away.y = 0.0f;
    away.z = cptr->pos.z - targetZ;
    NormVector(away, 2048.0f);
    cptr->tgx = cptr->pos.x + away.x;
    cptr->tgz = cptr->pos.z + away.z;
    cptr->tgtime = 0;
}

// Character targets must stay inside the playable map; the legacy target
// pickers clamp to these bounds and an unbounded flee/search extension can
// otherwise park a creature against the world edge.
inline float ClampCharacterTargetCoordinate(float value)
{
    constexpr float kTargetMin = 512.0f;
    constexpr float kTargetMax = 1018.0f * 256.0f;
    if (value < kTargetMin) return kTargetMin;
    if (value > kTargetMax) return kTargetMax;
    return value;
}

inline void ClearHunterReaction(TCharacter* cptr)
{
    cptr->hunterAwareness = HunterAwarenessState::None;
    cptr->AfraidTime = 0;
    cptr->State = 0;
    cptr->tgx = cptr->pos.x;
    cptr->tgz = cptr->pos.z;
    cptr->tgtime = 0;
}

// A fixed flee reaction stores one point rather than a direction. Once the
// creature reaches it, the target sits behind it and it turns back, which
// looks like circling. Extend the point along the flee direction so the
// creature keeps running while the reaction lasts.
// Pack following helpers
// Called from animation functions to maintain pack formation
inline void AnimatePackFollow(TCharacter* cptr, float leaderDist)
{
    if (cptr->followLeader) {
        if (leaderDist < cptr->packDensity * 128.0f * 0.6f)
            cptr->followLeader = false;
    } else {
        if (leaderDist > cptr->packDensity * 128.0f * 1.3f)
            cptr->followLeader = true;
    }
}

inline void AnimatePackFollowSq(TCharacter* cptr, float leaderDistSq)
{
    if (cptr->followLeader) {
        float minDist = cptr->packDensity * 128.0f * 0.6f;
        if (leaderDistSq < minDist * minDist)
            cptr->followLeader = false;
    } else {
        float maxDist = cptr->packDensity * 128.0f * 1.3f;
        if (leaderDistSq > maxDist * maxDist)
            cptr->followLeader = true;
    }
}
