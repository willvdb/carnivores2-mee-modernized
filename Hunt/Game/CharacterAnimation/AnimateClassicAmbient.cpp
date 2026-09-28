// AnimateClassicAmbient.cpp � auto-extracted from CharacterAnimation.cpp
// ==========================================================================
// Auto-generated from CharacterAnimation.cpp
// ==========================================================================

#include "Hunt.h"
#include "../CharacterInternal.h"

// Global state imported from StateDefs.cpp
extern int CurDino;
extern int NewPhase;

void AnimateClassicAmbient(TCharacter *cptr)
{
	NewPhase = false;
	int _Phase = cptr->Phase;
	int _FTime = cptr->FTime;
	float _tgalpha = cptr->tgalpha;

	bool alertInit = false;
	if (cptr->State == 2) alertInit = true;
	if (cptr->packId >= 0) {
		if (!cptr->State && Packs[cptr->packId]._alert) alertInit = true;
	}

	if (alertInit) {
		NewPhase = true;
		cptr->State = 1;
	}

TBEGIN:
	float targetx = cptr->tgx;
	float targetz = cptr->tgz;
	float targetdx = targetx - cptr->pos.x;
	float targetdz = targetz - cptr->pos.z;

	float tdistSq = targetdx * targetdx + targetdz * targetdz;

	bool pdistMulti = false;
	int pCh = 1;
	float pdistSq[4];
	float playerdx[4];
	float playerdz[4];
	playerdx[0] = PlayerX - cptr->pos.x;
	playerdz[0] = PlayerZ - cptr->pos.z;
	pdistSq[0] = playerdx[0] * playerdx[0] + playerdz[0] * playerdz[0];
	if (Multiplayer) {
		//for loop 1 to hunter count
		playerdx[pCh] = MPlayers[pCh].pos.x - cptr->pos.x;
		playerdz[pCh] = MPlayers[pCh].pos.z - cptr->pos.z;
		pdistSq[pCh] = playerdx[pCh] * playerdx[pCh] + playerdz[pCh] * playerdz[pCh];
		pCh += 1;
		//
	}

	if (GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z) > DinoInfo[cptr->CType].waterLevel * cptr->scale)
		cptr->StateF |= csONWATER;
	else
		cptr->StateF &= (!csONWATER);


	//=========== run away =================//

	if (cptr->State)
	{
		cptr->currentIdleGroup = -1;

		if (!cptr->AfraidTime)
		{
			pdistMulti = false;
			for (int pNo = 0; pNo < pCh; pNo++) {
				if (pdistSq[pNo] < 2048.f * 2048.f) pdistMulti = true;
			}
			if (pdistMulti) {
				if (pdistSq[0] < 2048.f * 2048.f) {
					cptr->hunterAwareness = HunterAwarenessState::TrackingHunter;
					TraceHunterEvent(cptr, "startle",
						static_cast<float>(sqrt(pdistSq[0])), 0.0f, 0.0f);
				}
				if (cptr->Clone == AI_GALL) cptr->State = 1;
				cptr->AfraidTime = (5 + rRand(5)) * 1024;
				if (cptr->packId >= 0) {
					Packs[cptr->packId].alert = true;
				}
			}

			pdistMulti = true;
			for (int pNo = 0; pNo < pCh; pNo++) {
				if (!(pdistSq[pNo] > 4096.f * 4096.f)) pdistMulti = false;
			}
			if (pdistMulti)
			{
				if (cptr->packId >= 0) {
					if (!Packs[cptr->packId]._alert) {
						cptr->State = 0;
						SetNewTargetPlace(cptr, 2048.f);
						goto TBEGIN;
					}
				} else {
					cptr->State = 0;
					SetNewTargetPlace(cptr, 2048.f);
					goto TBEGIN;
				}
			}
		} else if (cptr->packId >= 0) Packs[cptr->packId].alert = true;


		if (TracksHunterExactly(cptr)) {
			nv.x = playerdx[0];
			nv.z = playerdz[0];
			nv.y = 0;
			NormVector(nv, 2048.f);
			cptr->tgx = cptr->pos.x - nv.x;
			cptr->tgz = cptr->pos.z - nv.z;
			cptr->tgtime = 0;
		}
		else if (IsTimedHunterReaction(cptr)) {
			// Fixed event reactions keep the stored event position (approach
			// or flee) instead of being overwritten with a pack-relative
			// target, which made investigating pack members flee the leader.
			// The arrival search is owned by UpdateHunterNavigation.
			cptr->tgtime = 0;
		}
		else SetPackLeaderTarget(cptr, true);
	}

	// Step 4: Extend culling distance by 4 units (~1024 world units)
	// to allow smoothstep fade-out to complete
	if (pdistSq[0] > ((charViewR + 20 + 4) * 256) * ((charViewR + 20 + 4) * 256) && cptr->CType)
		if (ReplaceCharacterForward(cptr)) goto TBEGIN;


	//======== exploring area ===============//
	if (!cptr->State)
	{
		cptr->AfraidTime = 0;
		pdistMulti = false;
		for (int pNo = 0; pNo < pCh; pNo++) {
			if (pdistSq[pNo] < 812.f * 812.f) pdistMulti = true;
		}
		if (pdistMulti)
		{
			cptr->State = 1;
			// A proximity scatter is a detection (the same rule as the 2048-unit
			// refresh above): the creature knows where the hunter is and may use
			// the live position. Without the lock a solo scatter kept its stale
			// wander target and could run towards the hunter it just fled.
			if (pdistSq[0] < 812.f * 812.f) {
				cptr->hunterAwareness = HunterAwarenessState::TrackingHunter;
				TraceHunterEvent(cptr, "startle",
					static_cast<float>(sqrt(pdistSq[0])), 0.0f, 0.0f);
			}
			cptr->AfraidTime = (5 + rRand(5)) * 1024;
			cptr->Phase = DinoInfo[cptr->CType].runAnim;
			goto TBEGIN;
		}

		if (cptr->packId >= 0) {
			float leaderdx = Packs[cptr->packId].leader->pos.x - cptr->pos.x;
			float leaderdz = Packs[cptr->packId].leader->pos.z - cptr->pos.z;
			float leaderdistSq = leaderdx * leaderdx + leaderdz * leaderdz;

			if (cptr->followLeader) {
				if (leaderdistSq < (cptr->packDensity * 128 * 0.6) * (cptr->packDensity * 128 * 0.6))
				{
					cptr->followLeader = false;
					SetNewTargetPlace(cptr, 2048.f);
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
		else if (tdistSq < 456 * 456)
		{
			SetNewTargetPlace(cptr, 2048.f);
			goto TBEGIN;
		}
	}


	//============================================//

	if (cptr->NoFindCnt) cptr->NoFindCnt--;
	else
	{
		cptr->tgalpha = CorrectedAlpha(FindVectorAlpha(targetdx, targetdz), cptr->alpha);//FindVectorAlpha(targetdx, targetdz);

		if (cptr->State && pdistSq[0] > DinoInfo[cptr->CType].weaveRange * DinoInfo[cptr->CType].weaveRange && !DinoInfo[cptr->CType].dontWeave)
		{
			cptr->tgalpha += static_cast<float>(sin(RealTime / 824.f)) / 2.f;
			if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
			if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;
		}
	}
	
	LookForAWay(cptr, !DinoInfo[cptr->CType].canSwim, true);
	if (cptr->NoWayCnt > 8)
	{
		cptr->NoWayCnt = 0;
		if (cptr->Clone == AI_GALL){
			cptr->NoFindCnt = 8 + rRand(40);
		} else {
			cptr->NoFindCnt = 8 + rRand(80);
		}
	}

	if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
	if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;

	//===============================================//

	ProcessPrevPhase(cptr);

	//======== select new phase =======================//
	cptr->FTime += TimeDt;

	if (cptr->FTime >= cptr->pinfo->Animation[cptr->Phase].AniTime)
	{
		cptr->FTime %= cptr->pinfo->Animation[cptr->Phase].AniTime;
		NewPhase = true;
	}

	if (NewPhase)


		if (!cptr->State)
		{

			//if (DinoInfo[cptr->CType].idleCount) {
			if (DinoInfo[cptr->CType].idleGroupCount
			//	&& (MyHealth || !DinoInfo[cptr->CType].killType[cptr->killType].carryCorpse)
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

				
			} else cptr->Phase = DinoInfo[cptr->CType].walkAnim;

		}
		else cptr->Phase = DinoInfo[cptr->CType].runAnim;

	if (DinoInfo[cptr->CType].canSwim) {
		if (cptr->StateF & csONWATER) cptr->Phase = DinoInfo[cptr->CType].swimAnim;
	}

ENDPSELECT:

	//====== process phase changing ===========//
	if ((_Phase != cptr->Phase) || NewPhase)
		ActivateCharacterFx(cptr);

	if (_Phase != cptr->Phase)
	{
		if ((_Phase == DinoInfo[cptr->CType].runAnim ||
			_Phase == DinoInfo[cptr->CType].walkAnim) &&
			(cptr->Phase == DinoInfo[cptr->CType].runAnim ||
				cptr->Phase == DinoInfo[cptr->CType].walkAnim))
			cptr->FTime = _FTime * cptr->pinfo->Animation[cptr->Phase].AniTime / cptr->pinfo->Animation[_Phase].AniTime + 64;
		else if (!NewPhase) cptr->FTime = 0;

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

	if (cptr->currentIdleGroup >= 0) goto SKIPROT;

	if (drspd > 0.02)
		if (cptr->tgalpha > cptr->alpha) currspeed = 0.8f + drspd * 1.4f;
		else currspeed = -0.8f - drspd * 1.4f;
	else currspeed = 0;

	if (cptr->AfraidTime) currspeed *= 1.5;
	if (dalpha > pi) currspeed *= -1;
	if ((cptr->State & csONWATER) || cptr->Phase == DinoInfo[cptr->CType].walkAnim) currspeed /= 1.4f;

	DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 260.f);

	if (cptr->Clone == AI_GALL) {
		tgbend = drspd / 3;
	} else {
		tgbend = drspd / 2;
	}

	if (tgbend > pi / 2) tgbend = pi / 2;

	tgbend *= SGN(currspeed);
	if (fabs(tgbend) > fabs(cptr->bend)) DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 800.f);
	else DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 400.f);


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
	curspeed *= cptr->scale;
	DeltaFunc(cptr->vspeed, curspeed, TimeDt / 1024.f);

	MoveCharacter(cptr, cptr->lookx * cptr->vspeed * TimeDt,
		cptr->lookz * cptr->vspeed * TimeDt, !DinoInfo[cptr->CType].canSwim, true);

	//============ Y movement =================//
	if (cptr->StateF & csONWATER && DinoInfo[cptr->CType].canSwim)
	{
		cptr->pos.y = GetLandUpH(cptr->pos.x, cptr->pos.z) - (DinoInfo[cptr->CType].waterLevel + 20) * cptr->scale;
		cptr->beta /= 2;
		cptr->tggamma = 0;
	}
	else {
		ThinkY_Beta_Gamma(cptr, 64, 32, 0.7f, 0.4f);
	}

	if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) cptr->tggamma += cptr->rspeed / 12.0f;
	else cptr->tggamma += cptr->rspeed / 8.0f;
	DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 2048.f);
}
