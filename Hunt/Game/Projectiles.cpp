// Projectiles.cpp — auto-extracted from Game.cpp
// ==========================================================================
// Auto-extracted from Game.cpp
// ==========================================================================

#include "Hunt.h"
#include "Core/ProjectileMath.h"

std::uint32_t ColorSum(std::uint32_t C1, std::uint32_t C2)
{
  std::uint32_t R,G,B;
  R = MIN(255, ((C1>> 0) & 0xFF) + ((C2>> 0) & 0xFF));
  G = MIN(255, ((C1>> 8) & 0xFF) + ((C2>> 8) & 0xFF));
  B = MIN(255, ((C1>>16) & 0xFF) + ((C2>>16) & 0xFF));
  return R + (G<<8) + (B<<16);
}


#define partBlood   1
#define partWater   2
#define partGround  3
#define partBubble  4
void AddElements(float x, float y, float z, int etype, int cnt)
{
	AddElementsA(x, y, z, etype, cnt, cnt, false, 0);
}
void AddElementsA(float x, float y, float z, int etype, int cnt, int mag, bool angled, float alph)
{
  if (ElCount > 697)
  {
    memcpy(&Elements[0], &Elements[1], (ElCount-1) * sizeof(TElements));
    ElCount--;
  }

  Elements[ElCount].EDone  = 0;
  Elements[ElCount].Type = etype;
  Elements[ElCount].ECount = MIN(30, cnt);
  int c;

  switch (etype)
  {
  case partBlood:
#ifdef _d3d
    //Elements[ElCount].RGBA = 0xE0600000;
    //Elements[ElCount].RGBA2= 0x20300000;
	  Elements[ElCount].RGBA = 0xE0000000 +
		  (DinoInfo[Characters[ShotDino].CType].bloodRed << 16) +
		  (DinoInfo[Characters[ShotDino].CType].bloodGreen << 8) +
		  DinoInfo[Characters[ShotDino].CType].bloodBlue;
	  Elements[ElCount].RGBA2 = 0x20000000 +
		  (DinoInfo[Characters[ShotDino].CType].bloodRed / 2 << 16) +
		  (DinoInfo[Characters[ShotDino].CType].bloodGreen / 2 << 8) +
		  DinoInfo[Characters[ShotDino].CType].bloodBlue / 2;
#else
  //Elements[ElCount].RGBA = 0xE0000060;
  //Elements[ElCount].RGBA2= 0x20000030;
	Elements[ElCount].RGBA = 0xE0000000 +
		(DinoInfo[Characters[ShotDino].CType].bloodBlue << 16) +
		(DinoInfo[Characters[ShotDino].CType].bloodGreen << 8) +
		DinoInfo[Characters[ShotDino].CType].bloodRed;
	Elements[ElCount].RGBA2= 0x20000000 +
		(DinoInfo[Characters[ShotDino].CType].bloodBlue/2 << 16) +
		(DinoInfo[Characters[ShotDino].CType].bloodGreen/2 << 8) +
		DinoInfo[Characters[ShotDino].CType].bloodRed/2;
#endif
    break;

  case partGround:
#ifdef _d3d
    Elements[ElCount].RGBA = 0xF0F09E55;
    Elements[ElCount].RGBA2= 0x10F09E55;
#else
    Elements[ElCount].RGBA = 0xF0559EF0;
    Elements[ElCount].RGBA2= 0x10559EF0;
#endif
    break;


  case partBubble:
    c = WaterList[ WMap[ static_cast<int>(z) / 256][ static_cast<int>(x) / 256] ].fogRGB;
#ifdef _d3d
    c = ColorSum( ((c & 0xFEFEFE)>>1), 0x152020);
#else
    c = ColorSum( ((c & 0xFEFEFE)>>1), 0x202015);
#endif
    Elements[ElCount].RGBA = 0x70000000 + (ColorSum(c, ColorSum(c,c)));
    Elements[ElCount].RGBA2= 0x40000000 + (ColorSum(c, c));
    break;

  case partWater:
    c = WaterList[ WMap[ static_cast<int>(z) / 256][ static_cast<int>(x) / 256] ].fogRGB;
#ifdef _d3d
    c = ColorSum( ((c & 0xFEFEFE)>>1), 0x152020);
#else
    c = ColorSum( ((c & 0xFEFEFE)>>1), 0x202015);
#endif
    Elements[ElCount].RGBA  = 0xB0000000 + ( ColorSum(c, ColorSum(c,c)) );
    Elements[ElCount].RGBA2 = 0x40000000 + (c);
    break;
  }

  Elements[ElCount].RGBA  = conv_xGx(Elements[ElCount].RGBA);
  Elements[ElCount].RGBA2 = conv_xGx(Elements[ElCount].RGBA2);

  float al = siRand(128) / 128.f * pi / 4.f;
  float ss = sin(al);
  float cc = cos(al);

  for (int e=0; e<Elements[ElCount].ECount; e++)
  {
    Elements[ElCount].EList[e].pos.x = x;
    Elements[ElCount].EList[e].pos.y = y;
    Elements[ElCount].EList[e].pos.z = z;
    Elements[ElCount].EList[e].R = 6 + rRand(5);
    Elements[ElCount].EList[e].Flags = 0;
    float v;

	float velo = mag * rRand(20)/20;

    switch (etype)
    {
    case partBlood:
      v = velo * 6 + rRand(96) + 220;
      Elements[ElCount].EList[e].speed.x =ss*ca*v + siRand(32);
      Elements[ElCount].EList[e].speed.y =cc * (v * 3);
      Elements[ElCount].EList[e].speed.z =ss*sa*v + siRand(32);
      break;
    case partGround:
      Elements[ElCount].EList[e].speed.x =siRand(52)-sa*64;
      Elements[ElCount].EList[e].speed.y =rRand(100) + 600 + velo * 20;
      Elements[ElCount].EList[e].speed.z =siRand(52)+ca*64;
      break;
    case partWater:
		Elements[ElCount].EList[e].speed.x = siRand(32);
		Elements[ElCount].EList[e].speed.z = siRand(32);
		Elements[ElCount].EList[e].speed.y =rRand(80) + 400 + velo * 40;
		if (angled) {
			Elements[ElCount].EList[e].speed.x = siRand(132) + (static_cast<float>(cos(alph)) * velo * 40);
			Elements[ElCount].EList[e].speed.z = siRand(132) + (static_cast<float>(sin(alph)) * velo * 40);
		}
      break;
    case partBubble:
      Elements[ElCount].EList[e].speed.x =siRand(40);
      Elements[ElCount].EList[e].speed.y =rRand(140) + 20;
      Elements[ElCount].EList[e].speed.z =siRand(40);
      break;
    }
  }

  ElCount++;
}
int AnimateBullet(float ax, float ay, float az,
              float bx, float by, float bz, int b)
{
  int sres;
    sres = TraceShot(ax, ay, az, bx, by, bz, bullet[b].Danger, bullet[b].cDanger);

//ENDTRACE:

	bool poon = false;
	if (WeapInfo[bullet[b].parent].harpoon &&
		GetLandUpH(bx, bz) > GetLandH(bx, bz) &&
		GetLandUpH(bx, bz) > by) {
		poon = true;
		if (!bullet[b].state) AddElements(bx, by, bz, partBubble, 1);
	}
  if (sres==-1) return sres;

  int mort = (sres & 0xFF00) && (Characters[ShotDino].Health);
  sres &= 0xFF;

  int powerL = WeapInfo[CurrentWeapon].Power;
  if (poon) powerL = WeapInfo[CurrentWeapon].PowerAq;
  if (powerL > 100) powerL = 100;
	  
	  //underwater model/ground impact sounds?

	  //if (sres != tresChar) return sres;
	  // add in underwater body impact sounds
	  //if (!Characters[ShotDino].Health) return sres;
  
	  if (sres == tresGround) {
		  if (!poon) AddElements(bx, by, bz, partGround, 6 + powerL * 4);
		  int sNo = rRand(2);
		  if (!IsUnderwater() && !poon) AddVoice3dv(fxImpactGround[sNo].length, fxImpactGround[sNo].lpData.data(), bx, by, bz, 256);
		  if (IsUnderwater() && poon) AddVoice3dv(fxImpactAquatic[sNo].length, fxImpactAquatic[sNo].lpData.data(), bx, by, bz, 256);
	  }
	  if (sres == tresModel) {
		  if (!poon) AddElements(bx, by, bz, partGround, 6 + powerL * 4);
		  int sNo = rRand(2);
		  if (!IsUnderwater() && !poon) AddVoice3dv(fxImpactModel[sNo].length, fxImpactModel[sNo].lpData.data(), bx, by, bz, 256);
		  if (IsUnderwater() && poon) AddVoice3dv(fxImpactAquatic[sNo].length, fxImpactAquatic[sNo].lpData.data(), bx, by, bz, 256); //change this to aquatic sound
	  }

	  if (sres == tresWater)
	  {
		  AddElements(bx, by, bz, partWater, 4 + powerL * 3);
		  //AddElements(bx, GetLandH(bx, bz), bz, partBubble);
		  //AddWCircle(bx, bz, 1.2);
		  AddWCircle(bx, bz, 1.2);
		  int sNo = rRand(2);
		  AddVoice3dv(fxImpactWater[sNo].length, fxImpactWater[sNo].lpData.data(), bx, by, bz, 256);
	  }
	  


	  if (sres != tresChar && sres != tresHunter) return sres;
	  if (!poon) AddElements(bx, by, bz, partBlood, 4 + powerL * 4);
	  int sNo = rRand(2);
	  if (!IsUnderwater() && !poon) AddVoice3dv(fxImpactChar[sNo].length, fxImpactChar[sNo].lpData.data(), bx, by, bz, 256);
	  if (IsUnderwater() && poon) AddVoice3dv(fxImpactAquatic[sNo].length, fxImpactAquatic[sNo].lpData.data(), bx, by, bz, 256); //change this to aquatic sound

	  if (sres == tresHunter) {
		AddDeadBody(nullptr, HUNT_EAT, true);
		Characters[ChCount - 1].alpha = PlayerAlpha - pi / 2;
		return sres;
	  } else if (!Characters[ShotDino].Health) return sres;

//======= character damage =========//

  if (WeapInfo[bullet[b].parent].onRadar) {
	  Characters[ShotDino].tracker = bullet[b].parent;
	  Characters[ShotDino].RTime = bullet[b].RTime;
  }

  if (Multiplayer && !Host) {
	  if (mort && !WeapInfo[bullet[b].parent].cannotMortal) sendDamage[ShotDino] = Characters[ShotDino].Health;
	  else {
		  if (poon) sendDamage[ShotDino] += WeapInfo[CurrentWeapon].PowerAq;
		  else sendDamage[ShotDino] += WeapInfo[CurrentWeapon].Power;
	  }
  } else {
	  if (mort && !WeapInfo[bullet[b].parent].cannotMortal) Characters[ShotDino].Health = 0;
	  else {
		  if (poon) Characters[ShotDino].Health -= WeapInfo[CurrentWeapon].PowerAq;
		  else Characters[ShotDino].Health -= WeapInfo[CurrentWeapon].Power;
	  }
	  if (Characters[ShotDino].Health < 0) Characters[ShotDino].Health = 0;
	  registerDamage(ShotDino, bullet[b].enemy);
  }
  
  return sres;
}
void AddBullet(float ax, float ay, float az,
	float Dx, float Dy, float Dz,
	float Dlx, float Dly, float Dlz,
	int parent, bool enemy)
{
	// bullet[] is fixed at 256. Stuck bolts (retrieve) linger, so sustained
	// fire without this guard walks past the array into neighbouring globals
	// -> corruption/freeze. Dropping the newest shot when full is harmless
	// (256 live projectiles never happens in play).
	if (bulletCh < 0 || bulletCh >= 256) return;
	bullet[bulletCh].a.x = ax;
	bullet[bulletCh].a.y = ay;
	bullet[bulletCh].a.z = az;
	bullet[bulletCh].orig.x = ax;
	bullet[bulletCh].orig.y = ay;
	bullet[bulletCh].orig.z = az;
	bullet[bulletCh].dif.x = Dx;
	bullet[bulletCh].dif.y = Dy;
	bullet[bulletCh].dif.z = Dz;
	bullet[bulletCh].ldif.x = Dlx;
	bullet[bulletCh].ldif.y = Dly;
	bullet[bulletCh].ldif.z = Dlz;
	bullet[bulletCh].parent = parent;
	bullet[bulletCh].fallTotal = 0;
	bullet[bulletCh].state = 0;
	bullet[bulletCh].Danger = enemy;
	bullet[bulletCh].cDanger = !enemy;
	bullet[bulletCh].enemy = enemy;
	bullet[bulletCh].alpha = FindVectorAlpha(Dx, Dz);
	bullet[bulletCh].beta = FindVectorAlpha(sqrt(Dz*Dz + Dx*Dx), Dy);
	if (!enemy){
		if (WeapInfo[parent].onRadar) bullet[bulletCh].RTime = 1;
		if (WeapInfo[parent].radarTime) bullet[bulletCh].RTime = WeapInfo[parent].radarTime;
	}
	if (IsUnderwater()) bullet[bulletCh].aqState = 1;
	else bullet[bulletCh].aqState = 0;
	bulletCh++;
}
void AnimateBullets() {
	for (int b=0; b < bulletCh; b++) {

		if (bullet[b].RTime) {
			if (WeapInfo[bullet[b].parent].radarTime) bullet[b].RTime -= TimeDt;
			if (bullet[b].RTime < 0) bullet[b].RTime = 0;
		}

		if (bullet[b].state) {
			bullet[b].Danger = false;
			bullet[b].cDanger = false;
			if (VectorLength(SubVectors(PlayerPos, bullet[b].a)) < 300.f) {

				int maxAm = WeapInfo[bullet[b].parent].Shots;
				if (DoubleAmmo && (WeapInfo[bullet[b].parent].Reload || WeapInfo[bullet[b].parent].rldAnim < 0)) maxAm *= 2;
				if (ShotsLeft[bullet[b].parent] < maxAm) {
					int collectNo = rRand(2);
					AddVoicev(fxCollect[collectNo].length, fxCollect[collectNo].lpData.data(), 256);
					if (!Chambered[bullet[b].parent] &&
						((WeapInfo[bullet[b].parent].pmpAnim < 0 && !WeapInfo[bullet[b].parent].Reload) ||
						(WeapInfo[bullet[b].parent].rldAnim < 0 && WeapInfo[bullet[b].parent].Reload)))
						Chambered[bullet[b].parent]++;
					else ShotsLeft[bullet[b].parent]++;
					memcpy(&bullet[b], &bullet[b + 1], (bulletCh - 1 - b) * sizeof(TBullet));
					b--;
					bulletCh--;
				}
			}
		} else {

			if (!bullet[b].Danger)
				if (VectorLength(SubVectors(bullet[b].a, bullet[b].orig)) > 128.f)
					bullet[b].Danger = true;
			
			if (!bullet[b].cDanger)
				if (VectorLength(SubVectors(bullet[b].a, bullet[b].orig)) > 128.f)
					bullet[b].cDanger = true;

			Vector3d d = bullet[b].dif;
			bool poon = false;
			if (WeapInfo[bullet[b].parent].harpoon &&
				GetLandUpH(bullet[b].a.x, bullet[b].a.z) > GetLandH(bullet[b].a.x, bullet[b].a.z) &&
				GetLandUpH(bullet[b].a.x, bullet[b].a.z) > bullet[b].a.y)
				poon = true;

			if (bullet[b].aqState<2)
				if ((poon && bullet[b].aqState == 0) ||
					(!poon && bullet[b].aqState == 1)) {
					bullet[b].aqState = 2;
					bullet[b].dif = bullet[b].ldif;
					bullet[b].dif.y -= bullet[b].fallTotal;
					d = bullet[b].dif;
				}

			/*
			if (bullet[b].submerged) {

			//	d.x /= 2;
			//	d.z /= 2;
			//	d.y /= 2;
			}
   		    */

			int sres = AnimateBullet(
				bullet[b].a.x,
				bullet[b].a.y,
				bullet[b].a.z,
				bullet[b].a.x + d.x,
				bullet[b].a.y + d.y,
				bullet[b].a.z + d.z,
				b);

			//this ought to be adjusted on frame time

			float pdx = PlayerX - bullet[b].a.x;
			float pdz = PlayerZ - bullet[b].a.z;
			float pd = pdx * pdx + pdz * pdz;

			if ((sres > 0 && (!WeapInfo[bullet[b].parent].harpoon || sres!=tresWater)) || 
				pd > ProjectileViewRangeSquared(ctViewR)) {
				if (WeapInfo[bullet[b].parent].retrieve && (sres == 1 || sres == 3)) {
					bullet[b].state = 1;
					bullet[b].a = TraceB;
				} else {
					memcpy(&bullet[b], &bullet[b + 1], (bulletCh - 1 - b) * sizeof(TBullet));
					b--;
					bulletCh--;
				}
			} else {
				bullet[b].a.x += d.x;
				bullet[b].a.y += d.y;
				bullet[b].a.z += d.z;
				if (poon) {
					bullet[b].dif.y -= WeapInfo[bullet[b].parent].FallAq;
					if (bullet[b].aqState<2) bullet[b].fallTotal += WeapInfo[bullet[b].parent].FallAq;
				} else {
					bullet[b].dif.y -= WeapInfo[bullet[b].parent].Fall;
					if (bullet[b].aqState < 2) bullet[b].fallTotal += WeapInfo[bullet[b].parent].Fall;
				}
				if (WeapInfo[bullet[b].parent].bullet) {
					bullet[b].FTime += TimeDt;
					if (bullet[b].FTime >= Weapon.Bullet[bullet[b].parent].Animation[0].AniTime)
						bullet[b].FTime %= Weapon.Bullet[bullet[b].parent].Animation[0].AniTime;
					bullet[b].alpha = FindVectorAlpha(bullet[b].dif.x, bullet[b].dif.z);
					bullet[b].beta = FindVectorAlpha(
						sqrt(bullet[b].dif.z*bullet[b].dif.z +
							bullet[b].dif.x*bullet[b].dif.x), bullet[b].dif.y);
				}
			}

		}

	}

}
void registerDamage(int Dino, bool enemyBullet) {

	if (!Characters[Dino].Health)
	{
		if ((DinoInfo[Characters[Dino].CType].BaseScore || DinoInfo[Characters[Dino].CType].trophy) && !Multiplayer && g_GameMode != GameMode::SurvivalMode && !enemyBullet) //No trophies in multiplayer for now - update this at later date?
		{
			TrophyRoom.Last.success++;
			SubmitDinoScore(Dino);
		}

		//No amb respawn in multiplayer for now - update this at later date?
		Characters_AddSecondaryOne(&Characters[Dino]);

	}
	else
	{
		Characters[Dino].awareHunter = true;
		Characters[Dino].AfraidTime = 60 * 1000;
		if (Characters[Dino].Clone != AI_TREX || Characters[Dino].State == 0)
			Characters[Dino].State = 2;

		Characters[Dino].BloodTTime += 90000;

	}

	if (Characters[Dino].Clone == AI_TREX)
		if (Characters[Dino].State)
			Characters[Dino].State = 5;
		else
			Characters[Dino].State = 1;

}
void RemoveCharacter(int index)
{
  if (index==-1) return;
  memcpy( &Characters[index], &Characters[index+1], (255 - index) * sizeof(TCharacter) );
  ChCount--;

  if (DemoPoint.CIndex > index) DemoPoint.CIndex--;

  for (int c=0; c<ShipTask.tcount; c++)
    if (ShipTask.clist[c]>index) ShipTask.clist[c]--;
}
