// CharacterMovement.cpp — auto-extracted from Characters.cpp
// ==========================================================================
// Auto-extracted from Characters.cpp
// ==========================================================================

#include "Hunt.h"
#include "CharacterInternal.h"

void Characters_AddSecondaryOne(TCharacter *cptr)
{

	if (!spawnGroup[cptr->SpawnGroupType].moveForward) return;

	if (ChCount > 64) return;
	Characters[ChCount].CType = cptr->CType;
	Characters[ChCount].SpawnGroupType = cptr->SpawnGroupType;
	Characters[ChCount].Clone = cptr->Clone;
	Characters[ChCount].cpcpAquatic = cptr->cpcpAquatic;
	int tr = 0;
replace1:
	tr++;
	if (tr > 128) return;
	Characters[ChCount].pos.x = PlayerX + siRand(20040);
	Characters[ChCount].pos.z = PlayerZ + siRand(20040);
	Characters[ChCount].pos.y = GetLandH(Characters[ChCount].pos.x,
		Characters[ChCount].pos.z);

	
	std::int32_t outside = true;
	for (int sr = 0; sr < spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegionCh; sr++) {
		if (Characters[ChCount].pos.x > spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegion[sr].XMin * 256 &&
			Characters[ChCount].pos.x < spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegion[sr].XMax * 256 &&
			Characters[ChCount].pos.z > spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegion[sr].YMin * 256 &&
			Characters[ChCount].pos.z < spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegion[sr].YMax * 256) outside = false;
	}
	if (outside) goto replace1;
	if (spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegionCh; ar++) {
			if (Characters[ChCount].pos.x > spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegion[ar].XMin * 256 &&
				Characters[ChCount].pos.x < spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegion[ar].XMax * 256 &&
				Characters[ChCount].pos.z > spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegion[ar].YMin * 256 &&
				Characters[ChCount].pos.z < spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegion[ar].YMax * 256) goto replace1;
		}
	}

	if (Characters[ChCount].Clone == AI_BRACH || Characters[ChCount].Clone == AI_BRACHDANGER || Characters[ChCount].Clone == AI_ICTH) {
		if (CheckPlaceCollisionBrahiP(Characters[ChCount].pos))goto replace1;
	} else if (Characters[ChCount].Clone == AI_FISH || Characters[ChCount].Clone == AI_MOSA) {
		if (CheckPlaceCollisionFishP(Characters[ChCount].pos,
			DinoInfo[Characters[ChCount].CType].minDepth,
			DinoInfo[Characters[ChCount].CType].maxDepth)) goto replace1;
	} else if (CheckPlaceCollisionP(Characters[ChCount].pos, Characters[ChCount].cpcpAquatic)) goto replace1;
	

	if (fabs(Characters[ChCount].pos.x - PlayerX) +
		fabs(Characters[ChCount].pos.z - PlayerZ) < 256 * 40)
		goto replace1;

	Characters[ChCount].tgx = Characters[ChCount].pos.x;
	Characters[ChCount].tgz = Characters[ChCount].pos.z;
	Characters[ChCount].tgtime = 0;

	if (Characters[ChCount].Clone == AI_FISH || Characters[ChCount].Clone == AI_MOSA) {
		Characters[ChCount].pos.y = GetLandUpH(Characters[ChCount].pos.x, Characters[ChCount].pos.z) -
			((GetLandUpH(Characters[ChCount].pos.x, Characters[ChCount].pos.z) - GetLandH(Characters[ChCount].pos.x, Characters[ChCount].pos.z)) / 2);
	}

	Characters[ChCount].packId = -1;	//Classic ambient- no pack hunting
	ResetCharacter(&Characters[ChCount]);
	ChCount++;
}
void MoveCharacterFish(TCharacter *cptr, float dx, float dz)
{
	//return;
	Vector3d p = cptr->pos;

	if (CheckPlaceCollisionFish(cptr, p, cptr->depth,
		DinoInfo[cptr->CType].maxDepth,
		DinoInfo[cptr->CType].minDepth))
	{
		cptr->pos.x += dx / 2;
		cptr->pos.z += dz / 2;
		return;
	}

	p.x += dx;
	p.z += dz;

	if (!CheckPlaceCollisionFish(cptr, p, cptr->depth,
		DinoInfo[cptr->CType].maxDepth,
		DinoInfo[cptr->CType].minDepth))
	{
		cptr->pos = p;
		return;
	}

	p = cptr->pos;
	p.x += dx / 2;
	p.z += dz / 2;
	if (!CheckPlaceCollisionFish(cptr, p, cptr->depth,
		DinoInfo[cptr->CType].maxDepth,
		DinoInfo[cptr->CType].minDepth)) cptr->pos = p;
	p = cptr->pos;

	p.x += dx / 4;
	//if (!CheckPlaceCollision2(p)) cptr->pos = p;
	p.z += dz / 4;
	//if (!CheckPlaceCollision2(p)) cptr->pos = p;
	cptr->pos = p;
}
void MoveCharacterMosasaurus(TCharacter *cptr, float dx, float dz)
{
	//return;
	Vector3d p = cptr->pos;

	if (CheckPlaceCollisionMosasaurus(cptr, p, cptr->depth))
	{
		cptr->pos.x += dx / 2;
		cptr->pos.z += dz / 2;
		return;
	}

	p.x += dx;
	p.z += dz;

	if (!CheckPlaceCollisionMosasaurus(cptr, p, cptr->depth))
	{
		cptr->pos = p;
		return;
	}

	p = cptr->pos;
	p.x += dx / 2;
	p.z += dz / 2;
	if (!CheckPlaceCollisionMosasaurus(cptr, p, cptr->depth)) cptr->pos = p;
	p = cptr->pos;

	p.x += dx / 4;
	//if (!CheckPlaceCollision2(p)) cptr->pos = p;
	p.z += dz / 4;
	//if (!CheckPlaceCollision2(p)) cptr->pos = p;
	cptr->pos = p;
}
void MoveCharacter(TCharacter *cptr, float dx, float dz, std::int32_t wc, std::int32_t mc)
{
	//return;
	Vector3d p = cptr->pos;

	if (CheckPlaceCollision2(cptr, p, wc))
	{
		cptr->pos.x += dx / 2;
		cptr->pos.z += dz / 2;
		return;
	}

	p.x += dx;
	p.z += dz;

	if (!CheckPlaceCollision2(cptr, p, wc))
	{
		cptr->pos = p;
		return;
	}

	p = cptr->pos;
	p.x += dx / 2;
	p.z += dz / 2;
	if (!CheckPlaceCollision2(cptr, p, wc)) cptr->pos = p;
	p = cptr->pos;

	p.x += dx / 4;
	//if (!CheckPlaceCollision2(p)) cptr->pos = p;
	p.z += dz / 4;
	//if (!CheckPlaceCollision2(p)) cptr->pos = p;
	cptr->pos = p;
}
void MoveCharacter2(TCharacter *cptr, float dx, float dz)
{
	cptr->pos.x += dx;
	cptr->pos.z += dz;
}
