// AnimateTitan.cpp — auto-extracted from CharacterAnimation.cpp
// ==========================================================================
// Auto-generated from CharacterAnimation.cpp
// ==========================================================================

#include "Hunt.h"
#include "../CharacterInternal.h"

// Global state imported from StateDefs.cpp
extern int CurDino;
extern int NewPhase;

void AnimateTitan(TCharacter *cptr)
{
    bool fleeMode;
    float FlDst;
	NewPhase = false;
	int _Phase = cptr->Phase;
	int _FTime = cptr->FTime;
	float _tgalpha = cptr->tgalpha;

	bool alertInit = false;
	if (cptr->State == 2) alertInit = true;
	if (cptr->packId >= 0) {
		if (!cptr->State && Packs[cptr->packId]._alert) alertInit = true;
	}



	if (alertInit)
	{
		cptr->State = 1;
		if (cptr->gliding) cptr->Phase = DinoInfo[cptr->CType].flyAnim;
		else cptr->Phase = DinoInfo[cptr->CType].runAnim;
	}


TBEGIN:
	const float landH = GetLandH(cptr->pos.x, cptr->pos.z);
	const float landUpH = GetLandUpH(cptr->pos.x, cptr->pos.z);
	float targetx = cptr->tgx;
	float targetz = cptr->tgz;
	float targetdx = targetx - cptr->pos.x;
	float targetdz = targetz - cptr->pos.z;

	float tdist = static_cast<float>(sqrt(targetdx * targetdx + targetdz * targetdz));

	float playerdx, playerdz;
	playerdx = PlayerX - cptr->pos.x - cptr->lookx * 108;
	playerdz = PlayerZ - cptr->pos.z - cptr->lookz * 108;
	float pdist = static_cast<float>(sqrt(playerdx * playerdx + playerdz * playerdz));




	if (landUpH - landH > DinoInfo[cptr->CType].waterLevel * cptr->scale)
		cptr->StateF |= csONWATER;
	else
		cptr->StateF &= (!csONWATER);

	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) goto NOTHINK;

	//============================================//			// (run away)
	if (!MyHealth) cptr->State = 0;
	fleeMode = false;
	if (cptr->State)
	{

		cptr->currentIdleGroup = -1;

		float aDist;
		aDist = ctViewR * DinoInfo[cptr->CType].aggress + OptAgres / AIInfo[cptr->Clone].agressMulti;
		if (cptr->gliding) aDist *= 2;

		if (g_GameMode != GameMode::SurvivalMode) {
			if (pdist > aDist || ((PlayerY - cptr->pos.y > pdist) && cptr->gliding) ||
				DinoInfo[cptr->CType].aggress <= 0 || !cptr->awareHunter) {
				fleeMode = true;
			}
			else if (DinoInfo[cptr->CType].defensive && cptr->Health == DinoInfo[cptr->CType].Health0) fleeMode = true;
			else if (DinoInfo[cptr->CType].fearShot && cptr->Health < DinoInfo[cptr->CType].Health0) fleeMode = true;
			else if (DinoInfo[cptr->CType].fearHearShot && cptr->heardShot) fleeMode = true;
			else if (cptr->packId >= 0) Packs[cptr->packId].attack = true;
		}

		if (cptr->packId >= 0) {
			if (Packs[cptr->packId]._attack) fleeMode = false;
		}

		if (fleeMode) {
			nv.x = playerdx;
			nv.z = playerdz;
			nv.y = 0;
			NormVector(nv, 2048.f);
			cptr->tgx = cptr->pos.x - nv.x;
			cptr->tgz = cptr->pos.z - nv.z;
			cptr->tgtime = 0;
			cptr->AfraidTime -= TimeDt;

			if (cptr->packId >= 0) {
				if (cptr->AfraidTime <= 0)
				{
					if (!Packs[cptr->packId]._alert) {
						cptr->AfraidTime = 0;
						cptr->State = 0;
					}
				}
				else Packs[cptr->packId].alert = true;
			}
			else if (cptr->AfraidTime <= 0) {
				cptr->AfraidTime = 0;
				cptr->State = 0;
			}

		}
		else
		{
			cptr->tgx = PlayerX;
			cptr->tgz = PlayerZ;
			cptr->tgtime = 0;
			if (cptr->packId >= 0) {
				Packs[cptr->packId].alert = true;
			}
		}

		if (pdist < DinoInfo[cptr->CType].killDist && DinoInfo[cptr->CType].killDist > 0) {
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

	if (!cptr->State)
	{
		cptr->AfraidTime = 0;

		if (cptr->packId >= 0) {
			float leaderdx = Packs[cptr->packId].leader->pos.x - cptr->pos.x;
			float leaderdz = Packs[cptr->packId].leader->pos.z - cptr->pos.z;
			float leaderdist = static_cast<float>(sqrt(leaderdx * leaderdx + leaderdz * leaderdz));

			if (cptr->followLeader) {
				if (leaderdist < cptr->packDensity * 128 * 0.6)
				{
					cptr->followLeader = false;
					SetNewTargetPlace(cptr, AIInfo[cptr->Clone].targetDistance);
					goto TBEGIN;
				}
			} else {
				if (leaderdist > cptr->packDensity * 128 * 1.3)
				{
					cptr->followLeader = true;
				}
			}

		}

		float tdst = 456;
		if (cptr->gliding) tdst = 1024;

		if (cptr->followLeader) {
			cptr->tgx = Packs[cptr->packId].leader->pos.x;
			cptr->tgz = Packs[cptr->packId].leader->pos.z;
		}
		else if (tdist < tdst) 
		{
			SetNewTargetPlace(cptr, AIInfo[cptr->Clone].targetDistance);
			goto TBEGIN;
		}



	}

NOTHINK:
	if (pdist < AIInfo[cptr->Clone].pWMin && !cptr->gliding) cptr->NoFindCnt = 0;
	if (cptr->NoFindCnt && !cptr->gliding) cptr->NoFindCnt--;
	else
	{
		cptr->tgalpha = CorrectedAlpha(FindVectorAlpha(targetdx, targetdz), cptr->alpha);//FindVectorAlpha(targetdx, targetdz);

		if (cptr->State && pdist > DinoInfo[cptr->CType].weaveRange && !DinoInfo[cptr->CType].dontWeave)
		{
			float rTD;
			rTD = 824.f;

			cptr->tgalpha += static_cast<float>(sin(RealTime / rTD)) / AIInfo[cptr->Clone].tGAIncrement;
			if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
			if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;
		}
	}

	if (!cptr->gliding) {

		LookForAWay(cptr, !DinoInfo[cptr->CType].canSwim, true);

		if (cptr->NoWayCnt > AIInfo[cptr->Clone].noWayCntMin)
		{
			cptr->NoWayCnt = 0;
			cptr->NoFindCnt = AIInfo[cptr->Clone].noFindWayMed + rRand(AIInfo[cptr->Clone].noFindWayRange);
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

	FlDst = ctViewR * DinoInfo[cptr->CType].flyDist + OptAgres / AIInfo[cptr->Clone].agressMulti;
	if (!alertInit) FlDst *= 1.5;
	if (!cptr->gliding && cptr->State && pdist > FlDst) cptr->gliding = true;
    else if (cptr->pos.y < landUpH + 50
		&& cptr->Phase != DinoInfo[cptr->CType].takeoffAnim
		&& !(landUpH > landH)) {
		cptr->gliding = false;
	}

	if (NewPhase){

		if (!cptr->State && rRand(50) == 2) cptr->gliding = true;
		
		if (cptr->gliding) {

			if (!cptr->State || fleeMode) {
				//WANDER/FLEE
				if (!cptr->State && cptr->shakeTime) cptr->shakeTime -= 1;

				if (cptr->Phase == DinoInfo[cptr->CType].flyAnim) {
					if (cptr->pos.y > landUpH + 5800) {
						cptr->Phase = DinoInfo[cptr->CType].glideAnim;
					}
				}
				else if (cptr->Phase == DinoInfo[cptr->CType].glideAnim) {
					
					if (!cptr->shakeTime) {
						if (cptr->pos.y < landUpH + 1200) {

							//lander
							if (landUpH > landH) cptr->Phase = DinoInfo[cptr->CType].flyAnim;
							else cptr->Phase = DinoInfo[cptr->CType].landAnim;
						}
					} else {
						if (cptr->pos.y < landUpH + 3800) {
							cptr->Phase = DinoInfo[cptr->CType].flyAnim;
						}
					}

				}
				else if (cptr->Phase == DinoInfo[cptr->CType].takeoffAnim) {
					if (cptr->pos.y > landUpH + 1024) {
						cptr->Phase = DinoInfo[cptr->CType].flyAnim;
					}
				}
				else if (cptr->Phase != DinoInfo[cptr->CType].landAnim){
					cptr->beta = 0;
					cptr->gamma = 0;
					//	//TITAN_SLIDE	cptr->Slide = 0;
					cptr->Phase = DinoInfo[cptr->CType].takeoffAnim;

					cptr->shakeTime = 25 + rRand(150);//lander
				}
				
			} else {

				cptr->shakeTime = 0;//lander

				if (cptr->Phase != DinoInfo[cptr->CType].takeoffAnim &&
					cptr->Phase != DinoInfo[cptr->CType].glideAnim &&
					cptr->Phase != DinoInfo[cptr->CType].flyAnim &&
					cptr->Phase != DinoInfo[cptr->CType].diveAnim) {
					cptr->beta = 0;
					cptr->gamma = 0;
					//	//TITAN_SLIDE	cptr->Slide = 0;
					cptr->Phase = DinoInfo[cptr->CType].takeoffAnim;
				} else {
					float dalph = cptr->alpha - cptr->tgalpha;
					if (dalph < 0) dalph *= -1;
					if (dalph > pi) dalph -= pi;
					if (dalph > pi/2) {
						if(cptr->pos.y - PlayerY < pdist / 2) cptr->Phase = DinoInfo[cptr->CType].takeoffAnim;
						else if (cptr->pos.y > PlayerY + 600) cptr->Phase = DinoInfo[cptr->CType].glideAnim;
						else cptr->Phase = DinoInfo[cptr->CType].flyAnim;
					} else {
						if (cptr->pos.y < PlayerY + 256 && pdist > 2048) cptr->Phase = DinoInfo[cptr->CType].takeoffAnim;
						else if (cptr->pos.y - PlayerY > pdist / (1.4 * DinoInfo[cptr->CType].divspd)) cptr->Phase = DinoInfo[cptr->CType].diveAnim;
						else if (cptr->pos.y > PlayerY + 600) cptr->Phase = DinoInfo[cptr->CType].glideAnim;
						else cptr->Phase = DinoInfo[cptr->CType].flyAnim;
					}

					
				}
				
			}
			
		} else {

			cptr->shakeTime = 0;//lander

			if (!cptr->State) {

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

				} else {
					cptr->Phase = DinoInfo[cptr->CType].walkAnim;
				}

			} else {
				cptr->Phase = DinoInfo[cptr->CType].runAnim;

				if (fabs(cptr->pos.y - PlayerY) > pdist / 2) {
					cptr->beta = 0;
					cptr->gamma = 0;
					cptr->gliding = true;
					cptr->Phase = DinoInfo[cptr->CType].takeoffAnim;
				}

			}

		}
	}

	if (cptr->currentIdleGroup == -1 && !cptr->gliding) {
		if (!cptr->State) cptr->Phase = DinoInfo[cptr->CType].walkAnim;
		else if (fabs(cptr->tgalpha - cptr->alpha) < 1.0 ||
			fabs(cptr->tgalpha - cptr->alpha) > 2 * pi - 1.0)
			cptr->Phase = DinoInfo[cptr->CType].runAnim;
		else cptr->Phase = DinoInfo[cptr->CType].walkAnim;

	}

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

		if (cptr->gliding) {
			if (!NewPhase) cptr->FTime = 0;
		} else if (MORPHP) {
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
	float dalpha = fabs(cptr->tgalpha - cptr->alpha);
	float drspd = dalpha;
	if (drspd > pi) drspd = 2 * pi - drspd;
	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) goto SKIPROT;
	if (cptr->currentIdleGroup >= 0) goto SKIPROT;




	if (drspd > 0.02)
		if (cptr->tgalpha > cptr->alpha) currspeed = 0.6f + drspd * 1.2f;
		else currspeed = -0.6f - drspd * 1.2f;
	else currspeed = 0;
	if (cptr->AfraidTime && !cptr->gliding) currspeed *= 2.5;
	if (cptr->gliding) currspeed /= 2;

	if (dalpha > pi) currspeed *= -1;
	if (((cptr->StateF & csONWATER) || cptr->Phase == DinoInfo[cptr->CType].walkAnim ) && !cptr->gliding) currspeed /= 1.4f;

	if (cptr->gliding) DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 460.f);
	else if (cptr->AfraidTime) DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 160.f);
	else DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 180.f);

	if (cptr->gliding) {
		tgbend = drspd / 2.f;
		if (tgbend > pi / 10) tgbend = pi / 10;
	} else {
		tgbend = drspd / AIInfo[cptr->Clone].targetBendRotSpd;
		if (tgbend > pi / 5) tgbend = pi / 5;
	}

	tgbend *= SGN(currspeed);
	if (fabs(tgbend) > fabs(cptr->bend)) DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 800.f);
	else if (cptr->gliding) DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 400.f);
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
	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim) curspeed = DinoInfo[cptr->CType].flyspd;
	if (cptr->Phase == DinoInfo[cptr->CType].glideAnim) curspeed = DinoInfo[cptr->CType].gldspd;
	if (cptr->Phase == DinoInfo[cptr->CType].takeoffAnim) curspeed = DinoInfo[cptr->CType].tkfspd;
	if (cptr->Phase == DinoInfo[cptr->CType].diveAnim) curspeed = DinoInfo[cptr->CType].divspd;
	if (cptr->Phase == DinoInfo[cptr->CType].landAnim) curspeed = DinoInfo[cptr->CType].lndspd;
	if (DinoInfo[cptr->CType].canSwim) {
		if (cptr->Phase == DinoInfo[cptr->CType].swimAnim) curspeed = DinoInfo[cptr->CType].swmspd;
	}

	if (cptr->Phase == DinoInfo[cptr->CType].killType[cptr->killType].anim && DinoInfo[cptr->CType].killTypeCount) curspeed = 0.0f;

	
	if (drspd > pi / 2.f) curspeed *= 2.f - 2.f*drspd / pi;

	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim) cptr->pos.y += TimeDt / 5.f;
	if (cptr->Phase == DinoInfo[cptr->CType].takeoffAnim) cptr->pos.y += TimeDt / 4.f;
	if (cptr->Phase == DinoInfo[cptr->CType].glideAnim) cptr->pos.y -= TimeDt / 10.f;
	if (cptr->Phase == DinoInfo[cptr->CType].landAnim) cptr->pos.y -= TimeDt;
	if (cptr->Phase == DinoInfo[cptr->CType].diveAnim) cptr->pos.y -= TimeDt;

	//if (cptr->pos.y < landH + 236) cptr->pos.y = landH + 256;

	//========== process speed =============//

	if (cptr->gliding) {
		curspeed *= cptr->scale;
		DeltaFunc(cptr->vspeed, curspeed, TimeDt / 2024.f);

		cptr->pos.x += cptr->lookx * cptr->vspeed * TimeDt;
		cptr->pos.z += cptr->lookz * cptr->vspeed * TimeDt;

		cptr->tggamma = cptr->rspeed / 1.5f;
		if (cptr->tggamma > pi / 3.f) cptr->tggamma = pi / 3.f;
		if (cptr->tggamma < -pi / 3.f) cptr->tggamma = -pi / 3.f;
		DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 3048.f);

	} else {

		DeltaFunc(cptr->vspeed, curspeed, TimeDt / 500.f);

		MoveCharacter(cptr, cptr->lookx * cptr->vspeed * TimeDt * cptr->scale,
			cptr->lookz * cptr->vspeed * TimeDt * cptr->scale, !DinoInfo[cptr->CType].canSwim, true);

		


		//============ Y movement =================//

		if (cptr->pos.y < landH) cptr->pos.y = landH;

		if (cptr->StateF & csONWATER && DinoInfo[cptr->CType].canSwim)
		{
			cptr->pos.y = landUpH - (DinoInfo[cptr->CType].waterLevel + 20) * cptr->scale;
			cptr->beta /= 2;
			cptr->tggamma = 0;
		}
		else
		{
			ThinkY_Beta_Gamma(cptr,
				AIInfo[cptr->Clone].yBetaGamma1,
				AIInfo[cptr->Clone].yBetaGamma2,
				AIInfo[cptr->Clone].yBetaGamma3,
				AIInfo[cptr->Clone].yBetaGamma4);
		}

		//=== process to tggamma ===//
		if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) cptr->tggamma += cptr->rspeed / AIInfo[cptr->Clone].walkTargetGammaRot;
		else cptr->tggamma += cptr->rspeed / AIInfo[cptr->Clone].targetGammaRot;

		DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 1624.f);

		//==================================================//


	}

}
