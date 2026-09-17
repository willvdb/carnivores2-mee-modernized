// ==========================================================================
// CharacterLoader.cpp
// ==========================================================================

#include "Hunt.h"
#include "LoadValidate.h"

// Forward declarations
void PlaceHunter();

void LoadCharacters()
{
  std::int32_t pres[DINOINFO_MAX];
  memset(pres, 0, sizeof(pres));
  pres[0]=true;
  for (int c=0; c<ChCount; c++)
  {
    pres[Characters[c].CType] = true;
  }

  for (int c=0; c<TotalC; c++) if (pres[c] || (g_GameMode == GameMode::SurvivalMode && DinoInfo[c].survivalDino))
    {

      if (!ChInfo[c].mptr)
      {
        snprintf(logt, sizeof(logt), "HUNTDAT\\%s", DinoInfo[c].FName);
        LoadCharacterInfo(ChInfo[c], logt);
        PrintLog("Loading: ");
        PrintLog(logt);
        PrintLog("\n");
      }

    }

  for (int c=10; c<20; c++)
    if (TargetDino & (1<<c))
      if (!MenuDinoInfo[c-10].CallIcon.lpImage)
      {
        snprintf(logt, sizeof(logt), "HUNTDAT\\MENU\\PICS\\call%d.tga", c-9);
        LoadPictureTGA(MenuDinoInfo[c - 10].CallIcon, logt, MemoryTag::Level);
        conv_pic(MenuDinoInfo[c - 10].CallIcon);
      }

  // Keep track of max VCount for the weapons available
  int maxWeaponVCount = 0;
  for (int c=0; c<TotalW; c++)
    if (WeaponPres & (1<<c))
    {
      if (!Weapon.chinfo[c].mptr)
      {
        snprintf(logt, sizeof(logt), "HUNTDAT\\WEAPONS\\%s", WeapInfo[c].FName);
        LoadCharacterInfo(Weapon.chinfo[c], logt);
        PrintLog("Loading: ");
        PrintLog(logt);
        PrintLog("\n");
      }

	  if (WeapInfo[c].bullet) {
		  snprintf(logt, sizeof(logt), "HUNTDAT\\WEAPONS\\%s", WeapInfo[c].BLName);
		  LoadCharacterInfo(Weapon.Bullet[c], logt);
		  PrintLog("Loading: ");
		  PrintLog(logt);
		  PrintLog("\n");
	  }

	  maxWeaponVCount = MAX(Weapon.chinfo[c].mptr->VCount, maxWeaponVCount);

      if (!Weapon.BulletPic[c].lpImage)
      {
        snprintf(logt, sizeof(logt), "HUNTDAT\\WEAPONS\\%s", WeapInfo[c].BFName);
        LoadPictureTGA(Weapon.BulletPic[c], logt, MemoryTag::Level);
        conv_pic(Weapon.BulletPic[c]);
        PrintLog("Loading: ");
        PrintLog(logt);
        PrintLog("\n");
      }

	  if (!Weapon.ChambPic[c].lpImage && WeapInfo[c].picch)
	  {
		  snprintf(logt, sizeof(logt), "HUNTDAT\\WEAPONS\\%s", WeapInfo[c].CFName);
		  LoadPictureTGA(Weapon.ChambPic[c], logt, MemoryTag::Level);
		  conv_pic(Weapon.ChambPic[c]);
		  PrintLog("Loading: ");
		  PrintLog(logt);
		  PrintLog("\n");
	  }

	    if (WeapInfo[c].MGSSound) {
			snprintf(logt, sizeof(logt), "MULTIPLAYER\\GUNSHOTS\\%s", WeapInfo[c].SFXName);
			LoadWav(logt, fxGunShot[c]);
			WeapInfo[c].SFXIndex = c;
		  } else WeapInfo[c].SFXIndex = -1;
	  
    }

  // Weapon normals are reallocated on every LoadCharacters (each level
  // load AND each in-process restart), and the old buffer is dropped by
  // unique_ptr reset. LevelArena->Reset() only runs on full level loads,
  // so a Level tag would strand unreclaimable arena space on restarts.
  // Tag it Global: the heap free on reset is real, and the report stays
  // clean because the owner always releases it.
  size_t normalBytes = 0;
  if (maxWeaponVCount < 0 ||
      !CheckedBytes2(static_cast<size_t>(maxWeaponVCount), sizeof(Vector3d), normalBytes))
    DoHalt("Weapon normal allocation size overflow.");
  Weapon.normals.reset((Vector3d*)_HeapAlloc(Heap, 0, normalBytes, MemoryTag::Global));

  for (int c=10; c<20; c++)
    if (TargetDino & (1<<c))
      if (fxCall[c-10][0].lpData.empty())
      {
        snprintf(logt, sizeof(logt),"HUNTDAT\\SOUNDFX\\CALLS\\call%d_a.wav", (c-9));
        LoadWav(logt, fxCall[c-10][0]);
        snprintf(logt, sizeof(logt),"HUNTDAT\\SOUNDFX\\CALLS\\call%d_b.wav", (c-9));
        LoadWav(logt, fxCall[c-10][1]);
        snprintf(logt, sizeof(logt),"HUNTDAT\\SOUNDFX\\CALLS\\call%d_c.wav", (c-9));
        LoadWav(logt, fxCall[c-10][2]);
      }

  snprintf(logt, sizeof(logt), "MULTIPLAYER\\AVATARS\\Hitbox.car");
  LoadCharacterInfo(HitBoxModel, logt);
  PrintLog("Loading: ");
  PrintLog(logt);
  PrintLog("\n");

  //multiplayer hunter models
  //test - 1 other player
  //test - add custom models at some point?
  if (Multiplayer) {
	  snprintf(logt, sizeof(logt), "MULTIPLAYER\\AVATARS\\Poacher.car");
	  LoadCharacterInfo(MPlayerInfo[0], logt);
	  PrintLog("Loading: ");
	  PrintLog(logt);
	  PrintLog("\n");
  }

  // Phase 5F.2: print per-level arena stats now that every per-level
  // allocation has been made. C1 has the same call at
  // Carnivores1/Hunt/Resources.cpp:1501. Useful for two things:
  //   1. Spotting memory-hungry levels (peak usage vs capacity).
  //   2. Verifying the arena reset works between level transitions
  //      (if the alloc count keeps growing across reloads, the reset
  //      is broken).
  // No-op in non-MEM_DEBUG builds (LogStats is a plain method, not
  // macro-gated, but the call site is the only place that needs the
  // report and it's useful even in release for production tuning).
  if (LevelArena != nullptr) {
    LevelArena->LogStats(" after load");
  }
}

void resetSSHip() {
	SShip.State = 0;
	SShip.alpha = 0;
	SShip.speed = 0;
	AmmoBag.State = 0;
}

void resetBullets() {
	for (int b = 0; b < bulletCh; b++) {
		bullet[b] = {};
	}
	bulletCh = 0;
}

void refillWeapons(bool init) {
	for (int w = 0; w < TotalW; w++)
		if (WeaponPres & (1 << w))
		{
			if (WeapInfo[w].fullauto) FiringMode[w] = 1; else FiringMode[w] = 0;

			ShotsLeft[w] = WeapInfo[w].Shots;
			if (DoubleAmmo) {
				if (WeapInfo[w].Reload || WeapInfo[w].rldAnim < 0) {
					ShotsLeft[w] *= 2;
				} else {
					AmmoMag[w] = 1;
					MagShotsLeft[w] = WeapInfo[w].Shots;
				}
			}
			
			if (WeapInfo[w].Reload) {
				if (init || WeapInfo[w].rldAnim < 0) {
					Chambered[w] = WeapInfo[w].Reload;
					ShotsLeft[w] -= WeapInfo[w].Reload;
				}
			} else {
				if (init || WeapInfo[w].pmpAnim < 0) {
					Chambered[w] = 1;
					ShotsLeft[w] -= 1;
				}
			}
			if (TargetWeapon == -1) TargetWeapon = w;
		}
}

void ReInitGame()
{
  PrintLog("ReInitGame();\n");

  // Initialize underwater fog debug parameters with default values.
  // These can be tweaked at runtime via the F10 debug menu.
  // CameraWaterDepthFactor is recomputed from the camera position each frame;
  // this is only its initial fallback value.
  CameraWaterDepthFactor = 0.099f;
  UnderwaterDebugMenu = 0;
  UnderwaterDebugSelected = 0;
  UWFog_VertRange = 1488.6f;       // vertical gradient range (units)
  UWFog_VertStrength = 85.0f;      // additive fog at max depth
  UWFog_CurveExp = 1.94f;          // curve exponent
  UWFog_CapBase = 85.0f;           // soft cap added to FLimit
  UWFog_CapCameraBoost = 100.0f;   // camera depth boost for cap
  UWFog_BaseDensityMult = 0.23f;   // base fog density multiplier
  UWFog_CameraDepthMult = 0.10f;   // Beer-Lambert camera depth multiplier

  // Water wave debug parameters
  UnderwaterDebugTab = 0;
  WWave1Amp = 14.58f;              // primary swell amplitude
  WWave2Amp = 8.10f;               // secondary cross-wave amplitude
  WWave3Amp = 5.00f;               // fine detail amplitude
  WWaveSpeed = 0.50f;              // time multiplier

  // Sun glare debug parameters (1.0 = stock)
  SunGlare_Master = 1.0f;         // fullscreen blinding strength
  SunGlare_Disc = 1.0f;           // sky sun/moon disc alpha

  PlaceHunter();
  Muzz = false;
  MuzzFTime = 0;
  if (g_GameMode == GameMode::TrophyMode)	PlaceTrophy();
  else if (g_GameMode == GameMode::SurvivalMode) {
	  SurvivalWave = 0;
	  PlaceCharactersSurvival();
  } else {
	  PlaceCharacters();
	  resetSSHip();
	  resetBullets();
	  if (Multiplayer) {
		  sendGunShot = -1;
		  sendHunterCall = -1;
		  sendHunterCallType = -1;
		  for (int c = 0; c < ChCount; c++) {
			  sendDamage[c] = 0;
		  }
		  for (int i = 0; i < 4; i++) {
			  mGunShot[i] = -1;
			  mHunterCall[i] = -1;
			  mHunterCallType[i] = -1;
			  for (int c = 0; c < ChCount; c++) {
				  mDamage[i][c] = 0;
			  }
		  }
		  HunterCount = 0;
		  PlaceMHunters();//temp??
	  }
  }

  LoadCharacters();

  LockLanding = false;
  Wind.alpha = rRand(1024) * 2.f * pi / 1024.f;
  Wind.speed = 10;
  MyHealth = MAX_HEALTH;
  TargetWeapon = -1;

  refillWeapons(true);

  CurrentWeapon = TargetWeapon;

  Weapon.state = 0;
  Weapon.FTime = 0;
  PlayerAlpha = 0;
  PlayerBeta  = 0;
  Weapon.breath = 0.f;
  Weapon.BTime = 0;
  Weapon.HoldBreath = false;
  Weapon.breathPressed = 0;
  CrouchMode = 0;
  HitBox.phase = 0;

  WCCount = 0;
  ElCount = 0;
  BloodTrail.Count = 0;
  // Capture survival before clearing: the mode is sticky across levels
  // (set via -survival), and the scope needs ScopePower at init because
  // HideWeapon() early-returns in SurvivalMode and would never set it.
  bool wasSurvival = (g_GameMode == GameMode::SurvivalMode);
  g_GameMode = GameMode::Normal;

  if (wasSurvival) {
	  g_GameMode = GameMode::SurvivalMode;
	  PlayerAlpha = pi * 2 * SurvivalSpawnA / 360.f;
	  Weapon.state = 2;
	  if (WeapInfo[CurrentWeapon].Optic) {
		  g_GameMode = GameMode::OpticScope;
		  ScopePower = WeapInfo[CurrentWeapon].breathaim ? 1.0f : WeapInfo[CurrentWeapon].Optic;
	  }
  }

  Ship.pos.x = PlayerX;
  Ship.pos.z = PlayerZ;
  Ship.pos.y = GetLandUpH(Ship.pos.x, Ship.pos.z) + 2048;
  Ship.State = -1;
  Ship.tgpos.x = Ship.pos.x;
  Ship.tgpos.z = Ship.pos.z + 60*256;
  Ship.cindex  = -1;
  Ship.tgpos.y = GetLandUpH(Ship.tgpos.x, Ship.tgpos.z) + 2048;
  ShipTask.tcount = 0;

  if (g_GameMode != GameMode::TrophyMode)
  {
    TrophyRoom.Last.smade = 0;
    TrophyRoom.Last.success = 0;
    TrophyRoom.Last.path  = 0;
    TrophyRoom.Last.time  = 0;
  }

  DemoPoint.DemoTime = 0;
  RestartMode = false;
  TrophyDisplay=false;
  answtime = 0;
  ExitTime = 0;

  // Per-restart render scratch. ReInitGame does NOT call ReleaseResources,
  // so the old comment's arena-reclamation claim was wrong: each restart
  // orphaned three unreclaimable arena blocks (~27 KiB measured). Free the
  // previous buffers explicitly and keep them on the heap, where the free
  // is real. ReleaseResources() still releases them on level transitions.
  if (rVertex) { (void)_HeapFree(Heap, 0, rVertex); rVertex = nullptr; }
  if (gScrp) { (void)_HeapFree(Heap, 0, gScrp); gScrp = nullptr; }
  if (PhongMapping) { (void)_HeapFree(Heap, 0, PhongMapping); PhongMapping = nullptr; }
  size_t vertexBytes = 0, screenBytes = 0, mappingBytes = 0;
  if (MaxObjectVCount < 0 ||
      !CheckedBytes2(static_cast<size_t>(MaxObjectVCount), sizeof(Vector3d), vertexBytes) ||
      !CheckedBytes2(static_cast<size_t>(MaxObjectVCount), sizeof(Vector2di), screenBytes) ||
      !CheckedBytes2(static_cast<size_t>(MaxObjectVCount), sizeof(Vector2df), mappingBytes))
    DoHalt("Vertex scratch allocation size overflow.");
  rVertex = (Vector3d*)_HeapAlloc(Heap, 0, vertexBytes, MemoryTag::Global);
  gScrp = (Vector2di*)_HeapAlloc(Heap, 0, screenBytes, MemoryTag::Global);
  PhongMapping = (Vector2df*)_HeapAlloc(Heap, 0, mappingBytes, MemoryTag::Global);
  AllocateRenderTables();
}