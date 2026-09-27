// AnimateIcth.cpp � auto-extracted from CharacterAnimation.cpp
// ==========================================================================
// Auto-generated from CharacterAnimation.cpp
// ==========================================================================

#include "Hunt.h"
#include "../CharacterInternal.h"

// Global state imported from StateDefs.cpp
extern int CurDino;
extern int NewPhase;

void AnimateIcth(TCharacter *cptr)
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

	cptr->FTime += TimeDt;

TBEGIN:
	float targetx = cptr->tgx;
	float targetz = cptr->tgz;
	float targetdx = targetx - cptr->pos.x;
	float targetdz = targetz - cptr->pos.z;

	float tdistSq = targetdx * targetdx + targetdz * targetdz;

	float playerdx = PlayerX - cptr->pos.x;
	float playerdz = PlayerZ - cptr->pos.z;
	float pdistSq = playerdx * playerdx + playerdz * playerdz;
	float playerdy = PlayerY - cptr->pos.y;
	float pdistUpSq = pdistSq + playerdy * playerdy;

	//	if (cptr->AfraidTime && !(_Phase == ICTH_FLY || _Phase == ICTH_LANDING || _Phase == ICTH_FLY2 || _Phase == ICTH_TAKEOFF || _Phase == ICTH_WINGUP_WATER || _Phase == ICTH_WINGUP_LAND))
	//	{
	//		cptr->wingUp = true;
	//	}

	if (GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z) > 20)
		cptr->StateF |= csONWATER;
	else
		cptr->StateF &= (!csONWATER);

	//=========== run away =================//
	if (cptr->State)
	{

		cptr->currentIdleGroup = -1;
		cptr->currentIdle2Group = -1;

		bool relax = false;
		if (cptr->packId >= 0) {
			if (!cptr->AfraidTime) {
				if (!Packs[cptr->packId]._alert) relax = true;
			} else Packs[cptr->packId].alert = true;
		} else if (!cptr->AfraidTime) relax = true;

		if (relax)
		{
			if (cptr->pos.y >= GetLandUpH(cptr->pos.x, cptr->pos.z) + 236)
			{
				cptr->gliding = true;
				SetNewTargetPlace_Icth(cptr, 2048.f);
			}
			else
			{
				cptr->Phase = DinoInfo[cptr->CType].landAnim;
				NewPhase = true;
				SetNewTargetPlace_Icth(cptr, 2048.f);
			}
			cptr->State = 0;
			goto TBEGIN;
		}

	}


	// Step 4: Extend culling distance by 4 units (~1024 world units)
	// to allow smoothstep fade-out to complete
	if (pdistSq > ((charViewR + 20 + 4) * 256) * ((charViewR + 20 + 4) * 256))
		if (ReplaceCharacterForward(cptr)) goto TBEGIN;

	//======== exploring area ===============//
	if (!cptr->State)
	{
		cptr->AfraidTime = 0;
		if (pdistUpSq < 1050.f * 1050.f)
		{
			cptr->State = 1;
			SetNewTargetPlace_Icth(cptr, 2048.f);
			cptr->AfraidTime = (50 + rRand(8)) * 1024;
			NewPhase = true;
			if (cptr->packId >= 0) Packs[cptr->packId].alert = true;
			goto TBEGIN;
		}

	}

	int targetNear = 456;

	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim || cptr->Phase == DinoInfo[cptr->CType].glideAnim) {
		targetNear = 2024;
	}

	
	if (cptr->packId >= 0) {
		float leaderdx = Packs[cptr->packId].leader->pos.x - cptr->pos.x;
		float leaderdz = Packs[cptr->packId].leader->pos.z - cptr->pos.z;
		float leaderdistSq = leaderdx * leaderdx + leaderdz * leaderdz;

		if (cptr->followLeader) {
			if (leaderdistSq < (cptr->packDensity * 128 * 0.6) * (cptr->packDensity * 128 * 0.6))
			{
				cptr->followLeader = false;
				SetNewTargetPlace_Icth(cptr, 4048.f);
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
	else if (tdistSq < targetNear * targetNear)
	{
		SetNewTargetPlace_Icth(cptr, 2048.f);
		goto TBEGIN;
	}
	


	//===============================================//

	ProcessPrevPhase(cptr);



	//======== select new phase =======================//


	if (cptr->FTime >= cptr->pinfo->Animation[cptr->Phase].AniTime)
	{
		cptr->FTime %= cptr->pinfo->Animation[cptr->Phase].AniTime;
		NewPhase = true;
	}

	float wy = GetLandUpH(cptr->pos.x,
		cptr->pos.z) - GetLandH(cptr->pos.x,
			cptr->pos.z);
	float swimLevel = DinoInfo[cptr->CType].waterLevel * cptr->scale;// 40;

	if (NewPhase)
	{
		if (!cptr->State)
		{

			if (cptr->gliding == true)
			{
				cptr->Phase = DinoInfo[cptr->CType].glideAnim;
			}
			else if (cptr->Phase != DinoInfo[cptr->CType].landAnim)
			{
				if (wy >= swimLevel) {
					

					if (DinoInfo[cptr->CType].idle2GroupCount) {

						if (cptr->currentIdle2Group >= 0) {
							if (rRand(127) + 1 > (1 - DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].end) * 128
								&& (DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].endOnAny
									|| cptr->Phase == DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].anim[DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].count - 1])) {
								cptr->Phase = DinoInfo[cptr->CType].swimAnim;
								if (DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].instantRepeat) {
									cptr->currentIdle2Group = -1; //this must be done inside the if statement
								}
								else {
									cptr->currentIdle2Group = -1; //this must be done inside the if statement
									goto ENDPSELECT;
								}
							}
							else {
								cptr->Phase = DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].anim[rRand(DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].count - 1)];
								goto ENDPSELECT;
							}
						}

						for (int idle2GroupNo = 0; idle2GroupNo < DinoInfo[cptr->CType].idle2GroupCount; idle2GroupNo++) {
							if (rRand(127) + 1 > (1 - DinoInfo[cptr->CType].idle2Group[idle2GroupNo].start) * 128) cptr->currentIdle2Group = idle2GroupNo;
						}
						if (cptr->currentIdle2Group >= 0) {
							if (DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].startOnAny)
								cptr->Phase = DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].anim[rRand(DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].count - 1)];
							else
								cptr->Phase = DinoInfo[cptr->CType].idle2Group[cptr->currentIdle2Group].anim[0];
							goto ENDPSELECT;
						}
						else cptr->Phase = DinoInfo[cptr->CType].swimAnim;

					}
					else cptr->Phase = DinoInfo[cptr->CType].swimAnim;


				}
				else
				{
					

					if (DinoInfo[cptr->CType].idleGroupCount) {

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
					
				}
			}

		}
		
		if (cptr->State) {

			bool afraid = false;
			if (cptr->AfraidTime) afraid = true;

			if (cptr->packId >= 0) {
				if (Packs[cptr->packId]._alert) afraid = true;
			}


			if (afraid) {

				if (cptr->Phase == DinoInfo[cptr->CType].flyAnim)
				{
					if (cptr->pos.y > GetLandUpH(cptr->pos.x, cptr->pos.z) + 2100)
					{
						cptr->Phase = DinoInfo[cptr->CType].glideAnim;
						SetNewTargetPlace_Icth(cptr, 2048.f);
					}
				}
				else if (cptr->Phase == DinoInfo[cptr->CType].glideAnim)
				{
					if (cptr->pos.y < GetLandUpH(cptr->pos.x, cptr->pos.z) + 1600)
					{
						cptr->Phase = DinoInfo[cptr->CType].flyAnim;
						SetNewTargetPlace_Icth(cptr, 2048.f);
					}
				}
				else if (cptr->Phase == DinoInfo[cptr->CType].takeoffAnim)
				{
					if (cptr->pos.y > GetLandUpH(cptr->pos.x, cptr->pos.z) + 236)
					{
						cptr->Phase = DinoInfo[cptr->CType].flyAnim;
					}
				}
				else
				{
					cptr->Phase = DinoInfo[cptr->CType].takeoffAnim;
					if (cptr->notFlushed == false)
					{
						ActivateCharacterFx(cptr);
					}
					else
					{
						cptr->notFlushed = false;
					}

					cptr->gamma = 0;
					cptr->beta = 0;
					cptr->bend = 0;//?
				}


			}
			else {
				if (cptr->gliding == true)
				{
					cptr->Phase = DinoInfo[cptr->CType].glideAnim;
				}
				else if (cptr->Phase != DinoInfo[cptr->CType].landAnim)
				{
					if (wy >= swimLevel) cptr->Phase = DinoInfo[cptr->CType].swimAnim;
					else cptr->Phase = DinoInfo[cptr->CType].walkAnim;

				}
			}

			if (cptr->currentIdleGroup >= 0 || cptr->currentIdle2Group >= 0) {
						if (rRand(24) > 23)
						{
							cptr->State = 1;
							SetNewTargetPlace_Icth(cptr, 2048.f);
							cptr->AfraidTime = (50 + rRand(8)) * 1024;
							cptr->notFlushed = true;
							NewPhase = true;
							goto TBEGIN;
						}
			}

		}



	}

	if (cptr->gliding == true) {
		if (cptr->pos.y <= GetLandUpH(cptr->pos.x, cptr->pos.z) + 236)
		{
			cptr->gliding = false;
			cptr->Phase = DinoInfo[cptr->CType].landAnim;
			NewPhase = true;
			goto TBEGIN;
		}
	}

	if (cptr->Phase == DinoInfo[cptr->CType].landAnim) {
		if (cptr->pos.y <= GetLandUpH(cptr->pos.x, cptr->pos.z) + 15)
		{
			if (cptr->StateF & csONWATER)
			{
				cptr->Phase = DinoInfo[cptr->CType].shakeWaterAnim;
			}
			else
			{
				cptr->Phase = DinoInfo[cptr->CType].shakeLandAnim;
			}
			//TODO Set beta/gamma and such on land? - might be better to set it further down?
		}
		else if (cptr->pos.y > GetLandUpH(cptr->pos.x, cptr->pos.z) + 256)
		{
			cptr->gliding = true;
			NewPhase = true;
			goto TBEGIN;
		}
	}

	
	if (wy >= swimLevel)
	{
		if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) {
			NewPhase = true;
			goto TBEGIN;
		}

		if (cptr->currentIdleGroup >= 0) {
					NewPhase = true;
					cptr->currentIdleGroup = -1;
					goto TBEGIN;
		}
	}


	if (!(wy >= swimLevel))
	{
		if (cptr->Phase == DinoInfo[cptr->CType].swimAnim) {
			NewPhase = true;
			goto TBEGIN;
		}

		if (cptr->currentIdle2Group >= 0) {
					NewPhase = true;
					cptr->currentIdle2Group = -1;
					goto TBEGIN;
		}
	}
	


	//LAST


	if (NewPhase)
	{

		if (cptr->Phase == DinoInfo[cptr->CType].walkAnim || cptr->currentIdleGroup >= 0)
		{
			if (cptr->shakeTime < 9)
			{
				cptr->shakeTime = cptr->shakeTime + 1;
			}

			if (cptr->shakeTime == 8)
			{
				cptr->Phase = DinoInfo[cptr->CType].shakeLandAnim;
			}
		}
		else
		{
			cptr->shakeTime = 0;
		}
	}



	//============================================//

	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim || cptr->Phase == DinoInfo[cptr->CType].glideAnim
		|| cptr->Phase == DinoInfo[cptr->CType].takeoffAnim || cptr->Phase == DinoInfo[cptr->CType].landAnim) {
		cptr->tgalpha = CorrectedAlpha(FindVectorAlpha(targetdx, targetdz), cptr->alpha);//FindVectorAlpha(targetdx, targetdz);
	}
	else
	{

		if (cptr->NoFindCnt) cptr->NoFindCnt--;
		else
		{
			cptr->tgalpha = CorrectedAlpha(FindVectorAlpha(targetdx, targetdz), cptr->alpha);//FindVectorAlpha(targetdx, targetdz);
			if (cptr->AfraidTime)
			{
				cptr->tgalpha += static_cast<float>(sin(RealTime / 1024.f)) / 3.f;
				if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
				if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;
			}
		}


		LookForAWay(cptr, false, true);
		if (cptr->NoWayCnt > 12)
		{
			cptr->NoWayCnt = 0;
			cptr->NoFindCnt = 32 + rRand(60);
		}

	}

	if (cptr->tgalpha < 0) cptr->tgalpha += 2 * pi;
	if (cptr->tgalpha > 2 * pi) cptr->tgalpha -= 2 * pi;

ENDPSELECT:

	//====== process phase changing ===========//

	if ((_Phase != cptr->Phase) || NewPhase)
	{
		if (cptr->Phase == DinoInfo[cptr->CType].flyAnim || cptr->Phase == DinoInfo[cptr->CType].glideAnim)
		{

			if ((rand() & 1023) > 880)
			{
				ActivateCharacterFx(cptr);
			}
		}
		else if (cptr->Phase != DinoInfo[cptr->CType].takeoffAnim)
		{
			ActivateCharacterFx(cptr);
		}

	}

	if (_Phase != cptr->Phase)
	{

		if((_Phase == DinoInfo[cptr->CType].walkAnim || _Phase == DinoInfo[cptr->CType].swimAnim || _Phase == DinoInfo[cptr->CType].flyAnim
			|| _Phase == DinoInfo[cptr->CType].glideAnim || _Phase == DinoInfo[cptr->CType].landAnim || _Phase == DinoInfo[cptr->CType].takeoffAnim)
			&&
			(cptr->Phase == DinoInfo[cptr->CType].walkAnim || cptr->Phase == DinoInfo[cptr->CType].swimAnim || cptr->Phase == DinoInfo[cptr->CType].flyAnim
				|| cptr->Phase == DinoInfo[cptr->CType].glideAnim || cptr->Phase == DinoInfo[cptr->CType].landAnim || cptr->Phase == DinoInfo[cptr->CType].takeoffAnim))
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

	if (cptr->currentIdleGroup >= 0) {
				goto SKIPROT;
	}

	if (cptr->Phase == DinoInfo[cptr->CType].shakeLandAnim) goto SKIPROT;

	if (drspd > 0.02)
		if (cptr->Phase == DinoInfo[cptr->CType].flyAnim || cptr->Phase == DinoInfo[cptr->CType].glideAnim
			|| cptr->Phase == DinoInfo[cptr->CType].takeoffAnim || cptr->Phase == DinoInfo[cptr->CType].landAnim)
		{
			if (cptr->tgalpha > cptr->alpha) currspeed = 0.6f + drspd * 1.2f;
			else currspeed = -0.6f - drspd * 1.2f;
		}
		else
		{
			if (cptr->tgalpha > cptr->alpha) currspeed = 0.2f + drspd * 1.0f;
			else currspeed = -0.2f - drspd * 1.0f;
		}
	else currspeed = 0;

	//if (cptr->AfraidTime) currspeed *= 1.5;
	if (dalpha > pi) currspeed *= -1;


	if (cptr->currentIdle2Group >= 0) {
				currspeed /= 1.4f;
	}

	if (cptr->Phase == DinoInfo[cptr->CType].swimAnim || cptr->Phase == DinoInfo[cptr->CType].shakeWaterAnim) currspeed /= 1.4f;

	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim || cptr->Phase == DinoInfo[cptr->CType].glideAnim
		|| cptr->Phase == DinoInfo[cptr->CType].takeoffAnim || cptr->Phase == DinoInfo[cptr->CType].landAnim)
	{
		DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 460.f);
	}
	else
	{
		DeltaFunc(cptr->rspeed, currspeed, static_cast<float>(TimeDt) / 400.f);
	}

	tgbend = drspd / 2.f;
	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim || cptr->Phase == DinoInfo[cptr->CType].glideAnim
		|| cptr->Phase == DinoInfo[cptr->CType].takeoffAnim || cptr->Phase == DinoInfo[cptr->CType].landAnim)
	{
		if (tgbend > pi / 2) tgbend = pi / 2;
	}
	else
	{
		if (tgbend > pi / 3.f) tgbend = pi / 3.f;
	}

	tgbend *= SGN(currspeed);
	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim || cptr->Phase == DinoInfo[cptr->CType].glideAnim
		|| cptr->Phase == DinoInfo[cptr->CType].takeoffAnim || cptr->Phase == DinoInfo[cptr->CType].landAnim)
	{
		if (fabs(tgbend) > fabs(cptr->bend)) DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 800.f);
		else DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 400.f);

		rspd = cptr->rspeed * TimeDt / 1024.f;
	}
	else
	{
		DeltaFunc(cptr->bend, tgbend, static_cast<float>(TimeDt) / 2000.f);

		rspd = cptr->rspeed * TimeDt / 612.f;
	}

	if (drspd < fabs(rspd)) cptr->alpha = cptr->tgalpha;
	else cptr->alpha += rspd;


	if (cptr->alpha > pi * 2) cptr->alpha -= pi * 2;
	if (cptr->alpha < 0) cptr->alpha += pi * 2;

SKIPROT:

	//========== movement ==============================//
	cptr->lookx = static_cast<float>(cos(cptr->alpha));
	cptr->lookz = static_cast<float>(sin(cptr->alpha));

	float curspeed = 0;
	
	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim) curspeed = DinoInfo[cptr->CType].flyspd;
	if (cptr->Phase == DinoInfo[cptr->CType].glideAnim) curspeed = DinoInfo[cptr->CType].gldspd;
	if (cptr->Phase == DinoInfo[cptr->CType].takeoffAnim) curspeed = DinoInfo[cptr->CType].tkfspd;
	if (cptr->Phase == DinoInfo[cptr->CType].landAnim) curspeed = DinoInfo[cptr->CType].lndspd;
	if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) curspeed = DinoInfo[cptr->CType].wlkspd;
	if (cptr->Phase == DinoInfo[cptr->CType].swimAnim) curspeed = DinoInfo[cptr->CType].swmspd;
	if (cptr->Phase == DinoInfo[cptr->CType].shakeWaterAnim) curspeed = DinoInfo[cptr->CType].swmspd;

	if (cptr->currentIdle2Group >= 0) {
				curspeed = DinoInfo[cptr->CType].swmspd;
	}

	if (drspd > pi / 2.f) curspeed *= 2.f - 2.f*drspd / pi;



	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim || cptr->Phase == DinoInfo[cptr->CType].glideAnim
		|| cptr->Phase == DinoInfo[cptr->CType].takeoffAnim || cptr->Phase == DinoInfo[cptr->CType].landAnim)
	{
		if (cptr->Phase == DinoInfo[cptr->CType].flyAnim)
			DeltaFunc(cptr->pos.y, GetLandUpH(cptr->pos.x, cptr->pos.z) + 4048, TimeDt / 6.f);

		if (cptr->Phase == DinoInfo[cptr->CType].glideAnim)
		{
			if (cptr->gliding == true)
			{
				DeltaFunc(cptr->pos.y, GetLandUpH(cptr->pos.x, cptr->pos.z), TimeDt / 8.f);
			}
			else
			{
				DeltaFunc(cptr->pos.y, GetLandUpH(cptr->pos.x, cptr->pos.z), TimeDt / 16.f);
			}
		}

		if (cptr->Phase == DinoInfo[cptr->CType].takeoffAnim)
			DeltaFunc(cptr->pos.y, GetLandUpH(cptr->pos.x, cptr->pos.z) + 4048, TimeDt / 5.f);

		if (cptr->Phase == DinoInfo[cptr->CType].landAnim)
			DeltaFunc(cptr->pos.y, GetLandUpH(cptr->pos.x, cptr->pos.z), TimeDt / 4.f);


		if (cptr->gliding == false)
		{
			if (cptr->Phase != DinoInfo[cptr->CType].landAnim && cptr->Phase != DinoInfo[cptr->CType].takeoffAnim) {
				if (cptr->pos.y < GetLandUpH(cptr->pos.x, cptr->pos.z) + 236)
					cptr->pos.y = GetLandUpH(cptr->pos.x, cptr->pos.z) + 256;
			}
			else {
				if (cptr->pos.y < GetLandUpH(cptr->pos.x, cptr->pos.z))
					cptr->pos.y = GetLandUpH(cptr->pos.x, cptr->pos.z);
			}

		}

	}



	//========== process speed =============//

	bool swimmingAnim = false;
	if (cptr->Phase == DinoInfo[cptr->CType].swimAnim || DinoInfo[cptr->CType].shakeWaterAnim) swimmingAnim = true;
	if (cptr->currentIdle2Group >= 0) {
				swimmingAnim = true;
	}

	curspeed *= cptr->scale;

	if (cptr->Phase == DinoInfo[cptr->CType].flyAnim || cptr->Phase == DinoInfo[cptr->CType].glideAnim
		|| cptr->Phase == DinoInfo[cptr->CType].takeoffAnim || cptr->Phase == DinoInfo[cptr->CType].landAnim)
	{
		DeltaFunc(cptr->vspeed, curspeed, TimeDt / 2024.f);

		cptr->pos.x += cptr->lookx * cptr->vspeed * TimeDt;
		cptr->pos.z += cptr->lookz * cptr->vspeed * TimeDt;

		
		cptr->tggamma = cptr->rspeed / 4.0f;
		if (cptr->tggamma > pi / 6.f) cptr->tggamma = pi / 6.f;
		if (cptr->tggamma < -pi / 6.f) cptr->tggamma = -pi / 6.f;
		DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 2048.f);
		
	}
	else
	{
		if (curspeed > cptr->vspeed) DeltaFunc(cptr->vspeed, curspeed, TimeDt / 1024.f);
		else DeltaFunc(cptr->vspeed, curspeed, TimeDt / 256.f);

		MoveCharacter(cptr, cptr->lookx * cptr->vspeed * TimeDt,
			cptr->lookz * cptr->vspeed * TimeDt, false, true);

		if (!swimmingAnim)
		{
			ThinkY_Beta_Gamma(cptr, 128, 64, 0.6f, 0.4f);
			if (cptr->Phase == DinoInfo[cptr->CType].walkAnim) cptr->tggamma += cptr->rspeed / 16.0f;
			else cptr->tggamma += cptr->rspeed / 10.0f;

			DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 2048.f);
		}
		else {
			cptr->gamma = 0;
		}
	}


	



	//============ Y movement =================//
	if ((wy >= swimLevel) && cptr->Phase != DinoInfo[cptr->CType].flyAnim && cptr->Phase != DinoInfo[cptr->CType].glideAnim
		&& cptr->Phase != DinoInfo[cptr->CType].takeoffAnim && cptr->Phase != DinoInfo[cptr->CType].landAnim)
	{
		cptr->pos.y = GetLandUpH(cptr->pos.x, cptr->pos.z) - (DinoInfo[cptr->CType].waterLevel * cptr->scale);
		//cptr->pos.y = GetLandUpH(cptr->pos.x, cptr->pos.z) - 20;
		cptr->beta /= 2;
		cptr->tggamma = 0;
	}

}
