// ==========================================================================
// Ship.cpp
// ==========================================================================

#include "Hunt.h"

void AddWCircle(float x, float z, float scale)
{
  // Bounds guard. WCircles is a fixed-size array (WCircles[2096] in
  // GameState.h). AddWCircle is the ONLY writer that increments WCCount,
  // and it is called from AnimateFish's per-particle loop
  // (CharacterAnimation.cpp ~line 3665), which is gated by
  //   pdistSq < ((ctViewR + 20) * 256) ^ 2
  // i.e. "only create particles within player render distance". At high
  // view distance (180+) on a map with many aquatic ambients (e.g.
  // plesiosaurs wading on a beach), the activation radius becomes huge,
  // so many swimmers concurrently spawn water circles every frame.
  // Circles only expire after FTime >= 2000 (Game.cpp update loop), so
  // the spawn rate outruns the expiry rate and WCCount climbs past 2096,
  // writing out of bounds and clobbering the globals declared immediately
  // after WCircles in GameState.h (Snow, DemoPoint, killerDino, Players[],
  // PlayerPos/CameraPos, DirectDraw pointers...) -> hard crash.
  //
  // AddElementsA already guards Elements[700] the same way ("if (ElCount
  // > 697) ..."); this mirrors that protection. Dropping the newest
  // circle when full is O(1) and visually harmless: the array is only
  // reached at all in the extreme view-distance + many-swimmer case,
  // and the dropped ripples are far/short-lived anyway.
  // Derived from the array rather than repeating its size, so the guard and
  // the declaration cannot drift apart.
  static const int kWCircleCapacity =
      static_cast<int>(sizeof(WCircles) / sizeof(WCircles[0]));
  if (WCCount >= kWCircleCapacity) {
    static int hitCount = 0;
    ++hitCount;
    if (hitCount <= 20 || hitCount % 100 == 0) {
      char buf[128];
      sprintf(buf, "WARNING: WCircles hit %d cap (ctViewR=%d); water circle dropped (hit #%d)\n",
              kWCircleCapacity, ctViewR, hitCount);
      PrintLogVerbose(buf);
    }
    return;
  }

  WCircles[WCCount].pos.x = x;
  WCircles[WCCount].pos.z = z;
  WCircles[WCCount].pos.y = GetLandUpH(x, z);
  WCircles[WCCount].FTime = 0;
  WCircles[WCCount].scale = scale;
  WCCount++;
}

void AddShipTask(int cindex)
{

  TCharacter *cptr = &Characters[cindex];
  cptr->claimed = true;

  if (GetLandUpH(cptr->pos.x, cptr->pos.z) - GetLandH(cptr->pos.x, cptr->pos.z) < 100) {

    ShipTask.clist[ShipTask.tcount] = cindex;
    ShipTask.tcount++;
    AddVoicev(ShipModel.SoundFX[3].length,
              ShipModel.SoundFX[3].lpData.data(), 256);

  }

  //===== trophy =======//
  
  int t=0;
  bool foundSpareSlot = false;
  for (t = 0; t < TROPHY2_COUNT - 1; t++) {
	  bool validCType = false;
	  for (int i = 0; i < trophyType[t].ctypeCh; i++) {
		  if (trophyType[t].ctype[i] == cptr->CType) validCType = true;
	  }
	  if (!TrophyRoom2.Body[t].ctype && validCType) {
		  foundSpareSlot = true;
		  break;
	  }
  }

	  if (foundSpareSlot) {
		  TrophyBody = t;
		  TrophyRoom2.Body[t].ctype = Characters[cindex].CType; //0 is blank trophy
		  TrophyRoom2.Body[t].scale = Characters[cindex].scale;
		  TrophyRoom2.Body[t].weapon = CurrentWeapon;
		  TrophyRoom2.Body[t].score = Characters[cindex].tempScore;
		  TrophyRoom2.Body[t].phase = (RealTime & 3);
		  TrophyRoom2.Body[t].time = Characters[cindex].tempTime;
		  TrophyRoom2.Body[t].date = Characters[cindex].tempDate;
		  TrophyRoom2.Body[t].range = Characters[cindex].tempRange;
		  PrintLogVerbose("Trophy added: ");
		  PrintLogVerbose(DinoInfo[Characters[cindex].CType].Name);
		  PrintLogVerbose("\n");
	  
  }
}

void AddShipSupply(float tx, float tz) {

	if (SShip.State != 0) return;
	if (GetLandUpH(PlayerX, PlayerZ) > GetLandH(PlayerX, PlayerZ)) return;

	AddVoicev(SShipModel.SoundFX[1].length,
		SShipModel.SoundFX[1].lpData.data(), 256);

	SShip.DeltaY = 7048.f;

	// Spawn beyond visibility: the old fixed 90-cell offset predates the
	// increased view distance, so the ship visibly popped in mid-air.
	// (Also fixes a copy/paste slip that tested Ship.pos for SShip.pos.)
	int spawnCells = ctViewR + 30;
	if (spawnCells < 90) spawnCells = 90;
	SShip.pos.x = PlayerX - spawnCells * 256;
	if (SShip.pos.x < 256) SShip.pos.x = PlayerX + spawnCells * 256;
	SShip.pos.z = PlayerZ - spawnCells * 256;
	if (SShip.pos.z < 256) SShip.pos.z = PlayerZ + spawnCells * 256;
	SShip.pos.y = GetLandUpH(SShip.pos.x, SShip.pos.z) + SShip.DeltaY + 1024;

	SShip.tgpos.x = tx;
	SShip.tgpos.z = tz;
	SShip.tgpos.y = GetLandUpH(SShip.tgpos.x, SShip.tgpos.z) + 5048.f;
	SShip.State = 1;

	SShip.retpos = SShip.pos;
	SShip.retpos.y += SShip.DeltaY;
	SShip.FTime = 0;
}

void InitShip(int cindex){
  TCharacter *cptr = &Characters[cindex];

  Ship.DeltaY = 2048.f + DinoInfo[cptr->CType].ShDelta * cptr->scale;

  // Same beyond-visibility rule as the resupply ship (see AddShipSupply).
  int spawnCells = ctViewR + 30;
  if (spawnCells < 90) spawnCells = 90;
  Ship.pos.x = PlayerX - spawnCells*256;
  if (Ship.pos.x < 256) Ship.pos.x = PlayerX + spawnCells*256;
  Ship.pos.z = PlayerZ - spawnCells*256;
  if (Ship.pos.z < 256) Ship.pos.z = PlayerZ + spawnCells*256;
  Ship.pos.y = GetLandUpH(Ship.pos.x, Ship.pos.z)  + Ship.DeltaY + 1024;

  Ship.tgpos.x = cptr->pos.x;
  Ship.tgpos.z = cptr->pos.z;
  Ship.tgpos.y = GetLandUpH(Ship.tgpos.x, Ship.tgpos.z)  + Ship.DeltaY;
  Ship.State = 0;

  Ship.retpos = Ship.pos;
  Ship.cindex = cindex;
  Ship.FTime = 0;
}