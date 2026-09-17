// ==========================================================================
// CharacterInternal.h — Internal declarations shared across Character module
// ==========================================================================
// Functions declared here are used by multiple Character/*.cpp files but are
// not exposed to the rest of the engine (not in EngineAPI.h).

#pragma once

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
