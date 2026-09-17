// AnimateFish.cpp � auto-extracted from CharacterAnimation.cpp
// ==========================================================================
// Auto-generated from CharacterAnimation.cpp
// ==========================================================================

#include "Hunt.h"
#include "../CharacterInternal.h"

// Global state imported from StateDefs.cpp
extern int CurDino;
extern int NewPhase;

void AnimateFish(TCharacter *cptr)
{
    int ao;
    float attackDist;
	NewPhase = false;
	int _Phase = cptr->Phase;
	int _FTime = cptr->FTime;
	float _tgalpha = cptr->tgalpha;
	Vector3d _pos = cptr->pos;
	float _depth = cptr->depth;
	float _beta = cptr->beta;

TBEGIN:
	float targetx = cptr->tgx;
	float targetz = cptr->tgz;
	float targety = cptr->tdepth;
	float targetdx = targetx - cptr->pos.x;
	float targetdz = targetz - cptr->pos.z;
	float targetdy = targety - cptr->depth;

	float tdist2Sq = targetdx * targetdx + targetdz * targetdz;

	//float attackDist = 1024.f;
	//if (DinoInfo[cptr->CType].DangerFish) {
	//	attackDist = DinoInfo[cptr->CType].aggress;
	//}

	float playerdx = PlayerX - cptr->pos.x - cptr->lookx * 100 *cptr->scale;
	float playerdz = PlayerZ - cptr->pos.z - cptr->lookz * 100 *cptr->scale;
	float pdistSq = playerdx * playerdx + playerdz * playerdz;

	// Step 4: Extend culling distance by 4 units (~1024 world units)
	// to allow smoothstep fade-out to complete
	if (pdistSq > ((charViewR + 20 + 4) * 256) * ((charViewR + 20 + 4) * 256)) {
		if (ReplaceCharacterForward(cptr)) {
			goto TBEGIN;
		}
	}

	//REMOVED - turny !!!
	//if (cptr->State == 2)
	//{
	//	NewPhase = true;
	//	cptr->State = 1;
	//}

	float tv;
	switch (cptr->Clone) {
	 case AI_FISH: tv = 1024.f;
	 case AI_MOSA: tv = 5024.f;
	}

	// JUMP & IDLE PARTICLES

	//int Scal = ((cptr->scale * 2) - 1);
	// NOTE: float-first arithmetic is load-bearing: ((ctViewR+20)*256)^2 overflows
	// int32 above ctViewR 161 (wraps negative, killing all fish particles at high
	// view distance). Same bug class as the MakeCall dminSq overflow (issue #1).
	const float particleRange = static_cast<float>(ctViewR + 20) * 256.f;
	if (pdistSq < particleRange * particleRange) {	//Only create particles within player render distance
		if (DinoInfo[cptr->CType].partCnt[cptr->Phase]) {
			if (cptr->FTime > DinoInfo[cptr->CType].partFrame1[cptr->Phase] / cptr->pinfo->Animation[cptr->Phase].aniKPS
				&& cptr->FTime < DinoInfo[cptr->CType].partFrame2[cptr->Phase] / cptr->pinfo->Animation[cptr->Phase].aniKPS) {
				for (int i = 0; i < static_cast<int>(sqrt(DinoInfo[cptr->CType].partCnt[cptr->Phase]* ((cptr->scale * 3) - 2))); i++) {
					float xo = static_cast<int>(siRand(static_cast<int>(DinoInfo[cptr->CType].partDist[cptr->Phase])* cptr->scale)) + cptr->pos.x +
						((cos(cptr->alpha)  * ((cptr->scale * 1.5) - 0.5) * DinoInfo[cptr->CType].partOffset[cptr->Phase]));
					float zo = static_cast<int>(siRand(static_cast<int>(DinoInfo[cptr->CType].partDist[cptr->Phase]) * cptr->scale)) + cptr->pos.z +
						((sin(cptr->alpha)  * ((cptr->scale * 1.5) - 0.5) * DinoInfo[cptr->CType].partOffset[cptr->Phase]));
					AddElementsA(xo,
						GetLandUpH(xo, zo),
						zo,
						2,
						5,
						DinoInfo[cptr->CType].partMag[cptr->Phase],
						DinoInfo[cptr->CType].partAngled[cptr->Phase],
						cptr->alpha);
					if (DinoInfo[cptr->CType].partCircle[cptr->Phase]) AddWCircle(xo, zo, 1.2);
				}
			}
		}
	}


	if (GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z) > 180 * cptr->scale)
		cptr->StateF |= csONWATER;
	else
		cptr->StateF &= (!csONWATER);

	bool playerInWater = GetLandUpH(PlayerX, PlayerZ) - GetLandH(PlayerX, PlayerZ) > 0;


	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) goto NOTHINK;

	//============================================//
	if (!MyHealth) cptr->State = 0;

	ao = 0;
	if (DinoInfo[cptr->CType].DangerFish)ao = OptAgres;
	attackDist = ctViewR * DinoInfo[cptr->CType].aggress + ao / AIInfo[cptr->Clone].agressMulti;

	if (!cptr->State)
	{

		bool attackmode = pdistSq <= attackDist * attackDist && playerInWater && !DinoInfo[cptr->CType].dontSwimAway
			&& MyHealth && !ObservMode && !DEBUG;
		if (g_GameMode == GameMode::SurvivalMode) attackmode = true;
		if (attackmode)	cptr->AfraidTime = static_cast<int>((10.f)) * 1024;
		if (cptr->packId >= 0 && MyHealth) {
			if (attackmode) Packs[cptr->packId].alert = true;
			if (Packs[cptr->packId]._alert) attackmode = true;
		}

		if (attackmode) {
			cptr->State = 1;
			cptr->turny = 0;
			cptr->lastTBeta = cptr->beta;
			//cptr->AfraidTime = static_cast<int>((10.f)) * 1024;
			//goto TBEGIN;
		} else {

			if (cptr->packId >= 0) {
				float leaderdx = Packs[cptr->packId].leader->pos.x - cptr->pos.x;
				float leaderdz = Packs[cptr->packId].leader->pos.z - cptr->pos.z;
				float leaderdistSq = leaderdx * leaderdx + leaderdz * leaderdz;


				if (cptr->followLeader) {
					if (leaderdistSq < (cptr->packDensity * 128 * 0.6) * (cptr->packDensity * 128 * 0.6))
					{
						cptr->followLeader = false;
						SetNewTargetPlaceFish(cptr, tv);
						goto TBEGIN;
					}
				}
				else {
					if (leaderdistSq > (cptr->packDensity * 128 * 1.3) * (cptr->packDensity * 128 * 1.3))
					{
						cptr->followLeader = true;
						cptr->turny = 0;
						cptr->lastTBeta = cptr->beta;
					}
				}

			}

			if (cptr->followLeader) {
				cptr->tgx = Packs[cptr->packId].leader->pos.x;
				cptr->tgz = Packs[cptr->packId].leader->pos.z;
				cptr->tdepth = Packs[cptr->packId].leader->depth;

			} else if (tdist2Sq < 456 * 456) // Ignore vertical
			{
				SetNewTargetPlaceFish(cptr, tv);
				goto TBEGIN;
			}
		}
	}

	if (cptr->State)
	{
		if (pdistSq > attackDist * attackDist || !playerInWater)
		{
			cptr->AfraidTime -= TimeDt;

			if (cptr->packId >= 0) {
				if (cptr->AfraidTime <= 0) {

					if (!Packs[cptr->packId]._alert) {
						cptr->AfraidTime = 0;
						cptr->State = 0;
						SetNewTargetPlaceFish(cptr, tv);
						goto TBEGIN;
					}

				} else Packs[cptr->packId].alert = true;
			} else if (cptr->AfraidTime <= 0) {
				cptr->AfraidTime = 0;
				cptr->State = 0;
				SetNewTargetPlaceFish(cptr, tv);
				goto TBEGIN;
			}




		}

		if (DinoInfo[cptr->CType].DangerFish || g_GameMode == GameMode::SurvivalMode) {
			cptr->tgx = PlayerX;
			cptr->tgz = PlayerZ;
			cptr->tgtime = 0;
			cptr->tdepth = PlayerY;


			// Mosa Target Depth Failsafes
			if (cptr->tdepth > GetLandUpH(cptr->tgx, cptr->tgz) - (cptr->spcDepth * 0.75)) {
				cptr->tdepth = GetLandUpH(cptr->pos.x, cptr->pos.z) - (cptr->spcDepth * 0.75);
			}

			//Target above the player so it can get to jumping depth in time.
			if (AIInfo[cptr->Clone].jumper) {
				if (cptr->depth < cptr->tdepth) {
					cptr->tdepth += (cptr->tdepth - cptr->depth) * 3;
					//float haw = (GetLandUpH(cptr->tgx, cptr->tgz) - GetLandH(cptr->tgx, cptr->tgz));
					//if (haw) cptr->tdepth *= (cptr->tdepth - GetLandH(cptr->tgx, cptr->tgz)) / haw;
				}
			}

			if (cptr->packId >= 0) {
				Packs[cptr->packId].alert = true;
			}

		}
		else
		{
			nv.x = playerdx;
			nv.z = playerdz;
			nv.y = 0;
			NormVector(nv, 2048.f);
			cptr->tgx = cptr->pos.x - nv.x;
			cptr->tgz = cptr->pos.z - nv.z;

			cptr->tdepth = GetLandH(cptr->pos.x, cptr->pos.z) +
				((GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z)) / 2);
		}

		cptr->tgtime = 0;

		if (cptr->Phase != DinoInfo[cptr->CType].jumpAnim){
			if (AIInfo[cptr->Clone].jumper && DinoInfo[cptr->CType].DangerFish) {
				if (cptr->depth > GetLandUpH(cptr->pos.x, cptr->pos.z) - (cptr->spcDepth * 0.95)){
					float pUp = PlayerY - GetLandUpH(PlayerX, PlayerZ); //jump later if the player is on a low bridge, not at all if too high
					if (pUp < 0) pUp = 0;
					float md = ((DinoInfo[cptr->CType].jumpRange * DinoInfo[cptr->CType].jmpspd) - (pUp * 1.3)) * cptr->scale;
					float jumpMin = md - 200;
					if (pdistSq < md * md && (jumpMin <= 0 || pdistSq > jumpMin * jumpMin))//1200
						if (AngleDifference(cptr->alpha, FindVectorAlpha(playerdx, playerdz)) < 0.2f) {

							Vector3d pv;
							pv.x = PlayerX;
							pv.z = PlayerZ;

							if (!CheckPlaceCollisionFish(cptr, pv, cptr->depth,
								DinoInfo[cptr->CType].maxDepth,
								DinoInfo[cptr->CType].minDepth)) {

								cptr->Phase = DinoInfo[cptr->CType].jumpAnim;
								NewPhase = true;
								cptr->FTime = 0;
								cptr->bend = 0;
								cptr->bdepth = 0;

							}

						}
				}
			}
		}

		if (pdistSq < (DinoInfo[cptr->CType].killDist * cptr->scale) * (DinoInfo[cptr->CType].killDist * cptr->scale) && DinoInfo[cptr->CType].killDist > 0) {
			float killAlt = cptr->spcDepth;
			if (killAlt < 256) killAlt = 256;
			if (AIInfo[cptr->Clone].jumper && cptr->Phase == DinoInfo[cptr->CType].jumpAnim) killAlt += 80;
			if (fabs(PlayerY - cptr->pos.y) < killAlt + 20 * cptr->scale)
			{

				if (DinoInfo[cptr->CType].killTypeCount > 0) {

					cptr->vspeed /= 8.0f;
					cptr->State = 1;
					cptr->Phase = DinoInfo[cptr->CType].killType[cptr->killType].anim;
					if (DinoInfo[cptr->CType].killType[cptr->killType].dontloop) cptr->FTime = 0;
					//cptr->FTime = 0;
					AddDeadBody(cptr,
						DinoInfo[cptr->CType].killType[cptr->killType].hunteranim,
						DinoInfo[cptr->CType].killType[cptr->killType].scream);
				}
				else {
					AddDeadBody(cptr, HUNT_EAT, true);
					cptr->State = 0;
				}

				cptr->aquaticIdle = false;

			}
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
			cptr->tgalpha += static_cast<float>(sin(RealTime / 824.f)) / 2.f;
			if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
			if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;
		}
	}

	LookForAWay(cptr, false, true);
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

	if (AIInfo[cptr->Clone].jumper) {
		if (NewPhase && _Phase == DinoInfo[cptr->CType].jumpAnim)
		{
			cptr->Phase = DinoInfo[cptr->CType].runAnim;
			goto ENDPSELECT;
		}

		if (cptr->Phase == DinoInfo[cptr->CType].jumpAnim) goto ENDPSELECT;
	}

	

	if (cptr->State) cptr->aquaticIdle = false;
	else if (DinoInfo[cptr->CType].lookCount > 0) {
		for (int i = 0; i < DinoInfo[cptr->CType].lookCount; i++) {
			if (NewPhase && _Phase == DinoInfo[cptr->CType].lookAnim[i]) {
				cptr->aquaticIdle = false;
			}
		}
	}

	if (NewPhase) {
		if (!cptr->State) {
			cptr->Phase = DinoInfo[cptr->CType].walkAnim;
			if (DinoInfo[cptr->CType].lookCount){
				if (!cptr->aquaticIdle && rRand(128) > AIInfo[cptr->Clone].idleStart
					&& (MyHealth || !DinoInfo[cptr->CType].killType[cptr->killType].carryCorpse)
					) { // Don't play idles when carrying hunters corpse
					cptr->aquaticIdle = true;
				}

				if (cptr->aquaticIdle &&
					MyHealth && // Don't play idles when carrying hunters corpse
					cptr->depth > GetLandUpH(cptr->pos.x, cptr->pos.z) - (cptr->spcDepth * 0.8) &&
					fabs(cptr->beta) < pi / 32 &&
					fabs(cptr->gamma) < pi / 32 &&
					fabs(cptr->bend) < pi / 32) {

					cptr->Phase = DinoInfo[cptr->CType].lookAnim[rRand(DinoInfo[cptr->CType].lookCount - 1)];
					NewPhase = true;
					cptr->FTime = 0;
					goto ENDPSELECT;
				}

			}
		} else cptr->Phase = DinoInfo[cptr->CType].runAnim;

	}

	

	//if (cptr->StateF & csONWATER) cptr->Phase = RAP_SWIM;
	//if (cptr->Slide > 40) cptr->Phase = RAP_SLIDE;


ENDPSELECT:

	//====== process phase changing ===========//
	if ((_Phase != cptr->Phase) || NewPhase)
	{

		
		ActivateCharacterFxAquatic(cptr);
		if (cptr->Phase != DinoInfo[cptr->CType].walkAnim && cptr->Phase != DinoInfo[cptr->CType].runAnim) {
			ActivateCharacterFx(cptr);
		}



	}

	if (_Phase != cptr->Phase)
	{
		//==== set proportional FTime for better morphing =//
		//if (MORPHP)
		//	if (_Phase <= 3 && cptr->Phase <= 3)
		
		if ((_Phase == DinoInfo[cptr->CType].runAnim ||
			_Phase == DinoInfo[cptr->CType].walkAnim) &&
			(cptr->Phase == DinoInfo[cptr->CType].runAnim ||
				cptr->Phase == DinoInfo[cptr->CType].walkAnim)) {
			cptr->FTime = _FTime * cptr->pinfo->Animation[cptr->Phase].AniTime / cptr->pinfo->Animation[_Phase].AniTime + 64;
		}
		//else if (!NewPhase) cptr->FTime = 0;

		if (cptr->PPMorphTime > 128)
		{
			cptr->PrevPhase = _Phase;
			cptr->PrevPFTime = _FTime;
			cptr->PPMorphTime = 0;
		}
	}

	cptr->FTime %= cptr->pinfo->Animation[cptr->Phase].AniTime;



	//========== rotation to tgalpha ===================//

	//OLD BACKUP
	

	float rspd, currspeed, tgbend;
	float dalpha = static_cast<float>(fabs(cptr->tgalpha - cptr->alpha));
	float drspd = dalpha;
	if (drspd > pi) drspd = 2 * pi - drspd;

	if (AIInfo[cptr->Clone].jumper) {
		if (cptr->Phase == DinoInfo[cptr->CType].jumpAnim) goto SKIPROT;
	}

	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) goto SKIPROT;
		
	for (int i = 0; i < DinoInfo[cptr->CType].lookCount; i++) {
		if (cptr->Phase == DinoInfo[cptr->CType].lookAnim[i]) goto SKIPROT;
	}
	
	if (drspd > 0.02)
		if (cptr->tgalpha > cptr->alpha) currspeed = 0.6f + drspd * 1.2f;
		else currspeed = -0.6f - drspd * 1.2f;
	else currspeed = 0;
	//if (cptr->AfraidTime) currspeed *= 2.5;

	if (dalpha > pi) currspeed *= -1;
	currspeed /= 1.4f;

	if (cptr->AfraidTime) DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 250.f);
	else DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 460.f);

	tgbend = drspd / 2;
	if (tgbend > pi / 5) tgbend = pi / 5;

	tgbend *= SGN(currspeed);
	if (fabs(tgbend) > fabs(cptr->bend)) DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 800.f);
	else DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 600.f);


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
	if (AIInfo[cptr->Clone].jumper) {
		if (cptr->Phase == DinoInfo[cptr->CType].jumpAnim) curspeed = DinoInfo[cptr->CType].jmpspd;
	}

	if (DinoInfo[cptr->CType].lookCount > 0) {
		for (int i = 0; i < DinoInfo[cptr->CType].lookCount; i++) {
			if (cptr->Phase == DinoInfo[cptr->CType].lookAnim[i]) {
				curspeed = DinoInfo[cptr->CType].wlkspd;
			}
		}
	}


	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) curspeed = 0.0f;

	 if (drspd > pi / 2.f) curspeed *= 2.f - 2.f*drspd / pi;

	//========== process speed =============//

	DeltaFunc(cptr->vspeed, curspeed, TimeDt / 500.f);

	if (AIInfo[cptr->Clone].jumper) {
		if (cptr->Phase == DinoInfo[cptr->CType].jumpAnim) cptr->vspeed = DinoInfo[cptr->CType].jmpspd;
	}

	MoveCharacterFish(cptr, cptr->lookx * cptr->vspeed * TimeDt * cptr->scale,
		cptr->lookz * cptr->vspeed * TimeDt * cptr->scale);

	

	//============ Y movement =================//


	float tdx2 = cptr->tgx - cptr->pos.x;
	float tdz2 = cptr->tgz - cptr->pos.z;
	float tdist22 = static_cast<float>(sqrt(tdx2 * tdx2 + tdz2 * tdz2)); //need this, it's an updated target dist

	float tbeta = -atan((cptr->tdepth - cptr->depth) / tdist22);

	if (cptr->turny < (pi)) {
		tbeta = (((cos(cptr->turny) + 1) / 2) * (cptr->lastTBeta - tbeta)) + tbeta;
		cptr->turny += pi / 100;
	}
	DeltaFunc(cptr->beta,tbeta, cptr->vspeed * TimeDt * cptr->scale*(pi/5000));

	if (cptr->Clone == AI_MOSA && cptr->Phase == DinoInfo[cptr->CType].walkAnim) {
		//cptr->depth -= cptr->beta * 10;
		cptr->depth -= cptr->beta * 25 * curspeed;

	} else {
		cptr->depth -= cptr->beta * 35 * curspeed;
	}

	float newBend = (_beta - cptr->beta) * 25;
	float max = 0.2;
	float maxIt = max / 6;

	if (fabs(cptr->bdepth - newBend) > maxIt) {
		if (newBend > cptr->bdepth) {
			cptr->bdepth += maxIt;
			//if (cptr->bdepth > max) cptr->bdepth = max; - see below
		}
		else {
			cptr->bdepth -= maxIt;
			//if (cptr->bdepth < -max) cptr->bdepth = -max; - see below
		}
	}
	else {
		cptr->bdepth = newBend;
	}
	if (cptr->bdepth > max) cptr->bdepth = max;
	if (cptr->bdepth < -max) cptr->bdepth = -max;

	

	if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) {
		cptr->tggamma = cptr->bend;
	}
	else {
		cptr->tggamma = cptr->bend * 2;	//run anim only
	}

	//=== process to tggamma ===//
	if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) cptr->tggamma += cptr->rspeed / 10.0f;
	else cptr->tggamma += cptr->rspeed / 8.0f;

	if (AIInfo[cptr->Clone].jumper) {
		if (cptr->Phase == DinoInfo[cptr->CType].jumpAnim) cptr->tggamma = 0;
	}

	DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 1624.f);


	// Mosa Depth Failsafes
	if (cptr->depth > GetLandUpH(cptr->pos.x, cptr->pos.z) - (cptr->spcDepth / 2)) {
		cptr->depth = GetLandUpH(cptr->pos.x, cptr->pos.z) - (cptr->spcDepth / 2);
		cptr->tdepth = GetLandH(cptr->pos.x, cptr->pos.z) +
			((GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z)) / 2);
		cptr->lastTBeta = cptr->beta;
	}
	if (cptr->depth < GetLandH(cptr->pos.x, cptr->pos.z) + (cptr->spcDepth / 2)) {
		cptr->depth = GetLandH(cptr->pos.x, cptr->pos.z) + (cptr->spcDepth / 2);
		cptr->tdepth = GetLandH(cptr->pos.x, cptr->pos.z) +
			((GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z)) / 2);
		cptr->lastTBeta = cptr->beta;
	}

	//==================================================//

	cptr->pos.y = cptr->depth;

}
