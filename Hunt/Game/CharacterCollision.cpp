// CharacterCollision.cpp — auto-extracted from Characters.cpp
// ==========================================================================
// Auto-extracted from Characters.cpp
// ==========================================================================

#include "Hunt.h"
#include "CharacterInternal.h"

int CheckPlaceCollisionP(Vector3d &v, bool aquatic)
{
	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1008 || ccz>1008) return 1;

	int F = (FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
		FMap[ccz][ccx] |
		FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]);

	if (aquatic) {
		if (F & fmNOWAY) return 1;
	}
	else if (F & (fmWater + fmNOWAY)) return 1;


	


	float h = GetLandH(v.x, v.z);
	v.y = h;

	float hh = GetLandH(v.x - 164, v.z - 164);
	if (fabs(hh - h) > 160) return 1;
	hh = GetLandH(v.x + 164, v.z - 164);
	if (fabs(hh - h) > 160) return 1;
	hh = GetLandH(v.x - 164, v.z + 164);
	if (fabs(hh - h) > 160) return 1;
	hh = GetLandH(v.x + 164, v.z + 164);
	if (fabs(hh - h) > 160) return 1;

	for (int z = -2; z <= 2; z++)
		for (int x = -2; x <= 2; x++)
			if (OMap[ccz + z][ccx + x] != 255)
			{
				int ob = OMap[ccz + z][ccx + x];
				if (MObjects[ob].info.Radius < 10) continue;
				float CR = static_cast<float>(MObjects[ob].info.Radius) + 64;

				float oz = (ccz + z) * 256.f + 128.f;
				float ox = (ccx + x) * 256.f + 128.f;

				float r = static_cast<float>(sqrt((ox - v.x)*(ox - v.x) + (oz - v.z)*(oz - v.z)));
				if (r < CR) return 1;
			}

	return 0;
}
int CheckPlaceCollisionFishP(Vector3d &v, int minDepth, int maxDepth)
{
	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1008 || ccz>1008) return 1;
	
	if ((GetLandUpH(v.x, v.z) - GetLandH(v.x, v.z)) < minDepth ||
		(GetLandUpH(v.x + 256, v.z) - GetLandH(v.x + 256, v.z)) < minDepth ||
		(GetLandUpH(v.x, v.z + 256) - GetLandH(v.x, v.z + 256)) < minDepth ||
		(GetLandUpH(v.x + 256, v.z + 256) - GetLandH(v.x + 256, v.z + 256)) < minDepth ||
		(GetLandUpH(v.x - 256, v.z) - GetLandH(v.x - 256, v.z)) < minDepth ||
		(GetLandUpH(v.x, v.z - 256) - GetLandH(v.x, v.z - 256)) < minDepth ||
		(GetLandUpH(v.x - 256, v.z - 256) - GetLandH(v.x - 256, v.z - 256)) < minDepth ||
		(GetLandUpH(v.x + 256, v.z - 256) - GetLandH(v.x + 256, v.z - 256)) < minDepth ||
		(GetLandUpH(v.x - 256, v.z + 256) - GetLandH(v.x - 256, v.z + 256)) < minDepth) return 1;
		
	if ((GetLandUpH(v.x, v.z) - GetLandH(v.x, v.z)) > maxDepth ||
		(GetLandUpH(v.x + 256, v.z) - GetLandH(v.x + 256, v.z)) > maxDepth ||
		(GetLandUpH(v.x, v.z + 256) - GetLandH(v.x, v.z + 256)) > maxDepth ||
		(GetLandUpH(v.x + 256, v.z + 256) - GetLandH(v.x + 256, v.z + 256)) > maxDepth ||
		(GetLandUpH(v.x - 256, v.z) - GetLandH(v.x - 256, v.z)) > maxDepth ||
		(GetLandUpH(v.x, v.z - 256) - GetLandH(v.x, v.z - 256)) > maxDepth ||
		(GetLandUpH(v.x - 256, v.z - 256) - GetLandH(v.x - 256, v.z - 256)) > maxDepth ||
		(GetLandUpH(v.x + 256, v.z - 256) - GetLandH(v.x + 256, v.z - 256)) > maxDepth ||
		(GetLandUpH(v.x - 256, v.z + 256) - GetLandH(v.x - 256, v.z + 256)) > maxDepth) return 1;
		
	return 0;
}
int CheckPlaceCollisionFish(TCharacter *cptr, Vector3d &v, float mosaDepth, int maxDepth, int minDepth)
{

	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1018 || ccz>1018) return 1;

	/*if (wc)
		if ((FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
			FMap[ccz][ccx] |
			FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]) & fmWater)
			return 1;
	 */

	if (spawnGroup[cptr->SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[cptr->SpawnGroupType].avoidRegionCh; ar++) {
			if (ccx > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMin &&
				ccx < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMax &&
				ccz > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMin &&
				ccz < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMax) return 1;
		}
	}

	// #C1 KEEP THIS
	if ((GetLandUpH(v.x, v.z) - GetLandH(v.x, v.z)) < minDepth ||
		(GetLandUpH(v.x + 256, v.z) - GetLandH(v.x + 256, v.z)) < minDepth ||
		(GetLandUpH(v.x, v.z + 256) - GetLandH(v.x, v.z + 256)) < minDepth ||
		(GetLandUpH(v.x + 256, v.z + 256) - GetLandH(v.x + 256, v.z + 256)) < minDepth ||
		(GetLandUpH(v.x - 256, v.z) - GetLandH(v.x - 256, v.z)) < minDepth ||
		(GetLandUpH(v.x, v.z - 256) - GetLandH(v.x, v.z - 256)) < minDepth ||
		(GetLandUpH(v.x - 256, v.z - 256) - GetLandH(v.x - 256, v.z - 256)) < minDepth ||
		(GetLandUpH(v.x + 256, v.z - 256) - GetLandH(v.x + 256, v.z - 256)) < minDepth ||
		(GetLandUpH(v.x - 256, v.z + 256) - GetLandH(v.x - 256, v.z + 256)) < minDepth) return 1;

	if ((GetLandUpH(v.x, v.z) - GetLandH(v.x, v.z)) > maxDepth ||
		(GetLandUpH(v.x + 256, v.z) - GetLandH(v.x + 256, v.z)) > maxDepth ||
		(GetLandUpH(v.x, v.z + 256) - GetLandH(v.x, v.z + 256)) > maxDepth ||
		(GetLandUpH(v.x + 256, v.z + 256) - GetLandH(v.x + 256, v.z + 256)) > maxDepth ||
		(GetLandUpH(v.x - 256, v.z) - GetLandH(v.x - 256, v.z)) > maxDepth ||
		(GetLandUpH(v.x, v.z - 256) - GetLandH(v.x, v.z - 256)) > maxDepth ||
		(GetLandUpH(v.x - 256, v.z - 256) - GetLandH(v.x - 256, v.z - 256)) > maxDepth ||
		(GetLandUpH(v.x + 256, v.z - 256) - GetLandH(v.x + 256, v.z - 256)) > maxDepth ||
		(GetLandUpH(v.x - 256, v.z + 256) - GetLandH(v.x - 256, v.z + 256)) > maxDepth) return 1;



	// #C1 REMOVE THESE
	//if (mosaDepth > GetLandUpH(v.x, v.z) - 700) return 1;
	//if (mosaDepth < GetLandH(v.x, v.z) + 500) return 1;

	/*
	float h = GetLandH(v.x, v.z);
	if (fabs(h - v.y) > 64) return 1;

	v.y = h;

	float hh = GetLandH(v.x - 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x - 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;

	if (mc)
		for (int z = -2; z <= 2; z++)
			for (int x = -2; x <= 2; x++)
				if (OMap[ccz + z][ccx + x] != 255)
				{
					int ob = OMap[ccz + z][ccx + x];
					if (MObjects[ob].info.Radius < 10) continue;
					float CR = static_cast<float>(MObjects[ob].info.Radius) + 64;

					float oz = (ccz + z) * 256.f + 128.f;
					float ox = (ccx + x) * 256.f + 128.f;

					float r = static_cast<float>(sqrt((ox - v.x)*(ox - v.x) + (oz - v.z)*(oz - v.z)));
					if (r < CR) return 1;
				}

				*/
	return 0;
}
int CheckPlaceCollisionMosasaurus(TCharacter *cptr, Vector3d &v, float mosaDepth)
{

	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1018 || ccz>1018) return 1;

	/*if (wc)
		if ((FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
			FMap[ccz][ccx] |
			FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]) & fmWater)
			return 1;
	 */

	if (spawnGroup[cptr->SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[cptr->SpawnGroupType].avoidRegionCh; ar++) {
			if (ccx > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMin &&
				ccx < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMax &&
				ccz > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMin &&
				ccz < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMax) return 1;
		}
	}

	// #C1 KEEP THIS
	if ((GetLandUpH(v.x, v.z) - GetLandH(v.x, v.z)) < 1500 ||
		(GetLandUpH(v.x + 256, v.z) - GetLandH(v.x + 256, v.z)) < 1500 ||
		(GetLandUpH(v.x, v.z + 256) - GetLandH(v.x, v.z + 256)) < 1500 ||
		(GetLandUpH(v.x + 256, v.z + 256) - GetLandH(v.x + 256, v.z + 256)) < 1500 ||
		(GetLandUpH(v.x - 256, v.z) - GetLandH(v.x - 256, v.z)) < 1500 ||
		(GetLandUpH(v.x, v.z - 256) - GetLandH(v.x, v.z - 256)) < 1500 ||
		(GetLandUpH(v.x - 256, v.z - 256) - GetLandH(v.x - 256, v.z - 256)) < 1500 ||
		(GetLandUpH(v.x + 256, v.z - 256) - GetLandH(v.x + 256, v.z - 256)) < 1500 ||
		(GetLandUpH(v.x - 256, v.z + 256) - GetLandH(v.x - 256, v.z + 256)) < 1500) return 1;

	// #C1 REMOVE THESE
	//if (mosaDepth > GetLandUpH(v.x, v.z) - 700) return 1;
	//if (mosaDepth < GetLandH(v.x, v.z) + 500) return 1;

	/*
	float h = GetLandH(v.x, v.z);
	if (fabs(h - v.y) > 64) return 1;

	v.y = h;

	float hh = GetLandH(v.x - 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x - 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;

	if (mc)
		for (int z = -2; z <= 2; z++)
			for (int x = -2; x <= 2; x++)
				if (OMap[ccz + z][ccx + x] != 255)
				{
					int ob = OMap[ccz + z][ccx + x];
					if (MObjects[ob].info.Radius < 10) continue;
					float CR = static_cast<float>(MObjects[ob].info.Radius) + 64;

					float oz = (ccz + z) * 256.f + 128.f;
					float ox = (ccx + x) * 256.f + 128.f;

					float r = static_cast<float>(sqrt((ox - v.x)*(ox - v.x) + (oz - v.z)*(oz - v.z)));
					if (r < CR) return 1;
				}

				*/
	return 0;
}
bool jumpCollision(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc)
{
	Vector3d p = cptr->pos;
	float lookx = static_cast<float>(cos(cptr->tgalpha));
	float lookz = static_cast<float>(sin(cptr->tgalpha));
	for (int i = 0; i < 10; i++) {

		p.x += lookx * 64.f;
		p.z += lookz * 64.f;
		if (CheckPlaceCollision(cptr, p, wc, mc)) {
			return false;
		};
	}
	return true;
}
int CheckPlaceCollision(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc)
{
	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1018 || ccz>1018) return 1;

	if (wc)
		if ((FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
			FMap[ccz][ccx] |
			FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]) & fmWater)
			return 1;


	float h = GetLandH(v.x, v.z);
	if (!(FMap[ccz][ccx] & fmWater))
		if (fabs(h - v.y) > 64) return 1;

	if (spawnGroup[cptr->SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[cptr->SpawnGroupType].avoidRegionCh; ar++) {
			if (ccx > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMin &&
				ccx < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMax &&
				ccz > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMin &&
				ccz < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMax) return 1;
		}
	}

	v.y = h;

	float hh = GetLandH(v.x - 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x - 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;

	if (mc)
		for (int z = -2; z <= 2; z++)
			for (int x = -2; x <= 2; x++)
				if (OMap[ccz + z][ccx + x] != 255)
				{
					int ob = OMap[ccz + z][ccx + x];
					if (MObjects[ob].info.Radius < 10) continue;
					float CR = static_cast<float>(MObjects[ob].info.Radius) + 64;

					float oz = (ccz + z) * 256.f + 128.f;
					float ox = (ccx + x) * 256.f + 128.f;

					float r = static_cast<float>(sqrt((ox - v.x)*(ox - v.x) + (oz - v.z)*(oz - v.z)));
					if (r < CR) return 1;
				}

	return 0;
}
int CheckPlaceCollisionMicro(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc)
{
	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1018 || ccz>1018) return 1;

	if (wc)
		if ((FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
			FMap[ccz][ccx] |
			FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]) & fmWater)
			return 1;


	float h = GetLandH(v.x, v.z);
	if (!(FMap[ccz][ccx] & fmWater))
		if (fabs(h - v.y) > 64) return 1;

	if (spawnGroup[cptr->SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[cptr->SpawnGroupType].avoidRegionCh; ar++) {
			if (ccx > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMin &&
				ccx < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMax &&
				ccz > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMin &&
				ccz < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMax) return 1;
		}
	}

	v.y = h;

	float hh = GetLandH(v.x - 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x - 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;

	if (mc)
		for (int z = -2; z <= 2; z++)
			for (int x = -2; x <= 2; x++)
				if (OMap[ccz + z][ccx + x] != 255)
				{
					int ob = OMap[ccz + z][ccx + x];
					if (MObjects[ob].info.Radius < 10) continue;
					float CR = static_cast<float>(MObjects[ob].info.Radius) + 64;

					float oz = (ccz + z) * 256.f + 128.f;
					float ox = (ccx + x) * 256.f + 128.f;

					float r = static_cast<float>(sqrt((ox - v.x)*(ox - v.x) + (oz - v.z)*(oz - v.z)));
					if (r < CR && (!TreeTable[ob] || !cptr->gottaClimb)) return 1;
				}

	return 0;
}
int CheckPlaceCollisionLandBrahi(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc)
{
	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1018 || ccz>1018) return 1;

	/*if (wc)
		if ((FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
			FMap[ccz][ccx] |
			FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]) & fmWater)
			return 1;
	 */

	if (spawnGroup[cptr->SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[cptr->SpawnGroupType].avoidRegionCh; ar++) {
			if (ccx > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMin &&
				ccx < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMax &&
				ccz > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMin &&
				ccz < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMax) return 1;
		}
	}

	if (wc){
		if ((FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
			FMap[ccz][ccx] |
			FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]) & fmWater){ 
			return 1;
		}
	} else {
		if ((GetLandUpH(v.x, v.z) - GetLandH(v.x, v.z)) > DinoInfo[cptr->CType].waterLevel ||
			(GetLandUpH(v.x + 256, v.z) - GetLandH(v.x + 256, v.z)) > DinoInfo[cptr->CType].waterLevel ||
			(GetLandUpH(v.x, v.z + 256) - GetLandH(v.x, v.z + 256)) > DinoInfo[cptr->CType].waterLevel ||
			(GetLandUpH(v.x + 256, v.z + 256) - GetLandH(v.x + 256, v.z + 256)) > DinoInfo[cptr->CType].waterLevel ||
			(GetLandUpH(v.x - 256, v.z) - GetLandH(v.x - 256, v.z)) > DinoInfo[cptr->CType].waterLevel ||
			(GetLandUpH(v.x, v.z - 256) - GetLandH(v.x, v.z - 256)) > DinoInfo[cptr->CType].waterLevel ||
			(GetLandUpH(v.x - 256, v.z - 256) - GetLandH(v.x - 256, v.z - 256)) > DinoInfo[cptr->CType].waterLevel ||
			(GetLandUpH(v.x + 256, v.z - 256) - GetLandH(v.x + 256, v.z - 256)) > DinoInfo[cptr->CType].waterLevel ||
			(GetLandUpH(v.x - 256, v.z + 256) - GetLandH(v.x - 256, v.z + 256)) > DinoInfo[cptr->CType].waterLevel) return 1;
	}

	float h = GetLandH(v.x, v.z);
	if (fabs(h - v.y) > 64) return 1;

	v.y = h;

	float hh = GetLandH(v.x - 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x - 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;

	if (mc)
		for (int z = -2; z <= 2; z++)
			for (int x = -2; x <= 2; x++)
				if (OMap[ccz + z][ccx + x] != 255)
				{
					int ob = OMap[ccz + z][ccx + x];
					if (MObjects[ob].info.Radius < 10) continue;
					float CR = static_cast<float>(MObjects[ob].info.Radius) + 64;

					float oz = (ccz + z) * 256.f + 128.f;
					float ox = (ccx + x) * 256.f + 128.f;

					float r = static_cast<float>(sqrt((ox - v.x)*(ox - v.x) + (oz - v.z)*(oz - v.z)));
					if (r < CR) return 1;
				}

	return 0;
}
int CheckPlaceCollisionBrahi(TCharacter *cptr, Vector3d &v, std::int32_t wc, std::int32_t mc)
{
	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1018 || ccz>1018) return 1;

	/*if (wc)
		if ((FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
			FMap[ccz][ccx] |
			FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]) & fmWater)
			return 1;
	 */

	if (spawnGroup[cptr->SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[cptr->SpawnGroupType].avoidRegionCh; ar++) {
			if (ccx > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMin &&
				ccx < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMax &&
				ccz > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMin &&
				ccz < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMax) return 1;
		}
	}

	int limit = 550;//550
	int range = 256;//256bluz
	if (wc) {
		if ((GetLandUpH(v.x, v.z) - GetLandH(v.x, v.z)) > limit ||
			(GetLandUpH(v.x + range, v.z) - GetLandH(v.x + range, v.z)) > limit ||
			(GetLandUpH(v.x, v.z + range) - GetLandH(v.x, v.z + range)) > limit ||
			(GetLandUpH(v.x + range, v.z + range) - GetLandH(v.x + range, v.z + range)) > limit ||
			(GetLandUpH(v.x - range, v.z) - GetLandH(v.x - range, v.z)) > limit ||
			(GetLandUpH(v.x, v.z - range) - GetLandH(v.x, v.z - range)) > limit ||
			(GetLandUpH(v.x - range, v.z - range) - GetLandH(v.x - range, v.z - range)) > limit ||
			(GetLandUpH(v.x + range, v.z - range) - GetLandH(v.x + range, v.z - range)) > limit ||
			(GetLandUpH(v.x - range, v.z + range) - GetLandH(v.x - range, v.z + range)) > limit) return 1;
	}

	float h = GetLandH(v.x, v.z);
	if (fabs(h - v.y) > 64) return 1;

	v.y = h;

	float hh = GetLandH(v.x - 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x - 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;

	if (mc)
		for (int z = -2; z <= 2; z++)
			for (int x = -2; x <= 2; x++)
				if (OMap[ccz + z][ccx + x] != 255)
				{
					int ob = OMap[ccz + z][ccx + x];
					if (MObjects[ob].info.Radius < 10) continue;
					float CR = static_cast<float>(MObjects[ob].info.Radius) + 64;

					float oz = (ccz + z) * 256.f + 128.f;
					float ox = (ccx + x) * 256.f + 128.f;

					float r = static_cast<float>(sqrt((ox - v.x)*(ox - v.x) + (oz - v.z)*(oz - v.z)));
					if (r < CR) return 1;
				}

	return 0;
}
int CheckPlaceCollisionBrahiP(Vector3d &v)
{
	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1008 || ccz>1008) return 1;

	int F = (FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
		FMap[ccz][ccx] |
		FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]);

	if (!(GetLandUpH(v.x, v.z) > GetLandH(v.x, v.z))) return 1;

	int limit = 550;//550
	int range = 256;//256bluz
	if ((GetLandUpH(v.x, v.z) - GetLandH(v.x, v.z)) > limit ||
		(GetLandUpH(v.x + range, v.z) - GetLandH(v.x + range, v.z)) > limit ||
		(GetLandUpH(v.x, v.z + range) - GetLandH(v.x, v.z + range)) > limit ||
		(GetLandUpH(v.x + range, v.z + range) - GetLandH(v.x + range, v.z + range)) > limit ||
		(GetLandUpH(v.x - range, v.z) - GetLandH(v.x - range, v.z)) > limit ||
		(GetLandUpH(v.x, v.z - range) - GetLandH(v.x, v.z - range)) > limit ||
		(GetLandUpH(v.x - range, v.z - range) - GetLandH(v.x - range, v.z - range)) > limit ||
		(GetLandUpH(v.x + range, v.z - range) - GetLandH(v.x + range, v.z - range)) > limit ||
		(GetLandUpH(v.x - range, v.z + range) - GetLandH(v.x - range, v.z + range)) > limit) return 1;

	float h = GetLandH(v.x, v.z);
	v.y = h;

	float hh = GetLandH(v.x - 164, v.z - 164);
	if (fabs(hh - h) > 160) return 1;
	hh = GetLandH(v.x + 164, v.z - 164);
	if (fabs(hh - h) > 160) return 1;
	hh = GetLandH(v.x - 164, v.z + 164);
	if (fabs(hh - h) > 160) return 1;
	hh = GetLandH(v.x + 164, v.z + 164);
	if (fabs(hh - h) > 160) return 1;

	for (int z = -2; z <= 2; z++)
		for (int x = -2; x <= 2; x++)
			if (OMap[ccz + z][ccx + x] != 255)
			{
				int ob = OMap[ccz + z][ccx + x];
				if (MObjects[ob].info.Radius < 10) continue;
				float CR = static_cast<float>(MObjects[ob].info.Radius) + 64;

				float oz = (ccz + z) * 256.f + 128.f;
				float ox = (ccx + x) * 256.f + 128.f;

				float r = static_cast<float>(sqrt((ox - v.x)*(ox - v.x) + (oz - v.z)*(oz - v.z)));
				if (r < CR) return 1;
			}

	return 0;
}
int CheckPlaceCollision2(TCharacter *cptr, Vector3d &v, std::int32_t wc)
{
	int ccx = static_cast<int>(v.x) / 256;
	int ccz = static_cast<int>(v.z) / 256;

	if (ccx < 4 || ccz < 4 || ccx>1018 || ccz>1018) return 1;

	if (wc)
		if ((FMap[ccz][ccx - 1] | FMap[ccz - 1][ccx] | FMap[ccz - 1][ccx - 1] |
			FMap[ccz][ccx] |
			FMap[ccz + 1][ccx] | FMap[ccz][ccx + 1] | FMap[ccz + 1][ccx + 1]) & fmWater)
			return 1;

	if (spawnGroup[cptr->SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[cptr->SpawnGroupType].avoidRegionCh; ar++) {
			if (ccx > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMin &&
				ccx < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMax &&
				ccz > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMin &&
				ccz < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMax) return 1;
		}
	}

	float h = GetLandH(v.x, v.z);
	/*if (! (FMap[ccz][ccx] & fmWater) )
	  if (fabs(h - v.y) > 64) return 1;*/
	v.y = h;

	float hh = GetLandH(v.x - 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z - 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x - 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;
	hh = GetLandH(v.x + 64, v.z + 64);
	if (fabs(hh - h) > 100) return 1;

	return 0;
}
int CheckPossiblePath(TCharacter *cptr, std::int32_t wc, std::int32_t mc)
{
	Vector3d p = cptr->pos;
	float lookx = static_cast<float>(cos(cptr->tgalpha));
	float lookz = static_cast<float>(sin(cptr->tgalpha));
	int c = 0;
	for (int t = 0; t < 20; t++)
	{

		if (cptr->Clone == AI_BRACH) {
			p.x += lookx * 256.f;
			p.z += lookz * 256.f;
			if (CheckPlaceCollisionBrahi(cptr, p, wc, mc)) c++;
		}
		else if (cptr->Clone == AI_BRACHDANGER) {
			p.x += lookx * DinoInfo[cptr->CType].maxGrad;//128
			p.z += lookz * DinoInfo[cptr->CType].maxGrad;//128
			if (CheckPlaceCollisionBrahi(cptr, p, wc, mc)) c++;
		} if (cptr->Clone == AI_LANDBRACH) {
			p.x += lookx * DinoInfo[cptr->CType].maxGrad;//128
			p.z += lookz * DinoInfo[cptr->CType].maxGrad;//128
			if (CheckPlaceCollisionLandBrahi(cptr, p, wc, mc)) c++;
		}
		else if (cptr->Clone == AI_FISH ||
			cptr->Clone == AI_MOSA) {
			p.x += lookx * 64.f;
			p.z += lookz * 64.f;
			if (CheckPlaceCollisionFish(cptr, p, cptr->depth,
				DinoInfo[cptr->CType].maxDepth,
				DinoInfo[cptr->CType].minDepth)) c++;

		}
		else if (cptr->Clone == AI_MICRO) {
			p.x += lookx * 64.f;
			p.z += lookz * 64.f;
			if (CheckPlaceCollisionMicro(cptr, p, wc, mc)) c++;
		}
		else {
			p.x += lookx * 64.f;
			p.z += lookz * 64.f;
			if (CheckPlaceCollision(cptr, p, wc, mc)) c++;
		}
	}
	return c;
}
void LookForAWay(TCharacter *cptr, std::int32_t wc, std::int32_t mc)
{
	float alpha = cptr->tgalpha;
	float dalpha = 15.f;
	float afound = alpha;
	int maxp = 16;
	int curp;

	if (!CheckPossiblePath(cptr, wc, mc))
	{
		cptr->NoWayCnt = 0;
		return;
	}

	cptr->NoWayCnt++;
	for (int i = 0; i < 12; i++)
	{
		cptr->tgalpha = alpha + dalpha * pi / 180.f;
		curp = CheckPossiblePath(cptr, wc, mc) + (i >> 1);
		if (!curp) return;
		if (curp < maxp)
		{
			maxp = curp;
			afound = cptr->tgalpha;
		}

		cptr->tgalpha = alpha - dalpha * pi / 180.f;
		curp = CheckPossiblePath(cptr, wc, mc) + (i >> 1);
		if (!curp) return;
		if (curp < maxp)
		{
			maxp = curp;
			afound = cptr->tgalpha;
		}

		dalpha += 15.f;
	}

	cptr->tgalpha = afound;
}
