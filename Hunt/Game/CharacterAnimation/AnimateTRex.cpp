// AnimateTRex.cpp � auto-extracted from CharacterAnimation.cpp
// ==========================================================================
// Auto-generated from CharacterAnimation.cpp
// ==========================================================================

#include "Hunt.h"
#include "../CharacterInternal.h"

// Global state imported from StateDefs.cpp
extern int CurDino;
extern int NewPhase;

void AnimateTRex(TCharacter *cptr)
{
	NewPhase = false;
	int _Phase = cptr->Phase;
	int _FTime = cptr->FTime;
	float _tgalpha = cptr->tgalpha;
	std::int32_t LookMode = false;



TBEGIN:
	float targetx = cptr->tgx;
	float targetz = cptr->tgz;
	float targetdx = targetx - cptr->pos.x;
	float targetdz = targetz - cptr->pos.z;

	float tdistSq = targetdx * targetdx + targetdz * targetdz;

	float playerdx = PlayerX - cptr->pos.x - cptr->lookx * 108;
	float playerdz = PlayerZ - cptr->pos.z - cptr->lookz * 108;
	float pdistSq = playerdx * playerdx + playerdz * playerdz;
	float palpha = FindVectorAlpha(playerdx, playerdz);
	//if (cptr->State==2) { NewPhase=true; cptr->State=1; }


	bool alertInit = false;
	if (cptr->State == 5) alertInit = true;
	if (cptr->packId >= 0) {
		if (!cptr->State && Packs[cptr->packId]._alert) alertInit = true;
	}

	if (alertInit)
	{
		NewPhase = true;
		cptr->State = 1;
		cptr->Phase = DinoInfo[cptr->CType].walkAnim;
		cptr->FTime = 0;
		cptr->tgx = PlayerX;
		cptr->tgz = PlayerZ;
		goto TBEGIN;
	}

	if (cptr->State) Packs[cptr->packId].alert = true;



	if (GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z) > DinoInfo[cptr->CType].waterLevel * cptr->scale)
		cptr->StateF |= csONWATER;
	else
		cptr->StateF &= (!csONWATER);

	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) goto NOTHINK;

	//============================================//
	//if (!MyHealth) cptr->State = 0; //TREX cannot return to state 0!!!

	if (cptr->State)
	{

		cptr->currentIdleGroup = -1;

		cptr->tgx = PlayerX;
		cptr->tgz = PlayerZ;
		cptr->tgtime = 0;
		if (cptr->State > 1)
			if (AngleDifference(cptr->alpha, palpha) < 0.4f)
			{
				if (cptr->State == 2) {
					if (DinoInfo[cptr->CType].lookCount) {
						cptr->Phase = DinoInfo[cptr->CType].lookAnim[rRand(DinoInfo[cptr->CType].lookCount - 1)];
						cptr->rspeed = 0;
					}
					else if (DinoInfo[cptr->CType].roarCount) {
						cptr->Phase = cptr->roarAnim;
						cptr->rspeed = 0;
					} else {
						cptr->Phase = DinoInfo[cptr->CType].runAnim;
					}
					
				}
				else {
					if (DinoInfo[cptr->CType].smellCount) {
						cptr->Phase = DinoInfo[cptr->CType].smellAnim[rRand(DinoInfo[cptr->CType].smellCount - 1)];
						cptr->rspeed = 0;
					}
					else if (DinoInfo[cptr->CType].roarCount) {
						cptr->Phase = cptr->roarAnim;
						cptr->rspeed = 0;
					} else {
						cptr->Phase = DinoInfo[cptr->CType].runAnim;
					}

				}
				cptr->State = 1;
			}




		if (pdistSq < DinoInfo[cptr->CType].killDist * DinoInfo[cptr->CType].killDist && DinoInfo[cptr->CType].killDist > 0 && MyHealth)
		{
			int killAlt = DinoInfo[cptr->CType].waterLevel;
			if (killAlt < 256) killAlt = 256;
			if (fabs(PlayerY - cptr->pos.y) < killAlt + 20)
			{
				if (DinoInfo[cptr->CType].killTypeCount > 0) {

					if (!(cptr->StateF & csONWATER))
					{
						cptr->vspeed /= 8.0f;
						cptr->State = 1;
						cptr->Phase = DinoInfo[cptr->CType].killType[cptr->killType].anim;
						if (DinoInfo[cptr->CType].killType[cptr->killType].dontloop) cptr->FTime = 0;
						AddDeadBody(cptr,
							DinoInfo[cptr->CType].killType[cptr->killType].hunteranim,
							DinoInfo[cptr->CType].killType[cptr->killType].scream);
					}
					else AddDeadBody(cptr, HUNT_EAT, true);

				}
				else {
					AddDeadBody(cptr, HUNT_EAT, true);
					cptr->State = 0;
				}

			}
		}



	}

	// Step 4: Extend culling distance by 4 units (~1024 world units)
	// to allow smoothstep fade-out to complete
	if (pdistSq > ((charViewR + 20 + 4) * 256) * ((charViewR + 20 + 4) * 256))
		if (ReplaceCharacterForward(cptr)) goto TBEGIN;

	if (!cptr->State) {


		if (cptr->packId >= 0) {
			float leaderdx = Packs[cptr->packId].leader->pos.x - cptr->pos.x;
			float leaderdz = Packs[cptr->packId].leader->pos.z - cptr->pos.z;
			float leaderdistSq = leaderdx * leaderdx + leaderdz * leaderdz;

			if (cptr->followLeader) {
				if (leaderdistSq < (cptr->packDensity * 128 * 0.6) * (cptr->packDensity * 128 * 0.6))
				{
					cptr->followLeader = false;
					SetNewTargetPlace(cptr, 8048.f);
					goto TBEGIN;
				}
			}
			else {
				if (leaderdistSq > (cptr->packDensity * 128 * 1.3) * (cptr->packDensity * 128 * 1.3))
				{
					cptr->followLeader = true;
				}
			}

		}

		if (cptr->followLeader) {
			cptr->tgx = Packs[cptr->packId].leader->pos.x;
			cptr->tgz = Packs[cptr->packId].leader->pos.z;
		}
		else if (tdistSq < 1224 * 1224)
		{
			SetNewTargetPlace(cptr, 8048.f);
			goto TBEGIN;
		}
	}


NOTHINK:
	if (pdistSq < 2048 * 2048) cptr->NoFindCnt = 0;
	if (cptr->NoFindCnt) cptr->NoFindCnt--;
	else
	{
		cptr->tgalpha = CorrectedAlpha(FindVectorAlpha(targetdx, targetdz), cptr->alpha);//FindVectorAlpha(targetdx, targetdz);

		if (cptr->State && pdistSq > DinoInfo[cptr->CType].weaveRange * DinoInfo[cptr->CType].weaveRange && !DinoInfo[cptr->CType].dontWeave)
		{
			cptr->tgalpha += static_cast<float>(sin(RealTime / 824.f)) / 6.f;
			if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
			if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;
		}
	}

	LookForAWay(cptr, !DinoInfo[cptr->CType].canSwim, !cptr->State || DinoInfo[cptr->CType].TRexObjCollide);
	//LookForAWay(cptr, !DinoInfo[cptr->CType].canSwim, true);
	
	if (cptr->NoWayCnt > 12)
	{
		cptr->NoWayCnt = 0;
		cptr->NoFindCnt = 16 + rRand(20);
	}


	if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
	if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;

	//===============================================//

	ProcessPrevPhase(cptr);


	//======== select new phase =======================//

	
	for (int i = 0; i < DinoInfo[cptr->CType].lookCount; i++) {
		if (cptr->Phase == DinoInfo[cptr->CType].lookAnim[i]) LookMode = true;
	}

	for (int i = 0; i < DinoInfo[cptr->CType].smellCount; i++) {
		if (cptr->Phase == DinoInfo[cptr->CType].smellAnim[i]) LookMode = true;
	}
	

	cptr->FTime += TimeDt;

	if (cptr->FTime >= cptr->pinfo->Animation[cptr->Phase].AniTime)
	{
		cptr->FTime %= cptr->pinfo->Animation[cptr->Phase].AniTime;

		if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) {
			if (DinoInfo[cptr->CType].killType[cptr->killType].dontloop) {
				cptr->Phase = DinoInfo[cptr->CType].walkAnim;
				cptr->State = 0;
			}
		}


		NewPhase = true;
	}

	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount)    goto ENDPSELECT;

	if (!NewPhase)
		if (DinoInfo[cptr->CType].roarCount > 0 && cptr->Phase == cptr->roarAnim) goto ENDPSELECT;
		
	if (!cptr->State)
		if (NewPhase)
			

			if (DinoInfo[cptr->CType].idleGroupCount
				&& (MyHealth || !DinoInfo[cptr->CType].killType[cptr->killType].carryCorpse)
				&& !(cptr->StateF & csONWATER)) {

				if (cptr->currentIdleGroup >= 0) {
					if (rRand(127) + 1 > (1 - DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].end) * 128
						&& (DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].endOnAny
							|| cptr->Phase == DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].anim[DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].count - 1])) {
						cptr->Phase = DinoInfo[cptr->CType].walkAnim;
						if (DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].instantRepeat) {
							cptr->currentIdleGroup = -1; //this must be done inside the if statement
						}
						else {
							cptr->currentIdleGroup = -1; //this must be done inside the if statement
							goto ENDPSELECT;
						}
					}
					else {
						cptr->Phase = DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].anim[rRand(DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].count - 1)];
						goto ENDPSELECT;
					}
				}

				for (int idleGroupNo = 0; idleGroupNo < DinoInfo[cptr->CType].idleGroupCount; idleGroupNo++) {
					if (rRand(127) + 1 > (1 - DinoInfo[cptr->CType].idleGroup[idleGroupNo].start) * 128) cptr->currentIdleGroup = idleGroupNo;
				}
				if (cptr->currentIdleGroup >= 0) {
					if (DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].startOnAny)
						cptr->Phase = DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].anim[rRand(DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].count - 1)];
					else
						cptr->Phase = DinoInfo[cptr->CType].idleGroup[cptr->currentIdleGroup].anim[0];
					goto ENDPSELECT;
				}
				else cptr->Phase = DinoInfo[cptr->CType].walkAnim;
			}
			else {
				cptr->Phase = DinoInfo[cptr->CType].walkAnim;
			}


	if (!NewPhase) {
		if (LookMode) goto ENDPSELECT;
		if (cptr->currentIdleGroup >= 0) goto ENDPSELECT;
	}

	if (cptr->State)
		if (NewPhase && LookMode)
		{
			if (DinoInfo[cptr->CType].roarCount > 0) {
				cptr->Phase = cptr->roarAnim;
				goto ENDPSELECT;
			}// else cptr->Phase = DinoInfo[cptr->CType].runAnim;
		}

	if (!cptr->State || cptr->State > 1) cptr->Phase = DinoInfo[cptr->CType].walkAnim;
	else if (fabs(cptr->tgalpha - cptr->alpha) < 1.0 ||
		fabs(cptr->tgalpha - cptr->alpha) > 2 * pi - 1.0)
		cptr->Phase = DinoInfo[cptr->CType].runAnim;
	else cptr->Phase = DinoInfo[cptr->CType].walkAnim;

	if (DinoInfo[cptr->CType].canSwim) {
		if (cptr->StateF & csONWATER) cptr->Phase = DinoInfo[cptr->CType].swimAnim;
	}

ENDPSELECT:

	//====== process phase changing ===========//
	if ((_Phase != cptr->Phase) || NewPhase)
		ActivateCharacterFx(cptr);

	if (_Phase != cptr->Phase)
	{
		//==== set proportional FTime for better morphing =//

		if (MORPHP) {
			if ((_Phase == DinoInfo[cptr->CType].runAnim ||
				_Phase == DinoInfo[cptr->CType].walkAnim) &&
				(cptr->Phase == DinoInfo[cptr->CType].runAnim ||
					cptr->Phase == DinoInfo[cptr->CType].walkAnim))
				cptr->FTime = _FTime * cptr->pinfo->Animation[cptr->Phase].AniTime / cptr->pinfo->Animation[_Phase].AniTime + 64;
			else if (!NewPhase) cptr->FTime = 0;
		}

		if (cptr->PPMorphTime > 128)
		{
			cptr->PrevPhase = _Phase;
			cptr->PrevPFTime = _FTime;
			cptr->PPMorphTime = 0;
		}
	}

	cptr->FTime %= cptr->pinfo->Animation[cptr->Phase].AniTime;



	//========== rotation to tgalpha ===================//

	float rspd, currspeed, tgbend;
	float dalpha = static_cast<float>(fabs(cptr->tgalpha - cptr->alpha));
	float drspd = dalpha;
	if (drspd > pi) drspd = 2 * pi - drspd;

	if (DinoInfo[cptr->CType].roarCount > 0 && cptr->Phase == cptr->roarAnim) goto SKIPROT;
	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) goto SKIPROT;
	if (LookMode) goto SKIPROT;
	if (cptr->currentIdleGroup >= 0) goto SKIPROT;

	if (drspd > 0.02)
		if (cptr->tgalpha > cptr->alpha) currspeed = 0.7f + drspd * 1.4f;
		else currspeed = -0.7f - drspd * 1.4f;
	else currspeed = 0;
	if (cptr->AfraidTime) currspeed *= 2.5;

	if (dalpha > pi) currspeed *= -1;

	if (cptr->State) DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 440.f);
	else DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 620.f);

	tgbend = drspd / 2;
	if (tgbend > pi / 6.f) tgbend = pi / 6.f;

	tgbend *= SGN(currspeed);
	DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 1800.f);




	rspd = cptr->rspeed * TimeDt / 1024.f;
	if (drspd < fabs(rspd)) cptr->alpha = cptr->tgalpha;
	else cptr->alpha += rspd;


	if (cptr->alpha > pi * 2) cptr->alpha -= pi * 2;
	if (cptr->alpha < 0) cptr->alpha += pi * 2;

SKIPROT:

	//========== movement ==============================//
	cptr->lookx = static_cast<float>(cos(cptr->alpha));
	cptr->lookz = static_cast<float>(sin(cptr->alpha));

	float curspeed = 0;
	if (cptr->Phase == DinoInfo[cptr->CType].runAnim) curspeed = DinoInfo[cptr->CType].runspd;
	if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) curspeed = DinoInfo[cptr->CType].wlkspd;
	if (DinoInfo[cptr->CType].canSwim) {
		if (cptr->Phase == DinoInfo[cptr->CType].swimAnim) curspeed = DinoInfo[cptr->CType].swmspd;
	}

	if (drspd > pi / 2.f) curspeed *= 2.f - 2.f*drspd / pi;

	//========== process speed =============//

	DeltaFunc(cptr->vspeed, curspeed, TimeDt / 200.f);

	MoveCharacter(cptr, cptr->lookx * cptr->vspeed * TimeDt * cptr->scale,
		cptr->lookz * cptr->vspeed * TimeDt * cptr->scale, !DinoInfo[cptr->CType].canSwim, true);

	//============ Y movement =================//
	if ((cptr->StateF & csONWATER) && DinoInfo[cptr->CType].canSwim)
	{
		cptr->pos.y = GetLandUpH(cptr->pos.x, cptr->pos.z) - (DinoInfo[cptr->CType].waterLevel - 20) * cptr->scale;
		cptr->beta /= 2;
		cptr->tggamma = 0;
	}
	else
	{
		ThinkY_Beta_Gamma(cptr, 348, 324, 0.5f, 0.4f);
	}



	//=== process to tggamma ===//
	if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) cptr->tggamma += cptr->rspeed / 16.0f;
	else cptr->tggamma += cptr->rspeed / 12.0f;

	DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 2024.f);


	//==================================================//

}
