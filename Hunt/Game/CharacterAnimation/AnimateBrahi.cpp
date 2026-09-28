// AnimateBrahi.cpp � auto-extracted from CharacterAnimation.cpp
// ==========================================================================
// Auto-generated from CharacterAnimation.cpp
// ==========================================================================

#include "Hunt.h"
#include "../CharacterAwareness.h"
#include "../CharacterInternal.h"

// Global state imported from StateDefs.cpp
extern int CurDino;
extern int NewPhase;

void AnimateBrahi(TCharacter *cptr)
{
	NewPhase = false;
	int _Phase = cptr->Phase;
	int _FTime = cptr->FTime;
	float _tgalpha = cptr->tgalpha;

	bool autoCorrect = false;

TBEGIN:
	//cptr->tgtime = 0;
	float targetx = cptr->tgx;
	float targetz = cptr->tgz;
	float targetdx = targetx - cptr->pos.x;
	float targetdz = targetz - cptr->pos.z;

	float tdist = static_cast<float>(sqrt(targetdx * targetdx + targetdz * targetdz));

	const THunterGeometry hunter = GetHunterGeometry(cptr);

	bool playerAttackable = ((GetLandUpH(PlayerX, PlayerZ) - GetLandH(PlayerX, PlayerZ)) <= 550);
	bool attacking = false;

	bool alertInit = false;
	if (cptr->State == 2) alertInit = true;
	if (cptr->packId >= 0) {
		if (!cptr->State && Packs[cptr->packId]._alert) alertInit = true;
	}

	if (alertInit) {
		NewPhase = true;
		cptr->State = 1;
	}

	if (GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z) > 140 * cptr->scale)
		cptr->StateF |= csONWATER;
	else
		cptr->StateF &= (!csONWATER);

	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) goto NOTHINK;

	//============================================//
	if (!MyHealth) cptr->State = 0;
	if (cptr->State)
	{

		cptr->currentIdleGroup = -1;

		const bool fixedPursuit = IsFixedHunterPursuit(cptr);
		const bool fixedFlee = IsFixedHunterFlee(cptr);
		const bool fixedReaction = fixedPursuit || fixedFlee;
		const bool tracksHunter = TracksHunterExactly(cptr);
		// The authored flee/pursue rule lives in the awareness core;
		// hunterAttackable carries the Brahi altitude rule.
		const bool fleeMode = ShouldFleeHunter(*cptr, hunter.distanceSquared, playerAttackable);

		if (!autoCorrect) {
			if (GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z) > 550) {
				autoCorrect = true;
				SetNewTargetPlace_Brahi(cptr, 2048.f);
				goto TBEGIN;
			}
			else if (!fleeMode)
			{
				attacking = !fixedPursuit && tracksHunter;
				// The navigator owns the live tracking / pack-leader target.
				if (!fixedReaction && tracksHunter && cptr->packId >= 0) {
					Packs[cptr->packId].alert = true;
				}
			}
			else
			{
				attacking = false;
				// The navigator owns the flee destination and the tgtime clock.

				if (cptr->packId >= 0) {
					if (cptr->AfraidTime <= 0)
					{
						if (!Packs[cptr->packId]._alert) {
							cptr->AfraidTime = 0;
							cptr->State = 0;
						}
					}
					else if (!fixedReaction) Packs[cptr->packId].alert = true;
				}
				else if (cptr->AfraidTime <= 0) {
					cptr->AfraidTime = 0;
					cptr->State = 0;
				}

			}
		}

		if (CanKillHunter(*cptr, hunter)) {
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

	// Step 4: Extend culling distance by 4 units (~1024 world units)
	// to allow smoothstep fade-out to complete
	if (hunter.distance > (charViewR + 20 + 4) * 256)
		if (ReplaceCharacterForward(cptr)) goto TBEGIN;

	if (!cptr->State)
	{
		attacking = false;
		cptr->AfraidTime = 0;


		if (cptr->packId >= 0) {
			float leaderdx = Packs[cptr->packId].leader->pos.x - cptr->pos.x;
			float leaderdz = Packs[cptr->packId].leader->pos.z - cptr->pos.z;
			float leaderdist = static_cast<float>(sqrt(leaderdx * leaderdx + leaderdz * leaderdz));

			if (cptr->followLeader) {
				if (leaderdist < cptr->packDensity * 128 * 0.6)
				{
					cptr->followLeader = false;
					SetNewTargetPlace_Brahi(cptr, 2048.f);
					goto TBEGIN;
				}
			}
			else {
				if (leaderdist > cptr->packDensity * 128 * 1.3)
				{
					cptr->followLeader = true;
				}
			}

		}

		if (cptr->followLeader) {
			cptr->tgx = Packs[cptr->packId].leader->pos.x;
			cptr->tgz = Packs[cptr->packId].leader->pos.z;
		}
		else if (tdist < 256)
		{
			SetNewTargetPlace_Brahi(cptr, 2048.f);
			goto TBEGIN;
		}
	}

NOTHINK:
	
	if ((cptr->Clone == AI_LANDBRACH || cptr->State) && !autoCorrect) {
		if (hunter.distance < 2048) cptr->NoFindCnt = 0;
		if (cptr->NoFindCnt) cptr->NoFindCnt--;
		else
		{
			cptr->tgalpha = CorrectedAlpha(FindVectorAlpha(targetdx, targetdz), cptr->alpha);//FindVectorAlpha(targetdx, targetdz);

			if (cptr->State && hunter.distance > DinoInfo[cptr->CType].weaveRange && !DinoInfo[cptr->CType].dontWeave)
			{
				cptr->tgalpha += static_cast<float>(sin(RealTime / 824.f)) / 4.f;
				if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
				if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;
			}


		}

		if (cptr->Clone == AI_LANDBRACH) {
			LookForAWay(cptr, !DinoInfo[cptr->CType].canSwim, true);
		}
		else {
			LookForAWay(cptr, true, true);
		}


		if (cptr->NoWayCnt > 12)
		{
			cptr->NoWayCnt = 0;
			cptr->NoFindCnt = 16 + rRand(20);
		}
	} else {
		cptr->tgalpha = FindVectorAlpha(targetdx, targetdz);

		if (cptr->State && hunter.distance > DinoInfo[cptr->CType].weaveRange && !DinoInfo[cptr->CType].dontWeave)
		{
			cptr->tgalpha += static_cast<float>(sin(RealTime / 824.f)) / 4.f;
			if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
			if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;
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

		if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) {
			if (DinoInfo[cptr->CType].killType[cptr->killType].dontloop) {
				cptr->Phase = DinoInfo[cptr->CType].walkAnim;
				cptr->State = 0;
			}
		}

		NewPhase = true;
	}

	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount)  goto ENDPSELECT;

	if (NewPhase)
	{
		if (!cptr->State)
		{

			if (DinoInfo[cptr->CType].idleGroupCount
				&& (MyHealth || !DinoInfo[cptr->CType].killType[cptr->killType].carryCorpse)) {


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
		else
		{
			cptr->Phase = DinoInfo[cptr->CType].runAnim;
		}
	}

	 //001 is this needed?

	 //cptr->Phase=BRA_WALK;

ENDPSELECT:

	//====== process phase changing ===========//
	if ((_Phase != cptr->Phase) || NewPhase)
		ActivateCharacterFx(cptr);

	if (_Phase != cptr->Phase)
	{

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

	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) goto SKIPROT;

	if (cptr->currentIdleGroup >= 0) goto SKIPROT;

	if (drspd > 0.02)
		if (cptr->tgalpha > cptr->alpha) currspeed = 0.2f + drspd * 0.2f;
		else currspeed = -0.2f - drspd * 0.2f;
	else currspeed = 0;
	if (cptr->AfraidTime) currspeed *= DinoInfo[cptr->CType].rotspdmulti;

	if (dalpha > pi) currspeed *= -1;

	DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 600.f);

	tgbend = drspd / 4;
	if (tgbend > pi / 4) tgbend = pi / 4;

	tgbend *= SGN(currspeed);
	DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 3200.f);

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

	if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) curspeed = DinoInfo[cptr->CType].wlkspd;
	if (cptr->Phase == DinoInfo[cptr->CType].runAnim) curspeed = DinoInfo[cptr->CType].runspd;

	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) curspeed = 0.0f;

	if (drspd > pi / 2.f) curspeed *= 2.f - 2.f*drspd / pi;

	//========== process speed =============//
	curspeed *= cptr->scale;
	DeltaFunc(cptr->vspeed, curspeed, TimeDt / 1024.f);
	cptr->pos.x += cptr->lookx * cptr->vspeed * TimeDt;
	cptr->pos.z += cptr->lookz * cptr->vspeed * TimeDt;
	

	ThinkY_Beta_Gamma(cptr, 256, 128, 0.1f, 0.2f);
	DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 4048.f);
}
