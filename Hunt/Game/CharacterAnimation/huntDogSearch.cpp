// huntDogSearch.cpp — auto-extracted from CharacterAnimation.cpp
// ==========================================================================
// Auto-generated from CharacterAnimation.cpp
// ==========================================================================

#include "Hunt.h"
#include "../CharacterInternal.h"

// Global state imported from StateDefs.cpp
extern int CurDino;
extern int NewPhase;

std::uint8_t huntDogSearch(TCharacter *cptr)
{
	bool preyFound = false;
	Vector3d preyPos;
	float preyDist;
	int preyNo;

	//if (!MyHealth) return false;
	if (g_GameMode == GameMode::TrophyMode) return false;

	float kR, kwind, klook, kstand;

	float kmask = 1.0f;
	float kscent = 1.5f;

	for (int c = 0; c < ChCount; c++)
	{
		TCharacter *dino = &Characters[c];

		Vector3d ppos, plook, clook, wlook, rlook;
		ppos = dino->pos;

		wlook = Wind.nv;

		plook.y = 0;
		plook.x = static_cast<float>(sin(dino->alpha));
		plook.z = static_cast<float>(-cos(dino->alpha));

		if (!dino->Health) continue;
		if (!DinoInfo[dino->CType].dogSmell) continue;

		rlook = SubVectors(dino->pos, cptr->pos);
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
				dino->pos.x, dino->pos.y, dino->pos.z)) kALook *= 1.3f;

		if (kALook < 1.0)
			if (TraceLook(cptr->pos.x, cptr->pos.y + 220, cptr->pos.z,
				dino->pos.x, dino->pos.y, dino->pos.z))   kALook = 2.0;
		kALook *= (1.f + static_cast<float>(ObjectsOnLook) / 6.f);

		float kASmell = kR * ((kwind + 2.0f) / 2.0F) * ((klook + 3.f) / 3.f) * kscent;
		if (kwind > 0) kASmell *= 2.0;
		kASmell /= DinoInfo[cptr->CType].SmellK;

		float kRes = MIN(kALook, kASmell);

		if (kRes < 1.0)
		{
			kRes = MIN(kRes, kR);

			if (preyFound) {
				float dx = dino->pos.x - cptr->pos.x;
				float dz = dino->pos.z - cptr->pos.z;
				float tempDist = static_cast<float>(sqrt(dx * dx + dz * dz));
				if (tempDist < preyDist) {
					preyDist = tempDist;
					preyPos = dino->pos;
					preyNo = c;
				}
			} else {
				float dx = dino->pos.x - cptr->pos.x;
				float dz = dino->pos.z - cptr->pos.z;
				preyDist = static_cast<float>(sqrt(dx * dx + dz * dz));
				preyPos = dino->pos;
				preyNo = c;
				preyFound = true;
			}

		}
	}

	if (preyFound) {
		cptr->tgx = preyPos.x;
		cptr->tgz = preyPos.z;
		cptr->tgtime = 0;
		cptr->dogPrey = preyNo;
		return true;
	}

	return false;
}
