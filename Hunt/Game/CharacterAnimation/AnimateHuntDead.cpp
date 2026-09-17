// AnimateHuntDead.cpp — auto-extracted from CharacterAnimation.cpp
// ==========================================================================
// Auto-generated from CharacterAnimation.cpp
// ==========================================================================

#include "Hunt.h"
#include "../CharacterInternal.h"

// Global state imported from StateDefs.cpp
extern int CurDino;
extern int NewPhase;

void AnimateHuntDead(TCharacter *cptr)
{

	//if (!cptr->FTime) ActivateCharacterFx(cptr);

	ProcessPrevPhase(cptr);
	std::int32_t NewPhase = false;
	bool loopDone = false;

	if (killerDino) {
		if (DinoInfo[killerDino->CType].killType[killerDino->killType].carryCorpse &&
			DinoInfo[killerDino->CType].Aquatic) {
			cptr->bend = killerDino->bend;
			cptr->bdepth = killerDino->bdepth;
		}
	}



	cptr->FTime += TimeDt;
	if (cptr->FTime >= cptr->pinfo->Animation[cptr->Phase].AniTime)
	{
		if (killerDino) {
			if (DinoInfo[killerDino->CType].killType[killerDino->killType].dontloop &&
				cptr->Phase == DinoInfo[killerDino->CType].killType[killerDino->killType].hunteranim) {
				loopDone = true;
			}
		}

		NewPhase = true;
		if (cptr->Phase == 2)
			cptr->FTime = cptr->pinfo->Animation[cptr->Phase].AniTime - 1;
		else
			cptr->FTime = 0;

		if (cptr->Phase == 1)
		{
			cptr->FTime = 0;
			cptr->Phase = 2;
		}

		ActivateCharacterFx(cptr);
	}

	bool notAq = false;
	if (!killerDino) {
		notAq = true;
	} else if (!DinoInfo[killerDino->CType].Aquatic) notAq = true;

	if (notAq) {
		float h = GetLandH(cptr->pos.x, cptr->pos.z);
		DeltaFunc(cptr->pos.y, h, TimeDt / 5.f);

		if (cptr->Phase == 2)
			if (cptr->pos.y > h + 3)
			{
				cptr->FTime = 0;
				//MessageBeep(0xFFFFFFFF);
			}


		if (cptr->pos.y < h + 256)
		{
			//=== beta ===//
			float blook = 256;
			float hlook = GetLandH(cptr->pos.x + cptr->lookx * blook, cptr->pos.z + cptr->lookz * blook);
			float hlook2 = GetLandH(cptr->pos.x - cptr->lookx * blook, cptr->pos.z - cptr->lookz * blook);
			DeltaFunc(cptr->beta, (hlook2 - hlook) / (blook * 3.2f), TimeDt / 1800.f);

			if (cptr->beta > 0.4f) cptr->beta = 0.4f;
			if (cptr->beta < -0.4f) cptr->beta = -0.4f;

			//=== gamma ===//
			float glook = 256;
			hlook = GetLandH(cptr->pos.x + cptr->lookz * glook, cptr->pos.z - cptr->lookx*glook);
			hlook2 = GetLandH(cptr->pos.x - cptr->lookz * glook, cptr->pos.z + cptr->lookx*glook);
			cptr->tggamma = (hlook - hlook2) / (glook * 3.2f);
			if (cptr->tggamma > 0.4f) cptr->tggamma = 0.4f;
			if (cptr->tggamma < -0.4f) cptr->tggamma = -0.4f;
			DeltaFunc(cptr->gamma, cptr->tggamma, TimeDt / 1800.f);
		}
	}
	

	if (killerDino) {

		//	if (!(GetLandUpH(killerDino->pos.x, killerDino->pos.z) - GetLandH(killerDino->pos.x, killerDino->pos.z) >
		//		DinoInfo[killerDino->CType].waterLevel * killerDino->scale))
		if (!killedwater || DinoInfo[killerDino->CType].Aquatic) //make exception for aquatic dangerous creatures duh
		{
			if (DinoInfo[killerDino->CType].killTypeCount) {
				if ((DinoInfo[killerDino->CType].killType[killerDino->killType].elevate &&
					killerDino->Phase == DinoInfo[killerDino->CType].killType[killerDino->killType].anim)
					|| DinoInfo[killerDino->CType].killType[killerDino->killType].carryCorpse
					) {

					cptr->pos = killerDino->pos;
					cptr->FTime = killerDino->FTime;
					cptr->beta = killerDino->beta;
					cptr->gamma = killerDino->gamma;
					cptr->scale = killerDino->scale;
					cptr->alpha = killerDino->alpha;
					killerDino->bend = 0;

				}

			}

		}

		if (loopDone) {
			if (DinoInfo[killerDino->CType].killType[killerDino->killType].carryCorpse
				&& (!killedwater || DinoInfo[killerDino->CType].Aquatic)) {
				cptr->Phase = DinoInfo[killerDino->CType].killType[killerDino->killType].hunterswimanim;
			}
			else {
				cptr->FTime = cptr->pinfo->Animation[cptr->Phase].AniTime - 1;
			}
		}

	}

}
