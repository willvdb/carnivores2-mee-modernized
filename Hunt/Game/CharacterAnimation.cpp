// ==========================================================================
// CharacterAnimation.cpp — dispatcher for per-dino animation files
// Auto-generated from split_animations.py
// ==========================================================================

#include "Hunt.h"
#include "CharacterInternal.h"

// Forward declaration from AnimateHuntDead.cpp
void AnimateHuntDead(TCharacter *cptr);

// Forward declaration from AnimateDeadCommon.cpp
void AnimateDeadCommon(TCharacter *cptr);

// Forward declaration from AnimateTitan.cpp
void AnimateTitan(TCharacter *cptr);

// Forward declaration from AnimatePoacher.cpp
void AnimatePoacher(TCharacter *cptr);

// Forward declaration from AnimateHuntable.cpp
void AnimateHuntable(TCharacter *cptr);

// Forward declaration from AnimateMicro.cpp
void AnimateMicro(TCharacter *cptr);

// Forward declaration from huntDogSearch.cpp
std::uint8_t huntDogSearch(TCharacter *cptr);

// Forward declaration from AnimateHuntdog.cpp
void AnimateHuntdog(TCharacter *cptr);

// Forward declaration from AnimateTRex.cpp
void AnimateTRex(TCharacter *cptr);

// Forward declaration from AnimateMClientCharacter.cpp
void AnimateMClientCharacter(TCharacter *cptr);

// Forward declaration from AnimateClassicAmbient.cpp
void AnimateClassicAmbient(TCharacter *cptr);

// Forward declaration from AnimateFish.cpp
void AnimateFish(TCharacter *cptr);

// Forward declaration from AnimateIcth.cpp
void AnimateIcth(TCharacter *cptr);

// Forward declaration from AnimateDeadFish.cpp
void AnimateDeadFish(TCharacter *cptr);

// Forward declaration from AnimateIcthDead.cpp
void AnimateIcthDead(TCharacter *cptr);

// Forward declaration from AnimateBrahi.cpp
void AnimateBrahi(TCharacter *cptr);

// Forward declaration from AnimateBrahiOld.cpp
void AnimateBrahiOld(TCharacter *cptr);

// Forward declaration from AnimateDimor.cpp
void AnimateDimor(TCharacter *cptr);

void AnimateMHunters() {

	//loop through hunters

	for (int c = 0; c < 1; c++) {//temp 1 player

		Vector3d *pos = &MPlayers[c].pos;

		if (mGunShot[c] != -1) {
			int weapon = mGunShot[c];
			mGunShot[c] = -1;
			if (WeapInfo[weapon].MGSSound) {
				TSFX *shotFx = &fxGunShot[WeapInfo[weapon].SFXIndex];
				AddVoice3d(shotFx->length, shotFx->lpData.data(), pos->x, pos->y, pos->z);//TODO XYZ NEEDS TO BE PLAYER -> SOUND VECTOR
			}
			MakeNoise(*pos, ctViewR * 200 * WeapInfo[weapon].Loud);
		}

		if (mHunterCall[c] != -1) {
			int targetCreature = mHunterCall[c];
			mHunterCall[c] = -1;
			int callType = mHunterCallType[c];
			mHunterCallType[c] = -1;
			TSFX *callFx = &fxCall[targetCreature][callType];
			AddVoice3d(callFx->length, callFx->lpData.data(), pos->x, pos->y, pos->z);
		}

	}
	
}

void AnimateCharacters()
{
	//if (!RunMode) return;
	TCharacter *cptr;

	HitBox.pos.x = PlayerX;
	HitBox.pos.y = PlayerY;
	HitBox.pos.z = PlayerZ;
	HitBox.alpha = PlayerAlpha;

	if (g_GameMode == GameMode::TrophyMode) {

		for (CurDino = 0; CurDino < ChCount; CurDino++)
		{

			//LandingList.list[DinoInfo[Characters[ChCount].CType].trophyType[DinoInfo[Characters[ChCount].CType].tCounter].trophyPos].x

			cptr = &Characters[CurDino];

			if (cptr->animateTrophy) {
				cptr->FTime += TimeDt;
				if (cptr->FTime >= cptr->pinfo->Animation[cptr->Phase].AniTime)
				{
					cptr->FTime %= cptr->pinfo->Animation[cptr->Phase].AniTime;
				}
			}
		}

		return;
	}

	if (Multiplayer && !Host) {
		
		for (CurDino = 0; CurDino < 6; CurDino++)
		{
			cptr = &Characters[CurDino];

			AnimateMClientCharacter(cptr);
		}
		

		return;
	}

	if (g_GameMode == GameMode::SurvivalMode) {
		bool waveOver = true;
		for (CurDino = 0; CurDino < ChCount; CurDino++) {
			if (Characters[CurDino].Health) waveOver = false;
		}
		if (waveOver) {
			WaveNoteTime = 2000;
			PlaceCharactersSurvival();
		}
	}


	//packs
	for (int packN = 0; packN < PackCount; packN++) {

		Packs[packN]._alert = Packs[packN].alert;
		Packs[packN]._attack = Packs[packN].attack;
		Packs[packN].alert = false;
		Packs[packN].attack = false;

	}

	TrophyDisplay = false;

	for (CurDino = 0; CurDino < ChCount; CurDino++)
	{
		cptr = &Characters[CurDino];
		if (cptr->StateF == 0xFF) continue;
		cptr->tgtime += TimeDt;

		// tracker bullets
		if (cptr->RTime && WeapInfo[cptr->tracker].radarTime) {
			cptr->RTime -= TimeDt;
			if (cptr->RTime < 0) {
				cptr->RTime = 0;
				cptr->tracker = -1;
			}
		}


		// replace pack leader
		if (cptr->Health && cptr->packId >= 0) {
			if (!Packs[cptr->packId].leader->Health) Packs[cptr->packId].leader = cptr;
		}

		if (cptr->tgtime > 30 * 1000) {

			if (cptr->Clone == AI_BRACH || cptr->Clone == AI_BRACHDANGER || cptr->Clone == AI_LANDBRACH) SetNewTargetPlace_Brahi(cptr, 2048.f);
			else if (cptr->Clone == AI_MOSA) SetNewTargetPlaceFish(cptr, 5048.f);
			else if (cptr->Clone == AI_FISH) SetNewTargetPlaceFish(cptr, 1024.f);
			else SetNewTargetPlace(cptr, 2048);

		}

		if (cptr->tgtime > 50 * 1000 && cptr->Clone == AI_ICTH) {
			if (cptr->Phase != DinoInfo[cptr->CType].flyAnim &&
				cptr->Phase != DinoInfo[cptr->CType].glideAnim &&
				cptr->Phase != DinoInfo[cptr->CType].takeoffAnim &&
				cptr->Phase != DinoInfo[cptr->CType].landAnim)
			{
				cptr->State = 2;
				cptr->AfraidTime = (50 + rRand(8)) * 1024;
				cptr->notFlushed = true;
			}
			else {
				SetNewTargetPlace_Icth(cptr, 2048);
			}
		}



		if (GetLandUpH(cptr->pos.x, cptr->pos.z) == GetLandH(cptr->pos.x, cptr->pos.z))
		  if (cptr->Health)
			if (cptr->BloodTTime)
			{
				cptr->BloodTTime -= TimeDt;
				if (cptr->BloodTTime < 0) cptr->BloodTTime = 0;

				float k = (20000.f + cptr->BloodTTime) / 90000.f;
				if (k > 1.5) k = 1.5;
				cptr->BloodTime += static_cast<int>((static_cast<float>(TimeDt) * k));
				if (cptr->BloodTime > 600)
				{
					cptr->BloodTime = rRand(228);
					AddBloodTrail(cptr);
					if (rRand(128) > 96) AddBloodTrail(cptr);
				}
			}

		if (cptr->AfraidTime <= 0) {
			cptr->awareHunter = false;
			cptr->heardShot = false;
		}

		

		//disp ship info
		if (!cptr->Health && DinoInfo[cptr->CType].trophy && g_GameMode != GameMode::SurvivalMode) {
			if (fabs(VectorLength(SubVectors(PlayerPos, cptr->pos))) < DinoInfo[cptr->CType].Radius) {
				TrophyDisplayBody.ctype = cptr->CType;
				TrophyDisplayBody.scale = cptr->scale;
				TrophyDisplayBody.weapon = CurrentWeapon;
				TrophyDisplayBody.score = cptr->tempScore;
				TrophyDisplayBody.phase = (RealTime & 3);
				TrophyDisplayBody.time = cptr->tempTime;
				TrophyDisplayBody.date = cptr->tempDate;
				TrophyDisplayBody.range = cptr->tempRange;
				TrophyDisplay = true;
				TrophyDisplayC = CurDino;
			}
		}

		switch (cptr->Clone)
		{
		case AI_MOSA:
		case AI_FISH:
			if (cptr->Health) AnimateFish(cptr);
			else AnimateDeadFish(cptr);
			break;
		case AI_BRACH:
			if (cptr->Health) AnimateBrahiOld(cptr);
			else AnimateDeadCommon(cptr);
			break;
		case AI_BRACHDANGER:
		case AI_LANDBRACH:
			if (cptr->Health) AnimateBrahi(cptr);
			else AnimateDeadCommon(cptr);
			break;
		case AI_ICTH:
			if (cptr->Health) AnimateIcth(cptr);
			else AnimateIcthDead(cptr);
			break;
		case AI_MOSH:
		case AI_PIG:
		case AI_GALL:
		case AI_DIMET:
			if (cptr->Health) AnimateClassicAmbient(cptr);
			else AnimateDeadCommon(cptr);
			break;
		case AI_DIMOR:
		case AI_PTERA:
			if (cptr->Health) AnimateDimor(cptr);
			else AnimateIcthDead(cptr);
			break;
		case AI_HUNTDOG:
			//	HUNTDOG TEMPP DISABLED
			
			break;

		case AI_POACHER:
			// TEMP DISABLED
			//if (cptr->Health) AnimatePoacher(cptr);
			//else AnimateDeadCommon(cptr);
			break;

		case AI_PARA:
		case AI_ANKY:
		case AI_PACH:
		case AI_STEGO:
		case AI_ALLO:
		case AI_CHASM:
		case AI_VELO:
		case AI_SPINO:
		case AI_CERAT:
		case AI_BRONT:
		case AI_HOG:
		case AI_WOLF:
		case AI_RHINO:
		case AI_DEER:
		case AI_SMILO:
		case AI_MAMM:
		case AI_BEAR:
			if (cptr->Health) AnimateHuntable(cptr);
			else AnimateDeadCommon(cptr);
			break;
		case AI_TREX:
			if (cptr->Health) AnimateTRex(cptr);
			else AnimateDeadCommon(cptr);
			break;
		case AI_TITAN:
			//TEMP DISABLED
			//if (cptr->Health) AnimateTitan(cptr);
			//else AnimateIcthDead(cptr);
			break;
		case AI_MICRO:
			//TEMP DISABLED
			//if (cptr->Health) AnimateMicro(cptr);
			//else AnimateIcthDead(cptr);
			break;
		case 0:
			AnimateHuntDead(cptr);
			break;
		}

	}
}

