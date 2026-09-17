// AnimateIcthDead.cpp — auto-extracted from CharacterAnimation.cpp
// ==========================================================================
// Auto-generated from CharacterAnimation.cpp
// ==========================================================================

#include "Hunt.h"
#include "../CharacterInternal.h"

// Global state imported from StateDefs.cpp
extern int CurDino;
extern int NewPhase;

void AnimateIcthDead(TCharacter *cptr)
{
	cptr->bend = 0;

	if (cptr->Phase != DinoInfo[cptr->CType].deathType[cptr->deathType].fall && cptr->Phase != DinoInfo[cptr->CType].deathType[cptr->deathType].die
		&& !(cptr->Phase == DinoInfo[cptr->CType].deathType[cptr->deathType].sleep && cptr->Clone == AI_ICTH)
		&& !(cptr->Phase == cptr->waterDieAnim && DinoInfo[cptr->CType].waterDieCount))
	{
		cptr->deathPhase = cptr->Phase;
		if (cptr->PPMorphTime > 128)
		{
			cptr->PrevPhase = cptr->Phase;
			cptr->PrevPFTime = cptr->FTime;
			cptr->PPMorphTime = 0;
		}

		cptr->FTime = 0;
		cptr->Phase = DinoInfo[cptr->CType].deathType[cptr->deathType].fall;
		cptr->rspeed = 0;
		ActivateCharacterFx(cptr);
		return;
	}

	ProcessPrevPhase(cptr);

	float wh = GetLandUpH(cptr->pos.x, cptr->pos.z);
	float lh = GetLandH(cptr->pos.x, cptr->pos.z);
	std::int32_t OnWaterQ = (wh > lh);
	if (!DinoInfo[cptr->CType].waterDieCount) OnWaterQ = false;

	cptr->FTime += TimeDt;
	if (cptr->FTime >= cptr->pinfo->Animation[cptr->Phase].AniTime)
	{
		if (cptr->Phase == DinoInfo[cptr->CType].deathType[cptr->deathType].die ||
			(cptr->Phase == cptr->waterDieAnim && DinoInfo[cptr->CType].waterDieCount) ||
			(cptr->Phase == DinoInfo[cptr->CType].deathType[cptr->deathType].sleep && cptr->Clone == AI_ICTH))
		{
			if (cptr->canSleep)
			{
				cptr->FTime = 0;
				cptr->Phase = DinoInfo[cptr->CType].deathType[cptr->deathType].sleep;
				ActivateCharacterFx(cptr);
			}
			else
			{
				cptr->FTime = cptr->pinfo->Animation[cptr->Phase].AniTime - 1;
			}
		}
		else
			cptr->FTime %= cptr->pinfo->Animation[cptr->Phase].AniTime;


	}


	//======= movement ===========//
	if (cptr->Phase == DinoInfo[cptr->CType].deathType[cptr->deathType].die || 
		(cptr->Phase == cptr->waterDieAnim && DinoInfo[cptr->CType].waterDieCount) || 
		(cptr->Phase == DinoInfo[cptr->CType].deathType[cptr->deathType].sleep && cptr->Clone == AI_ICTH))
		DeltaFunc(cptr->vspeed, 0, TimeDt / 400.f);
	else
		DeltaFunc(cptr->vspeed, 0, TimeDt / 1200.f);

	cptr->pos.x += cptr->lookx * cptr->vspeed * TimeDt;
	cptr->pos.z += cptr->lookz * cptr->vspeed * TimeDt;

	if (cptr->Phase == DinoInfo[cptr->CType].deathType[cptr->deathType].fall)
	{
		if (OnWaterQ)
			if (cptr->pos.y >= wh && (cptr->pos.y + cptr->rspeed * TimeDt / 1024) < wh)
			{
				AddWCircle(cptr->pos.x + siRand(128), cptr->pos.z + siRand(128), 2.0);
				AddWCircle(cptr->pos.x + siRand(128), cptr->pos.z + siRand(128), 2.5);
				AddWCircle(cptr->pos.x + siRand(128), cptr->pos.z + siRand(128), 3.0);
				AddWCircle(cptr->pos.x + siRand(128), cptr->pos.z + siRand(128), 3.5);
				AddWCircle(cptr->pos.x + siRand(128), cptr->pos.z + siRand(128), 3.0);
			}
		cptr->pos.y += cptr->rspeed * TimeDt / 1024;
		cptr->rspeed -= TimeDt * 2.56;

		if (cptr->pos.y <= wh)
		{
			cptr->pos.y = wh;

			if (cptr->PPMorphTime > 128)
			{
				cptr->PrevPhase = cptr->Phase;
				cptr->PrevPFTime = cptr->FTime;
				cptr->PPMorphTime = 0;
			}

			if (OnWaterQ)
			{
				//				AddElements(cptr->pos.x + siRand(128), lh, cptr->pos.z + siRand(128), 4, 10);
				//				AddElements(cptr->pos.x + siRand(128), lh, cptr->pos.z + siRand(128), 4, 10);
				//				AddElements(cptr->pos.x + siRand(128), lh, cptr->pos.z + siRand(128), 4, 10);
				cptr->Phase = cptr->waterDieAnim;
			}
			else
			{
				cptr->Phase = DinoInfo[cptr->CType].deathType[cptr->deathType].die;
			}
			cptr->FTime = 0;
			ActivateCharacterFx(cptr);
		}

		cptr->canSleep = (Tranq && !OnWaterQ && cptr->Clone == AI_ICTH &&
			(cptr->deathPhase == DinoInfo[cptr->CType].walkAnim || cptr->deathPhase == DinoInfo[cptr->CType].shakeLandAnim || cptr->currentIdleGroup >= 0));

	}
	else
	{
		ThinkY_Beta_Gamma(cptr, 140, 126, 0.6f, 0.5f);
		DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 1600.f);
	}

	if (DinoInfo[cptr->CType].waterDieCount && cptr->Phase == cptr->waterDieAnim)
	{
		cptr->pos.y = wh;
		cptr->gamma = 0;
		cptr->beta = 0;
		cptr->alpha = 0;
	}
}
