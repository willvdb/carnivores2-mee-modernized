#include "Hunt.h"
#include "Game/CharacterInternal.h"
#include "stdio.h"

#define fx_DIE    0

/*
#define RAP_RUN    0
#define RAP_WALK   1
#define RAP_SWIM   2
#define RAP_SLIDE  3
#define RAP_JUMP   4
#define RAP_DIE    5
#define RAP_EAT    6
#define RAP_SLP    7
#define RAP_IDLE1  8
#define RAP_IDLE2  9

#define SPN_RUN    0
#define SPN_WALK   1
#define SPN_SLIDE  2
#define SPN_SWIM   1
#define SPN_IDLE1  3
#define SPN_IDLE2  4
#define SPN_JUMP   5
#define SPN_DIE    6
#define SPN_EAT    7
#define SPN_SLP    8

#define VEL_RUN    0
#define VEL_WALK   1
#define VEL_SWIM   3
#define VEL_SLIDE  2
#define VEL_JUMP   4
#define VEL_DIE    5
#define VEL_EAT    6
#define VEL_SLP    7
#define VEL_IDLE1  8
#define VEL_IDLE2  9

#define CER_WALK   0
#define CER_RUN    1
#define CER_IDLE1  2
#define CER_IDLE2  3
#define CER_IDLE3  4
#define CER_DIE    5
#define CER_SLP    6
#define CER_EAT    7
#define CER_SWIM   0

*/

#define MOSA_RUN    0
#define MOSA_WALK   1
#define MOSA_JUMP   2
#define MOSA_DIE    3
#define MOSA_EAT    4
#define MOSA_SLP    5

/*
#define FISH_WALK   0
#define FISH_RUN    1

#define REX_RUN    0
#define REX_WALK   1
#define REX_SCREAM 2
#define REX_SWIM   3
#define REX_SEE    4
#define REX_SEE1   5
#define REX_SMEL   6
#define REX_SMEL1  7
#define REX_DIE    8
#define REX_EAT    9
#define REX_SLP    10


#define ICTH_WALK           0
#define ICTH_WALK_IDLE1     1
#define ICTH_WALK_IDLE2     2
#define ICTH_WINGDOWN_LAND  3
#define ICTH_SWIM           4
#define ICTH_SWIM_IDLE1     5
#define ICTH_SWIM_IDLE2     6
#define ICTH_WINGDOWN_WATER 7
#define ICTH_FLY            8
#define ICTH_FLY2           9
#define ICTH_TAKEOFF        10
#define ICTH_LANDING        11#define ICTH_WATER_DIE      14
#define ICTH_SLEEP          15


#define MOS_RUN    0
#define MOS_WALK   1
#define MOS_DIE    2
#define MOS_IDLE1  3
#define MOS_IDLE2  4
#define MOS_SLP    5


#define DMT_WALK   0
#define DMT_RUN    1
#define DMT_IDLE1  2
#define DMT_IDLE2  3
#define DMT_DIE    4
#define DMT_SLP    5


#define ANK_RUN    0
#define ANK_WALK   1
#define ANK_IDLE1  2
#define ANK_IDLE2  3
#define ANK_DIE    4
#define ANK_SLP    5


#define STG_RUN    0
#define STG_WALK   1
#define STG_DIE    2
#define STG_IDLE1  3
#define STG_IDLE2  4
#define STG_SLP    5

#define GAL_RUN    0
#define GAL_WALK   1
#define GAL_SLIDE  2
#define GAL_DIE    3
#define GAL_IDLE1  4
#define GAL_IDLE2  5
#define GAL_SLP    6


#define TRI_RUN    0
#define TRI_WALK   1
#define TRI_IDLE1  2
#define TRI_IDLE2  3
#define TRI_IDLE3  4
#define TRI_DIE    5
#define TRI_SLP    6


#define PAC_WALK   0
#define PAC_RUN    1
#define PAC_SLIDE  2
#define PAC_DIE    3
#define PAC_IDLE1  4
#define PAC_IDLE2  5
#define PAC_SLP    6

#define PAR_WALK   0
#define PAR_RUN    1
#define PAR_IDLE1  2
#define PAR_IDLE2  3
#define PAR_DIE    4
#define PAR_SLP    5

#define DIM_FLY    0
#define DIM_FLYP   1
#define DIM_FALL   2
#define DIM_DIE    3

#define BRA_WALK   0
#define BRA_IDLE1  1
#define BRA_IDLE2  2
#define BRA_IDLE3  3
#define BRA_DIE    4
#define BRA_SLP    5
#define BRA_RUN    6
#define BRA_EAT    10
*/



void SetNewTargetPlace(TCharacter *cptr, float R);
void SetNewTargetPlaceRegion(TCharacter *cptr, float R);
void SetNewTargetPlaceVanilla(TCharacter *cptr, float R);


void ProcessPrevPhase(TCharacter *cptr)
{
/*	char buff[100];
	sprintf(buff, "\n AI= %i", DinoInfo[cptr->CType].Clone);
	PrintLog(buff);

	char buff2[100];
	sprintf(buff2, " Ph= %i", cptr->Phase);
	PrintLog(buff2);*/

	cptr->PPMorphTime += TimeDt;
	if (cptr->PPMorphTime > PMORPHTIME) cptr->PrevPhase = cptr->Phase;

	cptr->PrevPFTime += TimeDt;
 	cptr->PrevPFTime %= cptr->pinfo->Animation[cptr->PrevPhase].AniTime;
	cptr->PrevPFTime %= cptr->pinfo->Animation[cptr->PrevPhase].AniTime;
}


void ActivateCharacterFxAquatic(TCharacter *cptr)
{
	if (cptr->CType) //== not hunter ==//
		if (!IsUnderwater()) return;
	int fx = cptr->pinfo->Anifx[cptr->Phase];
	if (fx == -1) return;

	if (VectorLengthSq(SubVectors(PlayerPos, cptr->pos)) > (68 * 256) * (68 * 256)) return;

	AddVoice3d(cptr->pinfo->SoundFX[fx].length,
		cptr->pinfo->SoundFX[fx].lpData.data(),
		cptr->pos.x, cptr->pos.y, cptr->pos.z);
}


void ActivateCharacterFx(TCharacter *cptr)
{
	
	//char buff[100];
	//sprintf(buff, "\n AI= %i", DinoInfo[cptr->CType].Clone);
	//PrintLog(buff);

	//char buff2[100];
	//sprintf(buff2, " Ph= %i", cptr->Phase);
	//PrintLog(buff2);

	if (cptr->CType) //== not hunter ==//
		if (IsUnderwater()) return;
	int fx = cptr->pinfo->Anifx[cptr->Phase];
	if (fx == -1) return;

	if (VectorLengthSq(SubVectors(PlayerPos, cptr->pos)) > (68 * 256) * (68 * 256)) return;

	AddVoice3d(cptr->pinfo->SoundFX[fx].length,
		cptr->pinfo->SoundFX[fx].lpData.data(),
		cptr->pos.x, cptr->pos.y, cptr->pos.z);
		
}


void ResetCharacter(TCharacter *cptr)
{
	//cptr->AI = DinoInfo[cptr->CType].AI;
	cptr->pinfo = &ChInfo[cptr->CType];
	cptr->Clone = DinoInfo[cptr->CType].Clone;
	cptr->State = 0;
	cptr->StateF = 0;
	cptr->Phase = 0;
	cptr->FTime = 0;
	cptr->PrevPhase = 0;
	cptr->PrevPFTime = 0;
	cptr->PPMorphTime = 0;
	cptr->beta = 0;
	cptr->gamma = 0;
	cptr->tggamma = 0;
	cptr->bend = 0;
	cptr->rspeed = 0;
	cptr->AfraidTime = 0;
	cptr->BloodTTime = 0;
	cptr->BloodTime = 0;

	cptr->claimed = false;

	cptr->tracker = -1;
	cptr->RTime = 0;

	if (cptr->Clone == AI_BRACH ||
		cptr->Clone == AI_BRACHDANGER ||
		cptr->Clone == AI_ICTH ||
		cptr->Clone == AI_FISH ||
		cptr->Clone == AI_MOSA) {
		cptr->cpcpAquatic = true;
	}
	else cptr->cpcpAquatic = false;

	cptr->currentIdleGroup = -1;
	cptr->currentIdle2Group = -1;

	cptr->awareHunter = false;
	cptr->heardShot = false;

	if (DinoInfo[cptr->CType].killTypeCount > 1) {
		cptr->killType = rRand(DinoInfo[cptr->CType].killTypeCount - 1);
	}

	if (DinoInfo[cptr->CType].roarCount > 0) {
		cptr->roarAnim = DinoInfo[cptr->CType].roarAnim[rRand(DinoInfo[cptr->CType].roarCount - 1)];
	}

	if (DinoInfo[cptr->CType].deathTypeCount > 1) {
		cptr->deathType = rRand(DinoInfo[cptr->CType].deathTypeCount - 1);
	}

	if (DinoInfo[cptr->CType].waterDieCount > 0) {
		cptr->waterDieAnim = DinoInfo[cptr->CType].waterDieAnim[rRand(DinoInfo[cptr->CType].waterDieCount - 1)];
	}

	cptr->lastTBeta = 0;
	cptr->turny = 0;
	cptr->bdepth = static_cast<float>(0);

	cptr->lookx = static_cast<float>(cos(cptr->alpha));
	cptr->lookz = static_cast<float>(sin(cptr->alpha));

	cptr->Health = DinoInfo[cptr->CType].Health0;
	if (OptAgres > 128) cptr->Health = (cptr->Health*OptAgres) / 128;

	cptr->scale = static_cast<float>((DinoInfo[cptr->CType].Scale0 + rRand(DinoInfo[cptr->CType].ScaleA))) / 1000.f;

	//When does need to get set? not here huh?
	//cptr->RType = spawnGroup[cptr->SpawnGroupType].spawnRegionCh;

	cptr->followLeader = false;

	cptr->aquaticIdle = false;

	cptr->spcDepth = DinoInfo[cptr->CType].spacingDepth + (cptr->scale * 500) - 500;

	cptr->showSonar = false;

	//poacher
	cptr->ammo = DinoInfo[cptr->CType].Reload;

}


void AddDeadBody(TCharacter *cptr, int phase, bool scream)
{
	if (!MyHealth) return;

	if (ExitTime)
		AddMessage("Transportation cancelled.");
	ExitTime = 0;

	g_GameMode = GameMode::Normal;
	g_GameMode = GameMode::Normal;
	Characters[ChCount].CType = 0;
	Characters[ChCount].alpha = CameraAlpha;
	ResetCharacter(&Characters[ChCount]);

	int v = rRand(3);
	if (phase != HUNT_BREATH && scream){
		AddVoicev(fxScream[r].length, fxScream[r].lpData.data(), 256);
	}

	Characters[ChCount].Health = 0;
	MyHealth = 0;
	if (cptr)
	{
		killerDino = cptr;

		if (GetLandUpH(killerDino->pos.x, killerDino->pos.z) -
			GetLandH(killerDino->pos.x, killerDino->pos.z) >
			DinoInfo[killerDino->CType].waterLevel * killerDino->scale) {
			killedwater = true;
		}
		else {
			killedwater = false;
		}

		float pl = DinoInfo[cptr->CType].killType[cptr->killType].offset;
		Characters[ChCount].pos.x = cptr->pos.x + cptr->lookx * pl * cptr->scale;
		Characters[ChCount].pos.z = cptr->pos.z + cptr->lookz * pl * cptr->scale;
		Characters[ChCount].pos.y = GetLandQH(Characters[ChCount].pos.x, Characters[ChCount].pos.z);
		/*
		if (DinoInfo[cptr->CType].Aquatic) {
			Characters[ChCount].pos.x = cptr->pos.x + cptr->lookx * pl * cptr->scale * static_cast<float>(cos(cptr->beta));
			Characters[ChCount].pos.z = cptr->pos.z + cptr->lookz * pl * cptr->scale * static_cast<float>(cos(cptr->beta));
			Characters[ChCount].pos.y = cptr->pos.y - static_cast<float>(sin(cptr->beta)) * pl * cptr->scale;
			float ply = DinoInfo[cptr->CType].killType[cptr->killType].yoffset;
			Characters[ChCount].pos.y += ply * static_cast<float>(cos(cptr->beta));
			ply *= static_cast<float>(sin(cptr->beta));
			Characters[ChCount].pos.z += ply * static_cast<float>(sin(cptr->alpha));
			Characters[ChCount].pos.x += ply * static_cast<float>(cos(cptr->alpha));
			Characters[ChCount].alpha = cptr->alpha;
			Characters[ChCount].beta = cptr->beta;
			Characters[ChCount].gamma = cptr->gamma;

		}
		*/
	}
	else
	{
		Characters[ChCount].pos.x = PlayerX;
		Characters[ChCount].pos.z = PlayerZ;
		Characters[ChCount].pos.y = PlayerY;
	}

	Characters[ChCount].Phase = phase;
	Characters[ChCount].PrevPhase = phase;

	ActivateCharacterFx(&Characters[ChCount]);


	DemoPoint.pos = Characters[ChCount].pos;
	DemoPoint.DemoTime = 1;
	DemoPoint.CIndex = ChCount;

	/*
	//if (phase > 0) {
	if (DinoInfo[cptr->CType].killType[cptr->killType].elevate) {
		Characters[ChCount].scale = cptr->scale;
		Characters[ChCount].alpha = cptr->alpha;
		cptr->bend = 0;
		DemoPoint.CIndex = CurDino;
	}
	*/

	ChCount++;
}



float AngleDifference(float a, float b)
{
	a -= b;
	a = static_cast<float>(fabs(a));
	if (a > pi) a = 2 * pi - a;
	return a;
}

float CorrectedAlpha(float a, float b)
{
	float d = static_cast<float>(fabs(a - b));
	if (d < pi) return (a + b) / 2;
	else d = (a + pi * 2 - b);

	if (d < 0) d += 2 * pi;
	if (d > 2 * pi) d -= 2 * pi;
	return d;
}

void ThinkY_Beta_Gamma(TCharacter *cptr, float blook, float glook, float blim, float glim)
{
	cptr->pos.y = GetLandH(cptr->pos.x, cptr->pos.z);

	//=== beta ===//
	float hlook = GetLandH(cptr->pos.x + cptr->lookx * blook, cptr->pos.z + cptr->lookz * blook);
	float hlook2 = GetLandH(cptr->pos.x - cptr->lookx * blook, cptr->pos.z - cptr->lookz * blook);
	DeltaFunc(cptr->beta, (hlook2 - hlook) / (blook * 3.2f), TimeDt / 800.f);

	if (cptr->beta > blim) cptr->beta = blim;
	if (cptr->beta < -blim) cptr->beta = -blim;

	//=== gamma ===//
	hlook = GetLandH(cptr->pos.x + cptr->lookz * glook, cptr->pos.z - cptr->lookx*glook);
	hlook2 = GetLandH(cptr->pos.x - cptr->lookz * glook, cptr->pos.z + cptr->lookx*glook);
	cptr->tggamma = (hlook - hlook2) / (glook * 3.2f);
	if (cptr->tggamma > glim) cptr->tggamma = glim;
	if (cptr->tggamma < -glim) cptr->tggamma = -glim;
	/*
	  if (DEBUG) cptr->tggamma = 0;
	  if (DEBUG) cptr->beta    = 0;
	  */
}










//OLD

/*
*/
























//OLD


std::int32_t ReplaceCharacterForward(TCharacter *cptr)
{

	if (!spawnGroup[cptr->SpawnGroupType].moveForward) return false;

	float al = CameraAlpha + static_cast<float>(siRand(2048)) / 2048.f;
	float sa = static_cast<float>(sin(al));
	float ca = static_cast<float>(cos(al));
	Vector3d p;
	p.x = PlayerX + sa * (charViewR + rRand(10)) * 256;
	p.z = PlayerZ - ca * (charViewR + rRand(10)) * 256;
	p.y = GetLandH(p.x, p.z);

	if (p.x < 16 * 256) return false;
	if (p.z < 16 * 256) return false;
	if (p.x > 1000 * 256) return false;
	if (p.z > 1000 * 256) return false;

	std::int32_t outside = true;
	for (int sr = 0; sr < spawnGroup[cptr->SpawnGroupType].spawnRegionCh; sr++) {
		if (p.x > spawnGroup[cptr->SpawnGroupType].spawnRegion[sr].XMin * 256 &&
			p.x < spawnGroup[cptr->SpawnGroupType].spawnRegion[sr].XMax * 256 &&
			p.z > spawnGroup[cptr->SpawnGroupType].spawnRegion[sr].YMin * 256 &&
			p.z < spawnGroup[cptr->SpawnGroupType].spawnRegion[sr].YMax * 256) outside = false;
	}
	if (outside) return false;
	if (spawnGroup[cptr->SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[cptr->SpawnGroupType].avoidRegionCh; ar++) {
			if (p.x > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMin * 256 &&
				p.x < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].XMax * 256 &&
				p.z > spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMin * 256 &&
				p.z < spawnGroup[cptr->SpawnGroupType].avoidRegion[ar].YMax * 256) return false;
		}
	}

	if (cptr->Clone == AI_BRACH || cptr->Clone == AI_BRACHDANGER || cptr->Clone == AI_ICTH) {
		if (CheckPlaceCollisionBrahiP(p)) return false;
	} else if (cptr->Clone == AI_FISH || cptr->Clone == AI_MOSA) {
			if (CheckPlaceCollisionFishP(p,
				DinoInfo[cptr->CType].minDepth,
				DinoInfo[cptr->CType].maxDepth)) return false;
	} else if (CheckPlaceCollisionP(p, cptr->cpcpAquatic)) return false;

//	cptr->State = 0;
	cptr->pos = p;
	ResetCharacter(cptr);
	//cptr->tgx = cptr->pos.x + siRand(2048);
	//cptr->tgz = cptr->pos.z + siRand(2048);
	if (cptr->Clone == AI_BRACH || cptr->Clone == AI_BRACHDANGER || cptr->Clone == AI_LANDBRACH) SetNewTargetPlace_Brahi(cptr, 2048.f);
	else if (cptr->Clone == AI_MOSA) SetNewTargetPlaceFish(cptr, 5048.f);
	else if (cptr->Clone == AI_FISH) SetNewTargetPlaceFish(cptr, 1024.f);
	else SetNewTargetPlace(cptr, 2048);

	if (cptr->Clone == AI_FISH || cptr->Clone == AI_MOSA) {
		cptr->pos.y = GetLandUpH(cptr->pos.x, cptr->pos.z) - ((GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z)) / 2);
	}

	if (cptr->Clone == AI_DIMOR || cptr->Clone == AI_PTERA) //===== dimor ========//
		cptr->pos.y += DinoInfo[cptr->CType].minDepth;
	return true;
}








//old























