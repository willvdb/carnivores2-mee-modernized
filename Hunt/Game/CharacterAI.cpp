// ==========================================================================
// CharacterAI.cpp — Character awareness and noise-making logic
// ==========================================================================
// Extracted from Characters.cpp.

#include "Hunt.h"
#include "Game/CharacterInternal.h"

void MakeNoise(Vector3d pos, float range)
{
	for (int c = 0; c < ChCount; c++)
	{
		TCharacter *cptr = &Characters[c];
		if (!cptr->Health) continue;
		float l = VectorLength(SubVectors(cptr->pos, pos));
		float r = range * (DinoInfo[cptr->CType].HearK * 2);
		if (l > r) continue;


		if (cptr->Clone == AI_TREX) {  //===== T-Rex
			if (!cptr->State) {
				cptr->State = 2;
				cptr->awareHunter = true;
				cptr->heardShot = true;
			}
		}

		if (cptr->Clone != AI_TREX && !DinoInfo[cptr->CType].Aquatic && cptr->Clone != AI_HUNTDOG)
		{
			cptr->AfraidTime = static_cast<int>((10.f + (range - l) / 256.f)) * 1024;
			if (cptr->State == 0) {
				cptr->State = 2;
			}
			cptr->NoFindCnt = 0;

			cptr->awareHunter = true;
			cptr->heardShot = true;
		}
	}
}


void CheckAfraid()
{
	if (!MyHealth) return;
	if (g_GameMode == GameMode::TrophyMode) return;

	Vector3d ppos, plook, clook, wlook, rlook;
	ppos = PlayerPos;

	if (DEBUG || IsUnderwater() || ObservMode) return;

	plook.y = 0;
	plook.x = static_cast<float>(sin(CameraAlpha));
	plook.z = static_cast<float>(-cos(CameraAlpha));

	wlook = Wind.nv;

	float kR, kwind, klook, kstand;

	float kmask = 1.0f;
	float kskill = 1.0f;
	float kscent = 1.0f;

	if (CamoMode)  kmask *= 1.5;
	if (ScentMode) kscent *= 1.5;

	for (int c = 0; c < ChCount; c++)
	{
		TCharacter *cptr = &Characters[c];
		if (!cptr->Health) continue;
		if (!AIInfo[cptr->Clone].sniffer) continue;
		//if (cptr->AfraidTime || cptr->State == 1) continue;

		if (cptr->Clone == AI_TREX && (cptr->AfraidTime || cptr->State == 1)) continue; //here to check if hunter detected once it starts running from fear call, trex doesn't like it tho

		if (g_GameMode == GameMode::SurvivalMode) goto isAfraid;

		rlook = SubVectors(ppos, cptr->pos);
		kR = VectorLength(rlook) / 256.f / (32.f + charViewR / 2);
		NormVector(rlook, 1.0f);

		kR *= 2.5f / static_cast<float>((1.5 + OptSens / 128.f));
		if (kR > 3.0f) continue;

		clook.x = cptr->lookx;
		clook.y = 0;
		clook.z = cptr->lookz;

		MulVectorsScal(wlook, rlook, kwind);
		kwind *= Wind.speed / 10;
		MulVectorsScal(clook, rlook, klook);
		klook *= -1.f;

		if (HeadY > 180) kstand = 0.7f;
		else kstand = 1.2f;

		//============= reasons ==============//

		float kALook = kR * ((klook + 3.f) / 3.f) * kstand * kmask;
		if (klook > 0.3) kALook *= 2.0;
		if (klook > 0.8) kALook *= 2.0;
		kALook /= DinoInfo[cptr->CType].LookK;
		if (kALook < 1.0)
			if (TraceLook(cptr->pos.x, cptr->pos.y + 220, cptr->pos.z,
				PlayerX, PlayerY + HeadY / 2.f, PlayerZ)) kALook *= 1.3f;

		if (kALook < 1.0)
			if (TraceLook(cptr->pos.x, cptr->pos.y + 220, cptr->pos.z,
				PlayerX, PlayerY + HeadY, PlayerZ))   kALook = 2.0;
		kALook *= (1.f + static_cast<float>(ObjectsOnLook) / 6.f);

		/*
		  if (kR<1.0f) {
			  char t[32];
		   snprintf(t, sizeof(t),"%d", ObjectsOnLook);
		   AddMessage(t);
		   kALook = 20.f;
		  }
		  */

		float kASmell = kR * ((kwind + 2.0f) / 2.0F) * ((klook + 3.f) / 3.f) * kscent;
		if (kwind > 0) kASmell *= 2.0;
		kASmell /= DinoInfo[cptr->CType].SmellK;

		float kRes = MIN(kALook, kASmell);

		if (kRes < 1.0)
		{

			isAfraid:

			//MessageBeep(0xFFFFFFFF);
			char t[128];
			if (kALook < kASmell)
				sprintf(t, "LOOK: KR: %f  Tr: %d  K: %f", kR, ObjectsOnLook, kALook);
			else
				sprintf(t, "SMELL: KR: %f  Tr: %d  K: %f", kR, ObjectsOnLook, kASmell);
			//AddMessage(t);

			//MESSAGE REMOVED

			kRes = MIN(kRes, kR);
			cptr->AfraidTime = static_cast<int>((1.0 / (kRes + 0.1) * 10.f * 1000.f));
			if (cptr->State==0) {
				cptr->State = 2;
			}
			cptr->awareHunter = true;
			if (cptr->Clone == AI_TREX) //===== T-Rex
				if (kALook > kASmell) cptr->State = 3;
			cptr->NoFindCnt = 0;
		}
	}
}