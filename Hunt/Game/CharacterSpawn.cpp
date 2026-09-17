// ==========================================================================
// CharacterSpawn.cpp — Character placement and spawning logic
// ==========================================================================
// Extracted from Characters.cpp.

#include "Hunt.h"
#include "Platform/Platform.h"
#include "Game/CharacterInternal.h"

void PlaceTrophy()
{
	ChCount = 0;
	PrintLog("Placing Trophies...");

	for (int c = 0; c < TROPHY2_COUNT; c++)
	{

		//MessageBox(hwndMain, c.tostring(), "boring", IDOK);

		if (!TrophyRoom2.Body[c].ctype) continue;

		// A body slot can reference a dino that no longer exists in this
		// install (stale save, roster changed since the kill). Mounting it
		// would index DinoInfo and the model files out of range, so skip
		// it loudly instead of dying quietly.
		if (TrophyRoom2.Body[c].ctype < 0 || TrophyRoom2.Body[c].ctype >= TotalC) {
			char msg[128];
			snprintf(msg, sizeof(msg),
				"Placing Trophies: slot %d has invalid ctype %d (roster holds %d) - skipped.\n",
				c, TrophyRoom2.Body[c].ctype, TotalC);
			PrintLog(msg);
			continue;
		}

			//do this in addshiptask too

		Characters[ChCount].CType = TrophyRoom2.Body[c].ctype;
//		Characters[ChCount].tropIndex = c;

//		if (DinoInfo[Characters[ChCount].CType].tCounter >= DinoInfo[TrophyIndex[TrophyRoom2.Body[c].ctype]].trophyTypeCount) continue;
		

		//Characters[ChCount].CType = TrophyRoom.Body[c].ctype;

		/*
		if (c < 6) Characters[ChCount].alpha = pi / 2;
		else if (c < 12) Characters[ChCount].alpha = pi;
		else if (c < 18) Characters[ChCount].alpha = pi * 3 / 2;
		else			*/



		ResetCharacter(&Characters[ChCount]);

		
		Characters[ChCount].State = c;
		Characters[ChCount].scale = TrophyRoom2.Body[c].scale;
		
		float scaleDif = Characters[ChCount].scale - (DinoInfo[Characters[ChCount].CType].Scale0 / 1000.f);
		if (scaleDif != 0) {
			if (DinoInfo[Characters[ChCount].CType].ScaleA != 0) scaleDif /= (DinoInfo[Characters[ChCount].CType].ScaleA / 1000.f);
			else scaleDif = 0;
		}
		int scaleDifx = static_cast<int>((scaleDif * trophyType[c].xoffsetScale));
		int scaleDifz = static_cast<int>((scaleDif * trophyType[c].zoffsetScale));
		int scaleDify = static_cast<int>((scaleDif * trophyType[c].yoffsetScale));

		Characters[ChCount].pos.x = LandingList.list[trophyType[c].trophyPos].x
			* 256 + 128 + trophyType[c].xoffset +scaleDifx;
		Characters[ChCount].pos.z = LandingList.list[trophyType[c].trophyPos].y
			* 256 + 128 + trophyType[c].zoffset +scaleDifz;

		Characters[ChCount].Phase = trophyType[c].anim;
		Characters[ChCount].PrevPhase = Characters[ChCount].Phase;


		Characters[ChCount].pos.y = GetLandH(Characters[ChCount].pos.x,
			Characters[ChCount].pos.z);

		Characters[ChCount].pos.y += trophyType[c].yoffset +scaleDify;
		
		float a = static_cast<float>(trophyType[c].alpha);
		float b = static_cast<float>(trophyType[c].beta);
		float g = static_cast<float>(trophyType[c].gamma);
		Characters[ChCount].alpha = pi * 2 * a / 360.f;
		Characters[ChCount].beta = pi * 2 * b / 360.f;
		Characters[ChCount].gamma = pi * 2 * g / 360.f;

		Characters[ChCount].xdata = static_cast<int>((LandingList.list[trophyType[c].trophyPos].x
			* 256 + 128 + trophyType[c].xdata));
		Characters[ChCount].zdata = static_cast<int>((LandingList.list[trophyType[c].trophyPos].y
			* 256 + 128 + trophyType[c].zdata));
		Characters[ChCount].ydata = trophyType[c].ydata;

		Characters[ChCount].animateTrophy = trophyType[c].playAnim;

		// Mounts are static exhibits, not live animals: remove them from AI
		// and animation processing (same sentinel the dropship uses for a
		// carried dino â€” skipped by AnimateCharacters, still rendered).
		// Without this, mounts run the full hunt-AI path on setup-only
		// state and kill the room load on the first frame. Phase keeps the
		// mount's display pose (tropAnim) and FTime stays frozen at 0.
		Characters[ChCount].StateF = 0xFF;

		//DinoInfo[Characters[ChCount].CType].tCounter++;
		ChCount++;
	}
}


















void spawnPositionPackLeader() {
	TSpawnRegion *sr = &spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegion[rRand(spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegionCh - 1)];

	

	Characters[ChCount].pos.x = sr->XMin * 256
		+ abs(rRand(sr->XMax - sr->XMin) * 256);

	Characters[ChCount].pos.z = sr->YMin * 256
		+ abs(rRand(sr->YMax - sr->YMin) * 256);
}


void spawnPositionPackFollower(int leader) {
	//spawn on same spot, so the pack doesn't spawn over a cliff edge or something
	Characters[ChCount].pos.x = Characters[leader].pos.x;
	Characters[ChCount].pos.z = Characters[leader].pos.z;
}

/*
void spawnHuntable(int &tr, int leader) {

replace2:
	if (leader >= 0) {
		spawnPositionPackFollower(leader);
	}
	else { //scrap this if we use huntable regions?
		Characters[ChCount].pos.x = 512 * 256 + siRand(50 * 256) * 10;
		Characters[ChCount].pos.z = 512 * 256 + siRand(50 * 256) * 10;
	}

	Characters[ChCount].pos.y = GetLandH(Characters[ChCount].pos.x,
		Characters[ChCount].pos.z);
	tr++;
	if (tr > 10240) return;

	if (fabs(Characters[ChCount].pos.x - PlayerX) +
		fabs(Characters[ChCount].pos.z - PlayerZ) < 256 * 40)
		goto replace2;

	if (CheckPlaceCollisionP(Characters[ChCount].pos)) goto replace2;

	Characters[ChCount].tgx = Characters[ChCount].pos.x;
	Characters[ChCount].tgz = Characters[ChCount].pos.z;
	Characters[ChCount].tgtime = 0;

	ResetCharacter(&Characters[ChCount]);
	ChCount++;


}
*/

void spawnMapAmbient(int &tr, int leader, bool moveForward) {

replaceSMA:

	if (moveForward && tr < 1024) {
		Characters[ChCount].pos.x = PlayerX + siRand(10040);
		Characters[ChCount].pos.z = PlayerZ + siRand(10040);

		std::int32_t outside = true;
		for (int sr = 0; sr < spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegionCh; sr++) {
			if (Characters[ChCount].pos.x > spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegion[sr].XMin * 256 &&
				Characters[ChCount].pos.x < spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegion[sr].XMax * 256 &&
				Characters[ChCount].pos.z > spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegion[sr].YMin * 256 &&
				Characters[ChCount].pos.z < spawnGroup[Characters[ChCount].SpawnGroupType].spawnRegion[sr].YMax * 256) outside = false;
		}
		if (outside) {
			tr++;
			goto replaceSMA;
		}
		
	} else {
	
		if (leader >= 0) {
			spawnPositionPackFollower(leader);
		}
		else {
			spawnPositionPackLeader();
		}

	}

	Characters[ChCount].pos.y = GetLandH(Characters[ChCount].pos.x,
		Characters[ChCount].pos.z);
	float wy = GetLandUpH(Characters[ChCount].pos.x,
		Characters[ChCount].pos.z) - Characters[ChCount].pos.y;
	tr++;
	if (tr > 10240) return;


	int mindist = 40;
	if (g_GameMode == GameMode::SurvivalMode) {
		mindist = ctViewR + 1;
	}

	if (fabs(Characters[ChCount].pos.x - PlayerX) +
		fabs(Characters[ChCount].pos.z - PlayerZ) < 256 * mindist)
		goto replaceSMA;

	if (DinoInfo[Characters[ChCount].CType].Clone == AI_BRACH ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_BRACHDANGER ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_ICTH) {

		if (wy > 380) goto replaceSMA;
		if (wy < 220) goto replaceSMA;

	}

	if (DinoInfo[Characters[ChCount].CType].Clone == AI_FISH ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_MOSA) {
		if (wy < DinoInfo[Characters[ChCount].CType].minDepth) goto replaceSMA;
		if (wy > DinoInfo[Characters[ChCount].CType].maxDepth) goto replaceSMA;
	}



	if (spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegionCh) {
		for (int ar = 0; ar < spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegionCh; ar++) {
			if (Characters[ChCount].pos.x > spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegion[ar].XMin * 256 &&
				Characters[ChCount].pos.x < spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegion[ar].XMax * 256 &&
				Characters[ChCount].pos.z > spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegion[ar].YMin * 256 &&
				Characters[ChCount].pos.z < spawnGroup[Characters[ChCount].SpawnGroupType].avoidRegion[ar].YMax * 256) goto replaceSMA;
		}
	}

	if (!moveForward)
		if (fabs(Characters[ChCount].pos.x - PlayerX) +
			fabs(Characters[ChCount].pos.z - PlayerZ) < 256 * 40)
			goto replaceSMA;

	//note character not reset yet, cpcpAquatic not set.
	if (DinoInfo[Characters[ChCount].CType].Clone == AI_BRACH ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_BRACHDANGER ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_ICTH ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_FISH ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_MOSA) {
		if (CheckPlaceCollisionP(Characters[ChCount].pos, true)) goto replaceSMA;
	}
	else if (CheckPlaceCollisionP(Characters[ChCount].pos, false)) goto replaceSMA;


	
	

	Characters[ChCount].tgx = Characters[ChCount].pos.x;
	Characters[ChCount].tgz = Characters[ChCount].pos.z;
	Characters[ChCount].tgtime = 0;

	if (DinoInfo[Characters[ChCount].CType].Clone == AI_BRACH ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_BRACHDANGER ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_LANDBRACH ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_ICTH) {

		Characters[ChCount].spawnAlt = GetLandUpH(Characters[ChCount].pos.x,
			Characters[ChCount].pos.z);

	}

	if (DinoInfo[Characters[ChCount].CType].Clone == AI_FISH ||
		DinoInfo[Characters[ChCount].CType].Clone == AI_MOSA) {

		Characters[ChCount].depth = GetLandH(Characters[ChCount].tgx, Characters[ChCount].tgz) +
			((GetLandUpH(Characters[ChCount].tgx, Characters[ChCount].tgz) - GetLandH(Characters[ChCount].tgx, Characters[ChCount].tgz) / 2)
				);
		Characters[ChCount].tdepth = Characters[ChCount].depth;

	}

	ResetCharacter(&Characters[ChCount]);


}






//multiplayer
void PlaceMHunters() {

	//1 player test
	MPlayers[0].pos.x = PlayerX;
	MPlayers[0].pos.z = PlayerZ;
	MPlayers[0].pos.y = PlayerY;

	MPlayers[0].pinfo = &MPlayerInfo[0];

	MPlayers[0].alpha = 0;

	MPlayers[0].State = 0;
	MPlayers[0].StateF = 0;
	MPlayers[0].Phase = 0;
	MPlayers[0].FTime = 0;
	MPlayers[0].PrevPhase = 0;
	MPlayers[0].PrevPFTime = 0;
	MPlayers[0].PPMorphTime = 0;
	MPlayers[0].beta = 0;
	MPlayers[0].gamma = 0;
	MPlayers[0].tggamma = 0;
	MPlayers[0].bend = 0;
	MPlayers[0].rspeed = 0;
	MPlayers[0].AfraidTime = 0;
	MPlayers[0].BloodTTime = 0;
	MPlayers[0].BloodTime = 0;
	MPlayers[0].lookx = static_cast<float>(cos(MPlayers[0].alpha));
	MPlayers[0].lookz = static_cast<float>(sin(MPlayers[0].alpha));
	MPlayers[0].scale = 1;

	HunterCount = 1;

}




void dispSighting(int dii, int xx, int zz) {

	char buff[100];
	char loc[100];
	bool lob = false;

	int ccx = xx;
	int ccz = zz;

	int ccxd = rRand(8) ^ 3;
	int cczd = rRand(8) ^ 3;
	if ((rand() % 2)) ccxd *= -1;
	if ((rand() % 2)) cczd *= -1;

	bool foundLoc = false;
	if (rRand(4) != 1) {

		if (ccz > 400 &&
			ccx < 350) {
			sprintf(loc, "on the western approaches");
			foundLoc = true;
		}
		else if (ccz > 800 &&
			ccz < 850 &&
			ccx > 660 &&
			ccx < 820) {
			sprintf(loc, "in Fort Ciskin");
			foundLoc = true;
		}
		else if (ccz > 330 &&
			ccz < 430 &&
			ccx > 550 &&
			ccx < 650) {
			sprintf(loc, "in the desert");
			foundLoc = true;
		}
		else if (ccz > 180 &&
			ccz < 330 &&
			ccx > 450 &&
			ccx < 600) {
			sprintf(loc, "near the lake");
			foundLoc = true;
		}
		else if (ccz > 100 &&
			ccz < 160 &&
			ccx > 450 &&
			ccx < 570) {
			sprintf(loc, "in the gultch");
			foundLoc = true;
		}
		else if (ccz > 220 &&
			ccz < 300 &&
			ccx > 570 &&
			ccx < 820) {
			sprintf(loc, "in the Catacombs");
			foundLoc = true;
		}
		else if (ccz > 320 &&
			ccz < 340 &&
			ccx > 900) {
			sprintf(loc, "at the stone circle");
			foundLoc = true;
		}
		else if (ccz > 600 &&
			ccz < 680 &&
			ccx > 800) {
			sprintf(loc, "near Feltcher's Bog");
			foundLoc = true;
		}
		else if (ccz > 280 &&
			ccz < 350 &&
			ccx > 350 &&
			ccx < 550) {
			sprintf(loc, "along the twin cliffs");
			foundLoc = true;
		}
		else if (ccz > 200 &&
			ccz < 300 &&
			ccx > 250 &&
			ccx < 350) {
			sprintf(loc, "along the twin cliffs");
			foundLoc = true;
		}
		else if (ccz > 100 &&
			ccz < 250 &&
			ccx > 200 &&
			ccx < 450) {
			sprintf(loc, "along the estuary");
			foundLoc = true;
		}
		else if (ccz > 750 &&
			ccz < 830 &&
			ccx > 300 &&
			ccx < 650) {
			sprintf(loc, "along the river valley");
			foundLoc = true;
		}
		else if (ccz > 600 &&
			ccz < 700 &&
			ccx > 500 &&
			ccx < 700) {
			sprintf(loc, "near the Great Swamp");
			foundLoc = true;
		}
	}


	if (!foundLoc) {
		int locc;
		if (ccz > 683) {
			locc = 0;
		}
		else if (ccz < 341) {
			locc = 3;
		}
		else {
			locc = 6;
		}
		if (ccx > 683) {
			locc += 1;
		}
		else if (ccx < 341) {
			locc += 2;
		}
		switch (locc) {
		case 0:
			sprintf(loc, "in the South");
			break;
		case 1:
			sprintf(loc, "in the Southeast");
			break;
		case 2:
			sprintf(loc, "in the Southwest");
			break;
		case 3:
			sprintf(loc, "in the North");
			break;
		case 4:
			sprintf(loc, "in the Northeast");
			break;
		case 5:
			sprintf(loc, "in the Northwest");
			break;
		case 6:
			sprintf(loc, "in the Centre of the Island");
			break;
		case 7:
			sprintf(loc, "in the East");
			break;
		case 8:
			sprintf(loc, "in the West");
			break;
		}
	}

	sprintf(buff, "Sighting:\n%s", DinoInfo[dii].Name);
	sprintf(buff + strlen(buff), " %s", loc);
	Platform::ShowMessage("TEST", buff);

}

void PlaceCharactersSurvival()
{
	if (SurvivalWave > TrophyRoom2.survivalHighScore) TrophyRoom2.survivalHighScore = SurvivalWave;
	SaveTrophy();
	SurvivalWave++;
	if (SurvivalWave >= 27) DoHalt("Limit Exceeded");
	int tr = 0;
	int waveTotal = 1;
	int dinoCost = 1;
	for (int w = 0; w < SurvivalWave; w++) waveTotal *= 2;
	for (int d = 0; d < SurvivalIndexCh - 1; d++) dinoCost *= 3;
	int dinoIndex = SurvivalIndexCh;
	
	//char buff[100];
	//sprintf(buff, "SurvivalIndexCh %i", SurvivalIndexCh);
	//MessageBox(hwndMain, buff, "TEST", IDOK);
	//sprintf(buff, "/nSurvivalWave = %i", SurvivalWave);
	//MessageBox(hwndMain, buff, "TEST", IDOK);
	//sprintf(buff, "START-waveTotal %i", waveTotal);
	//MessageBox(hwndMain, buff, "TEST", IDOK);
	//sprintf(buff, "START-dinoCost  %i", dinoCost);
	//MessageBox(hwndMain, buff, "TEST", IDOK);
	//sprintf(buff, "START-dinoIndex %i", dinoIndex);
	//MessageBox(hwndMain, buff, "TEST", IDOK);

	while (waveTotal < dinoCost) {
		dinoCost /= 3;
		dinoIndex --;
	}
	while (dinoIndex > 0) {
		while (waveTotal >= dinoCost) {
			if (tr > 10500) DoHalt("Cannot spawn characters");
			if (ChCount > 254) DoHalt("Character limit exceeded");
			Characters[ChCount].CType = SurvivalIndex[dinoIndex];
			Characters[ChCount].SpawnGroupType = 0;
			Characters[ChCount].packId = -1;
			spawnMapAmbient(tr, -1, false);
			Characters[ChCount].State = 2;
			ChCount++;
			waveTotal -= dinoCost;
		}
		dinoCost /= 3;
		dinoIndex--;
	}
}

void PlaceCharacters()
{

	int c, tr;
	for (int i = 0; i < ChCount; i++) {
		Characters[i] = {};
	}
	ChCount = 0;

	PrintLog("Placing...");

	for (c = 10; c < 30; c++)
		if ((TargetDino & (1 << c)) > 0)
		{
			TargetCall = c;
			break;
		}

	// NEW SYSTEM

	//TODO - NO PACK HUNTING WITH MOVEFORWARD
	//TODO - HALF AMBIENT SPAWN AT NIGHT MANUALLY IN RES?


	if (!RestartMode) {

		for (int di = 0; di < DINOINFO_MAX; di++) {
			if (DinoInfo[di].packMember2Ch) {
				for (int pin = 0; pin < DinoInfo[di].packMember2Ch; pin++) {
					packType[DinoInfo[di].packMember2[pin].packGroup]
						.packMember[packType[DinoInfo[di].packMember2[pin].packGroup].packMemberCh]
						.ctype = di;
					packType[DinoInfo[di].packMember2[pin].packGroup]
						.packMember[packType[DinoInfo[di].packMember2[pin].packGroup].packMemberCh]
						.ratio = DinoInfo[di].packMember2[pin].ratio;
					packType[DinoInfo[di].packMember2[pin].packGroup].packMemberCh++;
				}
			}
		}

		for (int di = 0; di < DINOINFO_MAX; di++) {
			if (DinoInfo[di].SpawnInfoCh) {
				
				packType[packTypeCount].packMember[packType[packTypeCount].packMemberCh].ctype = di;
				packType[packTypeCount].packMember[packType[packTypeCount].packMemberCh].ratio = 1;
				packType[packTypeCount].packMax = 1;
				packType[packTypeCount].packMin = 1;
				packType[packTypeCount].packMemberCh++;
				for (int si = 0; si < DinoInfo[di].SpawnInfoCh; si++) {
					packType[packTypeCount].SpawnInfo[packType[packTypeCount].SpawnInfoCh].spawnGroup = DinoInfo[di].SpawnInfo[si].spawnGroup;
					packType[packTypeCount].SpawnInfo[packType[packTypeCount].SpawnInfoCh].spawnRatio = DinoInfo[di].SpawnInfo[si].spawnRatio;
					packType[packTypeCount].SpawnInfoCh++;
				}
				packTypeCount++;
			}
		}

		for (int p = 0; p < packTypeCount; p++) {
			if (packType[p].SpawnInfoCh){
				for (int si = 0; si < packType[p].SpawnInfoCh; si++) {
					spawnGroup[packType[p].SpawnInfo[si].spawnGroup].packIndex[spawnGroup[packType[p].SpawnInfo[si].spawnGroup].packIndexCh] = p;
					spawnGroup[packType[p].SpawnInfo[si].spawnGroup].spawnInfoIndex[spawnGroup[packType[p].SpawnInfo[si].spawnGroup].packIndexCh] = si;
					spawnGroup[packType[p].SpawnInfo[si].spawnGroup].packIndexCh++;
				}
			}
		}

	}

	for (int sg = 0; sg < TotalSpawnGroup; sg++){
	//for (TSpawnGroup sg : spawnGroup) {
		
		

		if (spawnGroup[sg].packIndexCh) {
			int spawnNo = spawnGroup[sg].SpawnMin;
			for (int i = 0; i < spawnGroup[sg].SpawnMax - spawnGroup[sg].SpawnMin; i++) {
				if (spawnGroup[sg].SpawnRate * 30000 > rRand(30000)) spawnNo++;
			}
			if (spawnGroup[sg].densityMulti != 0) {
				float m = OptDens - 128;
				m /= 128.f;
				m *= spawnGroup[sg].densityMulti;
				spawnNo += static_cast<int>(m);
				if (spawnNo < 0) spawnNo = 0;
			}

			float ratioScores[256];
			float totalRatio = 0;
			//int counter[256];
			for (c = 0; c < spawnGroup[sg].packIndexCh; c++) {
				ratioScores[c] = 0.f;
				//counter[c] = 0;
				totalRatio += packType[spawnGroup[sg].packIndex[c]].SpawnInfo[spawnGroup[sg].spawnInfoIndex[c]].spawnRatio;
				            //DinoInfo[spawnGroup[sg].dinoIndex[c]].SpawnInfo[spawnGroup[sg].spawnInfoIndex[c]].spawnRatio;
			}
			int posi = 0;
			tr = 0;

			for (c = 0; c < spawnNo; c++) {

				int packInd = -1;

				// select ctype accounting for spawn ratio
				if (spawnGroup[sg].Randomised) {
					float selector = rRand(30000);
					selector /= 30000;
					selector *= totalRatio;
					for (int ch = 0; ch < spawnGroup[sg].packIndexCh; ch++) {
						if (selector <= packType[spawnGroup[sg].packIndex[ch]].SpawnInfo[spawnGroup[sg].spawnInfoIndex[ch]].spawnRatio) {
							packInd = spawnGroup[sg].packIndex[ch];
							break;
						} else selector -= packType[spawnGroup[sg].packIndex[ch]].SpawnInfo[spawnGroup[sg].spawnInfoIndex[ch]].spawnRatio;
					}
				} else {
					while (packInd == -1) {
						int post = posi % spawnGroup[sg].packIndexCh;
						if (ratioScores[post] >= 1.f) {
							ratioScores[post] -= 1.f;
							packInd = spawnGroup[sg].packIndex[post];
						}
						else {
							ratioScores[post] += packType[spawnGroup[sg].packIndex[post]].SpawnInfo[spawnGroup[sg].spawnInfoIndex[post]].spawnRatio;
							posi++;
						}
					}
				}

				/*
				std::list<int> spawnList;
				for (int indx = 0; indx < spawnGroup[sg].dinoIndexCh; indx++) {
					for (int rat = 0; rat < DinoInfo[spawnGroup[sg].dinoIndex[indx]].SpawnInfo[spawnGroup[sg].spawnInfoIndex[indx]].spawnRatio; rat++) {
						spawnList.push_back(spawnGroup[sg].dinoIndex[indx]);
					}
				}

				auto it = spawnList.begin();
				if (spawnGroup[sg].Randomised) {	
					std::advance(it, rRand(spawnList.size() - 1));	
				} else {
					std::advance(it, c % spawnList.size());
				}
				Characters[ChCount].CType = *it;
				*/

				//DinoInfo[spawnGroup[sg].dinoIndex[indx]].SpawnInfo[spawnGroup[sg].spawnInfoIndex[indx]].spawnRatio;
				/*
				if (spawnGroup[sg].Randomised) Characters[ChCount].CType = spawnGroup[sg].dinoIndex[rRand(spawnGroup[sg].dinoIndexCh - 1)];
				else Characters[ChCount].CType = spawnGroup[sg].dinoIndex[c % spawnGroup[sg].dinoIndexCh];
				*/

				Characters[ChCount].CType = packType[packInd].packMember[0].ctype;
				Characters[ChCount].Clone = DinoInfo[Characters[ChCount].CType].Clone;
				Characters[ChCount].packDensity = packType[packInd].packDensity;
				Characters[ChCount].SpawnGroupType = sg;
				
				int leaderIndex = ChCount;
				// pack leaders
				spawnMapAmbient(tr, -1, spawnGroup[sg].moveForward);

				//pack size
				if (spawnGroup[sg].moveForward) {
					Characters[ChCount].packId = -1;
					ChCount++;
				} else {
				
					int packNo = 1;
					if (packType[packInd].packMax > 1) {//!spawnGroup[sg].moveForward
						packNo = packType[packInd].packMin;
						if (packType[packInd].packMax != packType[packInd].packMin) {
							for (int i = 0; i < packType[packInd].packMax - packType[packInd].packMin; i++) {
								if (1 == rRand(2)) packNo++;
							}
						}
					}

					ChCount++;


					//pack members
					if (packNo > 1) {
						Packs[PackCount].leader = &Characters[leaderIndex];
						Packs[PackCount].alert = false;
						Packs[PackCount].attack = false;
						Packs[PackCount]._alert = false;
						Packs[PackCount]._attack = false;
						Characters[leaderIndex].packId = PackCount;

						for (int packN = 0; packN < packNo - 1; packN++) {
							Characters[ChCount].packId = PackCount;

							Characters[ChCount].CType = packType[packInd].packMember[0].ctype; //failsafe

							//recalculate every time a member is added
							float memberRatio = 0;
							for (int pm = 0; pm < packType[packInd].packMemberCh; pm++) {
								memberRatio += packType[packInd].packMember[pm].ratio;
							}
							float memberSelector = rRand(30000);
							memberSelector /= 30000;
							memberSelector *= memberRatio;
							for (int pm = 0; pm < packType[packInd].packMemberCh; pm++) {
								if (memberSelector <= packType[packInd].packMember[pm].ratio) {
									Characters[ChCount].CType = packType[packInd].packMember[pm].ctype;
									break;
								}
								else memberSelector -= packType[packInd].packMember[pm].ratio;
							}

							//Characters[ChCount].CType = packType[packInd].packMember[rRand(packType[packInd].packMemberCh - 1)].ctype;//Characters[leaderIndex].CType;
							Characters[ChCount].SpawnGroupType = sg;
							Characters[ChCount].packDensity = Characters[leaderIndex].packDensity;
							spawnMapAmbient(tr, leaderIndex, false);
							ChCount++;
						}
						PackCount++;
					}
					else Characters[leaderIndex].packId = -1;

				}

				//ChCount++;

				if (tr > 10500) break;



			}
		}
		
	}

	//END NEW SYSTEM




	//======== lohs =========//
	/*
	int MC = 5 + OptDens / 80;
	if (OptDayNight == 2) MC /= 2;

	tr = 0;

	// NO PACKING HUNTING FOR CLASSIC AMBS
	for (c = 0; c < MC; c++)
	{
		Characters[ChCount].CType = 1 + c % 5; //rRand(3);
	replace1:
		Characters[ChCount].pos.x = PlayerX + siRand(10040);
		Characters[ChCount].pos.z = PlayerZ + siRand(10040);
		Characters[ChCount].pos.y = GetLandH(Characters[ChCount].pos.x,
			Characters[ChCount].pos.z);
		tr++;
		if (tr > 10240) break;

		if (CheckPlaceCollisionP(Characters[ChCount].pos)) goto replace1;

		Characters[ChCount].packId = -1;
		ResetCharacter(&Characters[ChCount]);

		if (Characters[ChCount].Clone == AI_DIMOR ||
			Characters[ChCount].Clone == AI_PTERA)
			Characters[ChCount].pos.y += DinoInfo[Characters[ChCount].CType].minDepth;

		Characters[ChCount].tgx = Characters[ChCount].pos.x;
		Characters[ChCount].tgz = Characters[ChCount].pos.z;
		Characters[ChCount].tgtime = 0;


		ChCount++;
	}
	*/

	/*
	//place hunting dog
	DogMode = false;
	if (DogMode) {
		tr = 0;
		Characters[ChCount].CType = AI_to_CIndex[AI_HUNTDOG];
		replacehuntDog:
		Characters[ChCount].pos.x = PlayerX + siRand(5) * 256;
		Characters[ChCount].pos.z = PlayerZ + siRand(5) * 256;
		Characters[ChCount].pos.y = GetLandH(Characters[ChCount].pos.x,
			Characters[ChCount].pos.z);
		tr++;
		if (tr < 10240 && CheckPlaceCollisionP(Characters[ChCount].pos)) goto replacehuntDog;
		Characters[ChCount].tgx = Characters[ChCount].pos.x;
		Characters[ChCount].tgz = Characters[ChCount].pos.z;
		Characters[ChCount].tgtime = 0;
		
		Characters[ChCount].packId = -1;
		ResetCharacter(&Characters[ChCount]);
		ChCount++;
	}
	*/


	//MAP AMBIENTS

	/*
	for (int i9 = 0; i9 < TotalMA; i9++) {

		int DinoInfoIndex = AI_to_CIndex[i9 + AI_FINAL + 1];

		for (int r = 0; r < DinoInfo[DinoInfoIndex].RegionCount; r++) {

			int RegionNo = DinoInfo[DinoInfoIndex].RType0[r];


			//spawn count
			int SpwnMax = Region[RegionNo].SpawnMax;
			if ( CiskMode && DinoInfo[DinoInfoIndex].Clone > 0) {
				if (Region[RegionNo].SpawnRate * 1000 > rRand(10000)){
				SpwnMax *= 10;
				char buff[100];
				snprintf(buff, "Influx:%s", DinoInfo[DinoInfoIndex].Name);
				Platform::ShowMessage("TEST", buff);
				}
				else if (Region[RegionNo].SpawnRate * 1000 > rRand(100000)) {
					SpwnMax *= 50;
					char buff[100];
					snprintf(buff, "Unprecidented Influx:%s", DinoInfo[DinoInfoIndex].Name);
					Platform::ShowMessage("TEST", buff);
				}
			}
			int spawnNo = Region[RegionNo].SpawnMin;
			for (int i = 0; i < SpwnMax - Region[RegionNo].SpawnMin; i++) { 
				if (Region[RegionNo].SpawnRate * 1000 > rRand(1000)) spawnNo++;
			}

			if (!spawnNo) {
				if (CiskMode && DinoInfo[DinoInfoIndex].Clone > 0 && Region[RegionNo].SpawnRate * 1000 > rRand(4000)) {//fake
					dispSighting(DinoInfoIndex, rRand(944) + 40, rRand(944) + 40);
				}
			}

			tr = 0;
			for (int spawnN = 0; spawnN < spawnNo; spawnN++)
			{

				int leaderIndex = ChCount;
				// pack leaders
				spawnMapAmbient(DinoInfoIndex, Region[RegionNo], tr, -1, RegionNo);

				if (CiskMode && DinoInfo[DinoInfoIndex].Clone > 0 && rRand(3) == 1) {//real
					dispSighting(DinoInfoIndex, static_cast<int>(Characters[ChCount - 1].pos.x) / 256, static_cast<int>(Characters[ChCount - 1].pos.z) / 256);
				}

				//pack size
				int packNo = 1;
				if (DinoInfo[DinoInfoIndex].packMax > 1) {
					packNo = DinoInfo[DinoInfoIndex].packMin;
					if (DinoInfo[DinoInfoIndex].packMax != DinoInfo[DinoInfoIndex].packMin) {
						for (int i = 0; i < DinoInfo[DinoInfoIndex].packMax - DinoInfo[DinoInfoIndex].packMin; i++) {
							if (1 == rRand(2)) packNo++;
						}
					}
				}

				//pack members
				if (packNo > 1) {
					Packs[PackCount].leader = &Characters[leaderIndex];
					Packs[PackCount].alert = false;
					Packs[PackCount].attack = false;
					Packs[PackCount]._alert = false;
					Packs[PackCount]._attack = false;
					Characters[leaderIndex].packId = PackCount;
					for (int packN = 0; packN < packNo - 1; packN++) {
						Characters[ChCount].packId = PackCount;
						spawnMapAmbient(DinoInfoIndex, Region[RegionNo], tr, leaderIndex, RegionNo);
					}
					PackCount++;
				}
				else Characters[leaderIndex].packId = -1;

				if (tr > 10500) break;

			}


		}


	}
	*/



	// main
	/*
	int TC = 0;
	int TDi[10];
	TDi[0] = 10;
	for (c = 10; c < 20; c++)
		if (TargetDino & (1 << c)) TDi[TC++] = c;


	MC = 8 + OptDens / 30 + rRand(6);
	tr = 0;

	//======== main =========//
	for (c = 0; c < MC; c++)
	{

		if ((c < 4) || (!TargetDino)) Characters[ChCount].CType = AI_to_CIndex[10] + rRand(6);
		else
			//if (c<10)
			Characters[ChCount].CType = AI_to_CIndex[TDi[c % (TC)]];
		//else Characters[ChCount].CType = AI_to_CIndex[ TDi[rRand(TC-1)] ];

		//Characters[ChCount].CType = AI_to_CIndex[10] + 7;//rRand(3);

		


		int DinoInfoIndex = Characters[ChCount].CType;

		int leaderIndex = ChCount;
		// pack leaders
		spawnHuntable(tr, -1);

		//pack size
		int packNo = 1;
		if (DinoInfo[DinoInfoIndex].packMax > 1) {
			packNo = DinoInfo[DinoInfoIndex].packMin;
			if(DinoInfo[DinoInfoIndex].packMax != DinoInfo[DinoInfoIndex].packMin){
				for (int i = 0; i < DinoInfo[DinoInfoIndex].packMax - DinoInfo[DinoInfoIndex].packMin; i++) {
					if (1 == rRand(2)) packNo++;
				}
			}
		}

		//pack members
		if (packNo > 1) {
			Packs[PackCount].leader = &Characters[leaderIndex];
			Packs[PackCount].alert = false;
			Packs[PackCount].attack = false;
			Packs[PackCount]._alert = false;
			Packs[PackCount]._attack = false;
			Characters[leaderIndex].packId = PackCount;
			for (int packN = 0; packN < packNo - 1; packN++) {
				Characters[ChCount].packId = PackCount;
				Characters[ChCount].CType = DinoInfoIndex;
				spawnHuntable(tr, leaderIndex);
			}
			PackCount++;
		}
		else Characters[leaderIndex].packId = -1;

		if (tr > 10500) break;

	}
	*/

	PrintLog("\n");
	DemoPoint.DemoTime = 0;
}