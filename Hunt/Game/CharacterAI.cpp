// ==========================================================================
// CharacterAI.cpp — Character awareness and noise-making logic
// ==========================================================================
// Extracted from Characters.cpp.

#include "Hunt.h"
#include "Game/CharacterAwareness.h"
#include "Game/CharacterInternal.h"

void MakeNoise(Vector3d pos, float range)
{
	THunterStimulus stimulus;
	stimulus.kind = HunterStimulusKind::GunshotHeard;
	stimulus.position = pos;
	stimulus.soundRange = range;
	for (int c = 0; c < ChCount; c++)
		ApplyHunterStimulus(Characters[c], stimulus);
}

void ReactToHunterCall(Vector3d pos, int callIndex)
{
	if (callIndex < 0 || callIndex >= 64) return;

	THunterStimulus stimulus;
	stimulus.kind = HunterStimulusKind::HunterCall;
	stimulus.position = pos;
	stimulus.callIndex = callIndex;
	for (int c = 0; c < ChCount; c++)
		ApplyHunterStimulus(Characters[c], stimulus);
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

	float kR = 1.0f, kwind = 0.0f, klook = 0.0f, kstand = 1.0f;

	float kmask = 1.0f;
	float kskill = 1.0f;
	float kscent = 1.0f;

	if (CamoMode)  kmask *= 1.5;
	if (ScentMode) kscent *= 1.5;

	for (int c = 0; c < ChCount; c++)
	{
		TCharacter *cptr = &Characters[c];
		// Trophy mounts use State as their persistent exhibit slot. The room
		// normally runs in GameMode::Normal, so CheckAfraid still executes;
		// allowing a mount through perception can rewrite slot 0 to AI state 2
		// and disconnect the first plaque from TrophyRoom2.Body[0].
		if (cptr->StateF == 0xFF) continue;
		if (!cptr->Health) continue;
		if (!GetHunterCapabilities(*cptr).sniffs) continue;
		//if (cptr->AfraidTime || cptr->State == 1) continue;

		// Perception keeps running for every creature, including the T-Rex
		// mid-pursuit: a successful check refreshes the tracking lock, which is
		// the documented "expires unless sight or scent keeps working" rule.
		// The notice animation is separately gated below so the refresh never
		// replays the look/roar sequence of an active chase.

		// The neutral values cover the survival path, which jumps straight to
		// the reaction with no perception math (waves always detect the
		// hunter). They also keep the goto from skipping the initializers of
		// the locals declared below it.
		float kALook = 1.0f;
		float kASmell = 1.0f;
		float kRes = 1.0f;

		if (g_GameMode == GameMode::SurvivalMode) goto isAfraid;

		rlook = SubVectors(ppos, cptr->pos);
		kR = VectorLength(rlook) / 256.f
			/ (32.f + GameplayViewRadiusCells(ctViewR) / 2.f);
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

		kALook = kR * ((klook + 3.f) / 3.f) * kstand * kmask;
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

		kASmell = kR * ((kwind + 2.0f) / 2.0F) * ((klook + 3.f) / 3.f) * kscent;
		if (kwind > 0) kASmell *= 2.0;
		kASmell /= DinoInfo[cptr->CType].SmellK;

		kRes = MIN(kALook, kASmell);

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
			const HunterAwarenessState priorAwareness = cptr->hunterAwareness;
			cptr->AfraidTime = static_cast<int>((1.0 / (kRes + 0.1) * 10.f * 1000.f));
			if (cptr->State==0) {
				cptr->State = 2;
			}
			cptr->hunterAwareness = HunterAwarenessState::TrackingHunter;
			// Sight and scent acquisitions are awareness events as well: the
			// trace shows the spark, not only the reaction. Re-perception
			// refreshes stay silent; a fresh acquisition or promotion logs.
			if (priorAwareness != HunterAwarenessState::TrackingHunter) {
				const float senseDx = cptr->pos.x - PlayerX;
				const float senseDz = cptr->pos.z - PlayerZ;
				TraceHunterEvent(cptr, kALook < kASmell ? "sight" : "scent",
					static_cast<float>(sqrt(senseDx * senseDx + senseDz * senseDz)),
					0.0f, 0.0f);
			}
			// A T-Rex with any active reaction keeps charging. Its awareness
			// still refreshes or upgrades above; only a brand-new detection
			// plays the look/smell notice.
			if (cptr->Clone == AI_TREX //===== T-Rex
				&& ShouldScheduleNoticeAnimation(priorAwareness))
				if (kALook > kASmell) cptr->State = 3;
			cptr->NoFindCnt = 0;
		}
	}
}