// EngineInit.cpp � auto-extracted from Game.cpp
// ==========================================================================
// Auto-extracted from Game.cpp
// ==========================================================================

#include "Hunt.h"
#include "Platform/Platform.h"

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include "Core/ConfigText.h"
#include "Game/DisplayModes.h"

#ifdef _gl
#include "Renderer/GLPerf.h"
#endif

// Imported from Game.cpp
extern bool ShowFaces;

void UploadGeometry()
{
  int x,y,xx,yy;
  std::uint8_t temp;

  AudioFCount = 0;

  int MaxView = 18;
  int HalfView = static_cast<int>((MaxView/2))+1;

  for (x = 0; x < MaxView; x++)
    for (y = 0; y < MaxView; y++)
    {
      xx = (x - HalfView)*2;
      yy = (y - HalfView)*2;
      data[AudioFCount].x1 = (CCX+xx) * 256 - CameraX;
      data[AudioFCount].y1 = HMap[CCY+yy][CCX+xx] * ctHScale - CameraY;
      data[AudioFCount].z1 = (CCY+yy) * 256 - CameraZ;

      xx = ((x+1) - HalfView)*2;
      yy = (y - HalfView)*2;
      data[AudioFCount].x2 = (CCX+xx) * 256 - CameraX;
      data[AudioFCount].y2 = HMap[CCY+yy][CCX+xx] * ctHScale - CameraY;
      data[AudioFCount].z2 = (CCY+yy) * 256 - CameraZ;

      xx = ((x+1) - HalfView)*2;
      yy = ((y+1) - HalfView)*2;
      data[AudioFCount].x3 = (CCX+xx) * 256 - CameraX;
      data[AudioFCount].y3 = HMap[CCY+yy][CCX+xx] * ctHScale - CameraY;
      data[AudioFCount].z3 = (CCY+yy) * 256 - CameraZ;

      xx = (x - HalfView)*2;
      yy = ((y+1) - HalfView)*2;
      data[AudioFCount].x4 = (CCX+xx) * 256 - CameraX;
      data[AudioFCount].y4 = HMap[CCY+yy][CCX+xx] * ctHScale - CameraY;
      data[AudioFCount].z4 = (CCY+yy) * 256 - CameraZ;

      AudioFCount++;
    }

//     MessageBeep(-1);

  if (ShowFaces)
  {
    snprintf(logt, sizeof(logt),"Audio_UpdateGeometry: %i faces uploaded\n", AudioFCount);
    PrintLog(logt);

    ShowFaces = false;
  }
}
void SetupRes()
{
  // OptRes is an index into ResolutionList[]. Fall back to the first
  // 800x600 entry (or 0) if the saved index is out of range.
  if (ResCount <= 0) {
    WinW = 800;
    WinH = 600;
    return;
  }
  if (OptRes < 0 || OptRes >= ResCount) {
    OptRes = 0;
    for (int r = 0; r < ResCount; r++) {
      if (ResolutionList[r].w == 800 && ResolutionList[r].h == 600) {
        OptRes = r;
        break;
      }
    }
  }
  WinW = ResolutionList[OptRes].w;
  WinH = ResolutionList[OptRes].h;
}
void EnumerateResolutions()
{
  const auto resolutions = GameDisplay::SelectResolutions(Platform::QueryDisplayInfo());
  ResCount = resolutions.count;
  for (int i = 0; i < ResCount; ++i) {
    ResolutionList[i].w = resolutions.modes[i].width;
    ResolutionList[i].h = resolutions.modes[i].height;
  }
}
void SubmitDinoScore (int cindex) {
	float score = DinoInfo[Characters[cindex].CType].BaseScore;

	if (TrophyRoom.Last.success > 1)
		score *= (1.f + TrophyRoom.Last.success / 10.f);

	//if (!(TargetDino & (1<<DinoInfo[Characters[cindex].CType].menuDino)) ) score/=2.f;

	SYSTEMTIME st;
	GetLocalTime(&st);
	// Score multipliers are now driven by the Menu (see smod= in
	// ProcessCommandLine) so modders can tune them via _RES.TXT.
	// Defaults match the original hardcoded values when no smod= is
	// supplied (see InitEngine).
	if (Tranq) score *= ScoreMod_Tranq;
	if (RadarMode) score *= ScoreMod_Radar;
	if (ScentMode) score *= ScoreMod_Scent;
	if (CamoMode) score *= ScoreMod_Camo;
	TrophyRoom.Score += static_cast<int>(score);
	Characters[cindex].tempScore = static_cast<int>(score);
	Characters[cindex].tempDate = (st.wYear << 20) + (st.wMonth << 10) + st.wDay;
	Characters[cindex].tempTime = (st.wHour << 10) + st.wMinute;
	Characters[cindex].tempRange = VectorLength(SubVectors(Characters[cindex].pos, PlayerPos)) / 64.f;

	ScoreDispTime = 2500;
	ScoreDisp = static_cast<int>(score);

}
void HideWeapon()
{
  TWeapon *wptr = &Weapon;
  if (IsUnderwater() && !wptr->state && !WeapInfo[CurrentWeapon].harpoon) return;
  // Belt and braces: the mode check plus the stable map-name check (the mode
  // value has many writers and can be flipped mid-session; ProjectName cannot).
  if (ObservMode || g_GameMode == GameMode::TrophyMode || InTrophyRoomMap()) return;
  if (g_GameMode == GameMode::SurvivalMode) return;

  if (wptr->state == 0)
  {  
	//if (!ShotsLeft[CurrentWeapon]) return;
    // ScopePower is only meaningful for optic weapons; non-optic raises
    // leave it untouched (it is unused while not in OpticScope). An
    // explicit init lives in StateDefs.cpp so the first scoped frame is sane.
    // Breath-aim weapons rest unzoomed and magnify only while holding
    // breath, so they raise at 1x; scoped weapons rest at their optic.
    if (WeapInfo[CurrentWeapon].Optic) {
      g_GameMode = GameMode::OpticScope;
      ScopePower = WeapInfo[CurrentWeapon].breathaim ? 1.0f : WeapInfo[CurrentWeapon].Optic;
    }
    
	if (IsUnderwater()) {
		if (WeapInfo[CurrentWeapon].getAqSnd >= 0)
			AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].getAqSnd].length,
				wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].getAqSnd].lpData.data(), 256);
	} else {
		int fx = wptr->chinfo[CurrentWeapon].Anifx[WeapInfo[CurrentWeapon].getAnim];
		if (fx >= 0) AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[fx].length,
			wptr->chinfo[CurrentWeapon].SoundFX[fx].lpData.data(), 256);
	}
    wptr->FTime = 0;
    wptr->state = 1;
    // Clear only binoculars here; the optic scope (if any) must stay active
    // through the raise animation. The previous code reset the whole game
    // mode, which stomped the OpticScope just set above and silently
    // disabled every scoped weapon outside Survival mode.
    if (g_GameMode == GameMode::Binocular) g_GameMode = GameMode::Normal;
    wptr->shakel = WeapInfo[CurrentWeapon].shake * 4.f;
	wptr->breath = 0.f;
	wptr->breathPressed = 0;
	wptr->HoldBreath = false;
    return;
  }

  if (wptr->state!=2 || wptr->FTime!=0) return;
  if (IsUnderwater()) {
	  if (WeapInfo[CurrentWeapon].putAqSnd >= 0)
		  AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].putAqSnd].length,
			  wptr->chinfo[CurrentWeapon].SoundFX[WeapInfo[CurrentWeapon].putAqSnd].lpData.data(), 256);
  } else {
	  int fx = wptr->chinfo[CurrentWeapon].Anifx[WeapInfo[CurrentWeapon].putAnim];
	  if (fx >= 0) AddVoicev(wptr->chinfo[CurrentWeapon].SoundFX[fx].length,
		  wptr->chinfo[CurrentWeapon].SoundFX[fx].lpData.data(), 256);
  }
  wptr->state = 3;
  wptr->FTime = 0;
  g_GameMode = GameMode::Normal;
  return ;
}
void InitGameInfo()
{
  for (int c=0; c< DINOINFO_MAX; c++)
  {
    DinoInfo[c].Scale0 = 800;
    DinoInfo[c].ScaleA = 600;
    DinoInfo[c].ShDelta = 0;
  }
  /*
      WeapInfo[0].Name = "Shotgun";
  	WeapInfo[0].Power = 1.5f;
  	WeapInfo[0].Prec  = 1.1f;
  	WeapInfo[0].Loud  = 0.3f;
  	WeapInfo[0].Rate  = 1.6f;
  	WeapInfo[0].Shots = 6;

  	WeapInfo[1].Name = "X-Bow";
  	WeapInfo[1].Power = 1.1f;
  	WeapInfo[1].Prec  = 0.7f;
  	WeapInfo[1].Loud  = 1.9f;
  	WeapInfo[1].Rate  = 1.2f;
  	WeapInfo[1].Shots = 8;

      WeapInfo[2].Name = "Sniper Rifle";
  	WeapInfo[2].Power = 1.0f;
  	WeapInfo[2].Prec  = 1.8f;
  	WeapInfo[2].Loud  = 0.6f;
  	WeapInfo[2].Rate  = 1.0f;
  	WeapInfo[2].Shots = 6;




  	DinoInfo[ 0].Name = "Moschops";
  	DinoInfo[ 0].Health0 = 2;
  	DinoInfo[ 0].Mass = 0.15f;

      DinoInfo[ 1].Name = "Galimimus";
  	DinoInfo[ 1].Health0 = 2;
  	DinoInfo[ 1].Mass = 0.1f;

  	DinoInfo[ 2].Name = "Dimorphodon";
      DinoInfo[ 2].Health0 = 1;
  	DinoInfo[ 2].Mass = 0.05f;

  	DinoInfo[ 3].Name = "Dimetrodon";
      DinoInfo[ 3].Health0 = 2;
  	DinoInfo[ 3].Mass = 0.22f;


  	DinoInfo[ 5].Name = "Parasaurolophus";
  	DinoInfo[ 5].Mass = 1.5f;
  	DinoInfo[ 5].Length = 5.8f;
  	DinoInfo[ 5].Radius = 320.f;
  	DinoInfo[ 5].Health0 = 5;
  	DinoInfo[ 5].BaseScore = 6;
  	DinoInfo[ 5].SmellK = 0.8f; DinoInfo[ 4].HearK = 1.f; DinoInfo[ 4].LookK = 0.4f;
  	DinoInfo[ 5].ShDelta = 48;

  	DinoInfo[ 6].Name = "Pachycephalosaurus";
  	DinoInfo[ 6].Mass = 0.8f;
  	DinoInfo[ 6].Length = 4.5f;
  	DinoInfo[ 6].Radius = 280.f;
  	DinoInfo[ 6].Health0 = 4;
  	DinoInfo[ 6].BaseScore = 8;
  	DinoInfo[ 6].SmellK = 0.4f; DinoInfo[ 5].HearK = 0.8f; DinoInfo[ 5].LookK = 0.6f;
  	DinoInfo[ 6].ShDelta = 36;

  	DinoInfo[ 7].Name = "Stegosaurus";
      DinoInfo[ 7].Mass = 7.f;
  	DinoInfo[ 7].Length = 7.f;
  	DinoInfo[ 7].Radius = 480.f;
  	DinoInfo[ 7].Health0 = 5;
  	DinoInfo[ 7].BaseScore = 7;
  	DinoInfo[ 7].SmellK = 0.4f; DinoInfo[ 6].HearK = 0.8f; DinoInfo[ 6].LookK = 0.6f;
  	DinoInfo[ 7].ShDelta = 128;

  	DinoInfo[ 8].Name = "Allosaurus";
  	DinoInfo[ 8].Mass = 0.5;
  	DinoInfo[ 8].Length = 4.2f;
  	DinoInfo[ 8].Radius = 256.f;
  	DinoInfo[ 8].Health0 = 3;
  	DinoInfo[ 8].BaseScore = 12;
  	DinoInfo[ 8].Scale0 = 1000;
  	DinoInfo[ 8].ScaleA = 600;
  	DinoInfo[ 8].SmellK = 1.0f; DinoInfo[ 7].HearK = 0.3f; DinoInfo[ 7].LookK = 0.5f;
  	DinoInfo[ 8].ShDelta = 32;
	DinoInfo[ 8].DangerCall = true;

  	DinoInfo[ 9].Name = "Chasmosaurus";
  	DinoInfo[ 9].Mass = 3.f;
  	DinoInfo[ 9].Length = 5.0f;
  	DinoInfo[ 9].Radius = 400.f;
  	DinoInfo[ 9].Health0 = 8;
  	DinoInfo[ 9].BaseScore = 9;
  	DinoInfo[ 9].SmellK = 0.6f; DinoInfo[ 8].HearK = 0.5f; DinoInfo[ 8].LookK = 0.4f;
  	//DinoInfo[ 8].ShDelta = 148;
  	DinoInfo[ 9].ShDelta = 108;

  	DinoInfo[10].Name = "Velociraptor";
  	DinoInfo[10].Mass = 0.3f;
  	DinoInfo[10].Length = 4.0f;
  	DinoInfo[10].Radius = 256.f;
  	DinoInfo[10].Health0 = 3;
  	DinoInfo[10].BaseScore = 16;
  	DinoInfo[10].ScaleA = 400;
  	DinoInfo[10].SmellK = 1.0f; DinoInfo[ 9].HearK = 0.5f; DinoInfo[ 9].LookK = 0.4f;
  	DinoInfo[10].ShDelta =-24;
	DinoInfo[10].DangerCall = true;

  	DinoInfo[11].Name = "T-Rex";
      DinoInfo[11].Mass = 6.f;
  	DinoInfo[11].Length = 12.f;
  	DinoInfo[11].Radius = 400.f;
  	DinoInfo[11].Health0 = 1024;
  	DinoInfo[11].BaseScore = 20;
  	DinoInfo[11].SmellK = 0.85f; DinoInfo[10].HearK = 0.8f; DinoInfo[10].LookK = 0.8f;
  	DinoInfo[11].ShDelta = 168;
	DinoInfo[11].DangerCall = true;

  	DinoInfo[ 4].Name = "Brahiosaurus";
      DinoInfo[ 4].Mass = 9.f;
  	DinoInfo[ 4].Length = 12.f;
  	DinoInfo[ 4].Radius = 400.f;
  	DinoInfo[ 4].Health0 = 1024;
  	DinoInfo[ 4].BaseScore = 0;
  	DinoInfo[ 4].SmellK = 0.85f; DinoInfo[16].HearK = 0.8f; DinoInfo[16].LookK = 0.8f;
  	DinoInfo[ 4].ShDelta = 168;
	DinoInfo[ 4].DangerCall = false;
  */
  LoadResourcesScript();
}


// MULTIPLAYER ===================================================




















static void CreateDefaultConfig();
static void LoadConfig();
void InitEngine()
{
  FULLSCREEN   = true;
  BORDERLESS   = false;
  DEBUG        = false;

  // Explicit zoom defaults: zero-init would collapse CameraW/H (which are
  // multiplied by these in OpticScope/Binocular) to a degenerate frustum
  // on any frame that runs before HideWeapon()/the per-frame clamp.
  ScopePower = 1.0f;
  BinocularPower = 2.0f;

  WATERANI     = true;
  NODARKBACK   = true;
  LoDetailSky  = true;
  CORRECTION   = true;
  FOGON        = true;
  FOGENABLE    = true;
  UIScale      = 1.0f;
  Clouds       = true;
  SKY          = true;
  GOURAUD      = true;
  MODELS       = true;
  TIMER        = DEBUG;
  BITMAPP      = false;
  MIPMAP       = true;
  NOCLIP       = false;
  CLIP3D       = true;


  SLOW         = false;
  LOWRESTX     = false;
  MORPHP       = true;
  MORPHA       = true;

  _GameState = 0;
  _MultiplayerState = 0;

  RadarMode    = false;
  NightVisionMode = false;
  NightVisionOn   = false;
  NightVisionKey  = 0x4E; // Default: 'N' key

  // Accessory score multipliers. Defaults match the legacy hardcoded
  // values that used to live in SubmitDinoScore() so legacy hunts
  // (launched without a 'smod=' argument) keep the same final score.
  ScoreMod_Camo     = 0.85f;
  ScoreMod_Radar    = 0.70f;
  ScoreMod_Scent    = 0.80f;
  ScoreMod_Double   = 1.0f;
  ScoreMod_Tranq    = 1.25f;
  ScoreMod_Observer = 1.0f;

  //multiplayer
  Multiplayer = false;
  Host = false;
  result = nullptr;

#ifdef _WIN32
  fnt_BIG = CreateFont(
              static_cast<int>((23 * UIScale)), static_cast<int>((10 * UIScale)), 0, 0,
              600, 0,0,0,
#ifdef __rus
              RUSSIAN_CHARSET,
#else
              ANSI_CHARSET,
#endif
              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, nullptr);




  fnt_Small = CreateFont(
                static_cast<int>((16 * UIScale)), static_cast<int>((7 * UIScale)), 0, 0,
				100, 0,0,0,
	  
	  //14, 5, 0, 0,
	  //100, 0, 0, 0,
#ifdef __rus
                RUSSIAN_CHARSET,
#else
                ANSI_CHARSET,
#endif
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, nullptr);


  fnt_Midd  = CreateFont(
			    static_cast<int>((16 * UIScale)), static_cast<int>((7 * UIScale)), 0, 0,
	            550, 0, 0, 0,
#ifdef __rus
                RUSSIAN_CHARSET,
#else
                ANSI_CHARSET,
#endif
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, nullptr);


#endif
  Heap = Platform::CreateHeap();
  if( Heap == nullptr )
  {
    MessageBox(hwndMain,"Error creating heap.","Error",IDOK);
    return;
  }

  // Phase 5C.2: Construct the per-level MemoryArena. C1 does this at
  // Carnivores1/Hunt/Game.cpp:506 with 128 MiB; we use LEVEL_ARENA_SIZE
  // (256 MiB) because C2 ME has more resident state (8 weapons, 128
  // dino types, multiplayer, snow, etc.). The arena must be created
  // AFTER HeapCreate (the Heap variable is referenced by all the
  // _HeapAlloc dispatch) and BEFORE LoadResources (which tags most
  // per-level allocations as MemoryTag::Level, causing them to land
  // here instead of in Heap).
  //
  // Defensive: verify the smart pointer layout assumption that all of
  // Phase 5 depends on -- if sizeof(unique_heap_ptr<T>) ever drifts
  // away from a raw pointer, every struct field we migrated will
  // silently change size and break the save-game / multiplayer
  // protocols. C1 has the same static_assert in its InitEngine.
  static_assert(sizeof(unique_heap_ptr<std::uint16_t[]>) == sizeof(void*),
                "unique_heap_ptr<T[]> must be the same size as a raw pointer (x86 EBO)");
  static_assert(sizeof(unique_obj_ptr<TModel>) == sizeof(void*),
                "unique_obj_ptr<TModel> must be the same size as a raw pointer (x86 EBO)");
  LevelArena = new MemoryArena(LEVEL_ARENA_SIZE, "LevelArena");

  // Phase 5E: tag as MemoryTag::Global. This is a one-time
  // allocation in InitEngine that lives for the whole session --
  // it backs the "null" texture at index 255 (used as a fallback
  // when a model references a missing texture). It's released by
  // ReleaseGlobalResources at shutdown. Goes to the heap, not the
  // arena, because it must survive all LevelArena->Reset() calls.
  Textures[255].reset((TEXTURE*) _HeapAlloc(Heap, 0, sizeof(TEXTURE), MemoryTag::Global));

  WaterR = 10;
  WaterG = 38;
  WaterB = 46;
  WaterA = 10;
  TargetDino = 1<<10;
  TargetCall = 10;
  WeaponPres = 1;
  MessageList.timeleft = 0;

  InitGameInfo();

  CreateFadeTab();
  CreateDivTable();
  InitClips();

  TrophyRoom.RegNumber=0;
  PlayerZ = (ctMapSize / 3) * 256;

  ProcessCommandLine();

  switch (OptDayNight)
  {
  case 0:
    SunShadowK = 0.7;
    Sun3dPos.x = - 4048;
    Sun3dPos.y = + 2048;
    Sun3dPos.z = - 4048;
    break;
  case 1:
    SunShadowK = 0.5;
    Sun3dPos.x = - 2048;
    Sun3dPos.y = + 4048;
    Sun3dPos.z = - 2048;
    break;
  case 2:
    SunShadowK = -0.7;
    Sun3dPos.x = + 3048;
    Sun3dPos.y = + 3048;
    Sun3dPos.z = + 3048;
    break;
  }

  // EnumerateResolutions() must come before LoadTrophy() so SetupRes()
  // (called inside LoadTrophy via ReadFile -> SetupRes) can use
  // ResolutionList[] to translate the saved OptRes index into a real
  // WinW/WinH. If we enumerated after, LoadTrophy would have no
  // resolution table to apply.
  EnumerateResolutions();

  // OptFov is the vertical field-of-view in degrees, range [kFovMin,
  // kFovMax]. It drives CameraH = VideoCY * FovScaleFromDegrees(OptFov)
  // in SetVideoMode() and the per-frame camera setup in Hunt.cpp. The
  // default is set unconditionally here as a safety net for first
  // launch (no save file) and for old saves written before the
  // FOV-slider port that don't include the OptFov field. LoadTrophy()
  // overwrites this default with the persisted value (or keeps this
  // default if the saved value is missing/out-of-range).
  OptFov = kFovDefault;
  g_gpuFeatures = kGpuFeaturesDefault;  // GPU-optimization kill-switch (see GameState.h)

  OptViewR = kViewOptDefault;
  OptObjectDetail = kObjectDetailDefault;
OptFpsLimit = 1;  // 1 = 60 FPS (0 remains available for unlimited)

  // Default key bindings (mirror Menu Options::Default). LoadTrophy()
  // overwrites these from the save; without defaults a missing/short save
  // (first launch, pre-modernization file) leaves KeyMap zeroed and every
  // bound action — notably sprint — dead until the user rebinds in the menu.
  KeyMap.fkForward = 'W';
  KeyMap.fkBackward = 'S';
  KeyMap.fkSLeft = 'A';
  KeyMap.fkSRight = 'D';
  KeyMap.fkFire = LegacyKey::LBUTTON;
  KeyMap.fkShow = LegacyKey::RBUTTON;
  KeyMap.fkJump = LegacyKey::SPACE;
  KeyMap.fkCall = LegacyKey::MENU;
  KeyMap.fkBinoc = 'B';
  KeyMap.fkCrouch = 'C';
  KeyMap.fkRun = LegacyKey::LSHIFT;
  KeyMap.fkReload = 'R';
  KeyMap.fkResupply = 'T';
  KeyMap.fkHoldBreath = LegacyKey::LCONTROL;
  KeyMap.fkFiringMode = 'V';
  KeyMap.fkStrafe = 'G';

  LoadTrophy();
  OptViewR = ClampViewOpt(OptViewR);

  // Create default config.cfg if it doesn't exist (first launch).
  CreateDefaultConfig();

  // Override settings from config.cfg (written by Carnivores2Menu).
  // This file is the single source of truth for settings that are not
  // part of the legacy binary trophy format (e.g. OptFov).
  LoadConfig();
  {
    const int fpsValues[] = {0, 60, 120, 240};
    char msg[192];
    snprintf(msg, sizeof(msg), "Config effective: fps_limit=%d (%d FPS; 0=unlimited), fov=%d, object_detail=%d\n",
              OptFpsLimit, fpsValues[OptFpsLimit], OptFov, OptObjectDetail);
    PrintLog(msg);
  }

#ifdef _gl
  // Init3DHardware runs before InitEngine, so apply the config value now
  // that glperf_logging has been loaded. Disabled profiling must remain out
  // of the per-frame render path even when GL_PERF_HOOKS is compiled in.
  glperf_set_logging(g_glperfLoggingEnabled);
#endif

  // CreateVideoDIB() must come after ProcessCommandLine() so WinW/WinH reflect
  // any /res command-line override.
  ProcessCommandLine();
  CreateVideoDIB();

  if (g_GameMode == GameMode::SurvivalMode) OptViewR = kViewOptMax;

  ctViewR = ViewOptToCtViewR(OptViewR);
  // Cap the character-processing view radius independently of the
  // rendering radius.  At ctViewR=230 the game processes characters
  // within a ~4B sq-unit area, causing multi-second frame freezes.
  // A cap of 120 keeps character LOD ~2.2x default while preventing
  // the worst scalability cliff.
  charViewR = (std::min)(ctViewR, 120);
  ctViewRM = ClampObjectDetail(OptObjectDetail);

  Soft_Persp_K = 1.5f;
  HeadY = 220;

  FogsList[0].fogRGB = 0x000000;
  FogsList[0].YBegin = 0;
  FogsList[0].Transp = 000;
  FogsList[0].FLimit = 000;

  FogsList[127].fogRGB = 0x00504000;
  FogsList[127].Mortal = false;
  // Underwater fog density.  Transp=220, FLimit=200: builds up moderately
  // fast, ~78% max opacity.  Was Transp=460 which gave a sparse, dark
  // underwater look.  The depth-based multiplier in CalcFogLevel() ramps
  // density up further as the camera goes deeper, so close-range vertices
  // already look heavily tinted at depth and far vertices saturate at
  // FLimit+60 (260) which the per-vertex shader clamps to 1.0.
  FogsList[127].Transp = 220;
  FogsList[127].FLimit = 200;

  FillMemory( FogsMap, sizeof(FogsMap), 0);
  PrintLog("Init Engine: Ok.\n");
}
void ShutDownEngine()
{
  // Phase 5C.1: Release per-level and global resources before tearing
  // down the heap and the window DC. C1's ShutDownEngine has these calls
  // (Carnivores1/Hunt/Game.cpp:660-670); C2 ME was missing them, so every
  // Quit leaked the level resources, the weapon character info, the
  // Sun/Compass/Binocular models, and the menu pictures. The LevelArena
  // construction is still pending in Phase 5C.2; once it's in, the
  // ReleaseResources() call will also trigger LevelArena->Reset().
  ReleaseResources();
  ReleaseGlobalResources();
#ifdef _WIN32
  ReleaseDC(hwndMain,hdcMain);
#endif

  // Phase 5F.2: Print the leak report to carnivor.log before tearing
  // down the arena. Must run AFTER Release* (so the per-level
  // allocations and global allocations have been released) and BEFORE
  // delete LevelArena (so the pointer values in the report are still
  // valid -- the report is informational only, but the doc explicitly
  // notes that printing after the arena is freed is wasteful). In
  // non-MEM_DEBUG builds the call is a no-op (the function expands to
  // a single branch and returns immediately).
#ifdef MEM_DEBUG
  PrintMemoryLeaks();
#endif

  // Phase 5C.2: Tear down the LevelArena after all _HeapFree calls have
  // run. C1 has the same order (Carnivores1/Hunt/Game.cpp:669-670).
  // LevelArena->Reset() in ReleaseResources() expects LevelArena to be
  // alive; delete must come AFTER that. The VirtualFree on the arena's
  // base pointer is the only thing the destructor does.
  if (LevelArena) {
    delete LevelArena;
    LevelArena = nullptr;
  }
}
void ProcessSyncro()
{
  RealTime = Platform::Milliseconds();
  srand( (unsigned) RealTime );
  if (SLOW) RealTime/=4;
  TimeDt = RealTime - PrevTime;
  if (TimeDt<0) TimeDt = 10;
  if (TimeDt>10000) TimeDt = 10;
  if (TimeDt>1000) TimeDt = 1000;
  PrevTime = RealTime;
  Takt++;
  if (!IsPaused())
    if (MyHealth) MyHealth+=TimeDt*4;
  if (MyHealth>MAX_HEALTH) MyHealth = MAX_HEALTH;
}
void MakeCall()
{
  if (!TargetDino) return;
  if (IsUnderwater()) return;
  if (ObservMode || g_GameMode == GameMode::TrophyMode || InTrophyRoomMap()) return;
  if (CallLockTime) return;

  CallLockTime=1024*3;

  NextCall+=(RealTime % 2)+1;
  NextCall%=3;

  AddVoicev(fxCall[TargetCall-10][NextCall].length,
            fxCall[TargetCall-10][NextCall].lpData.data(), 256);

  //multiplayer
  sendHunterCall = TargetCall - 10;
  sendHunterCallType = NextCall;

  // NOTE: float-first arithmetic is load-bearing here. (512*256)^2 overflows
  // int32 (it is exactly 4*2^32 and wraps to 0), which made dSq<dminSq false
  // for every dino and silently disabled all call answers (issue #1).
  float dminSq = 512.f * 256.f * 512.f * 256.f;
  int ai = -1;

  for (int c=0; c<ChCount; c++)
  {
    TCharacter *cptr = &Characters[c];

	float dx = PlayerX - cptr->pos.x;
	float dy = PlayerY - cptr->pos.y;
	float dz = PlayerZ - cptr->pos.z;
	float dSq = dx * dx + dy * dy + dz * dz;
	float hearRange = (ctViewR * 400) * (DinoInfo[cptr->CType].HearK * 2);
	bool canHear = dSq < hearRange * hearRange;

	if (DinoInfo[cptr->CType].fearCall[TargetCall-10] && canHear
		&& DinoInfo[cptr->CType].Clone != AI_DIMOR && DinoInfo[cptr->CType].Clone != AI_PTERA
		&& DinoInfo[cptr->CType].Clone != AI_BRACH
		) { //ai that cannot flee, state always 0
		cptr->State = 2;
		cptr->AfraidTime = (10 + rRand(5)) * 1024;
	}

	/*
    if (DinoInfo[AI_to_CIndex[TargetCall] ].DangerCall)
      if (cptr->AI<10)
      {
        cptr->State=2;
        cptr->AfraidTime = (10 + rRand(5)) * 1024;
      }
	  */

	if (DinoInfo[cptr->CType].menuDino != TargetCall-10) continue;
	if (cptr->AfraidTime) continue;
    if (cptr->State) continue;

    
    if (canHear)
    {
      if (rRand(128) > 32)
        if (dSq<dminSq)
        {
          dminSq = dSq;
          ai = c;
        }
      cptr->tgx = PlayerX + siRand(1800);
      cptr->tgz = PlayerZ + siRand(1800);
    }
  }

  if (ai!=-1)
  {
    answpos = SubVectors(Characters[ai].pos, PlayerPos);
    answpos.x/=-3.f;
    answpos.y/=-3.f;
    answpos.z/=-3.f;
    answpos = SubVectors(PlayerPos, answpos);
    answtime = 2000 + rRand(2000);
    answcall = TargetCall;
  }

}
static void GetConfigPath(char* buf, size_t bufsz)
{
  // Try EXE directory first
  char mod[MAX_PATH];
  std::uint32_t len = GetModuleFileNameA(nullptr, mod, sizeof(mod));
  if (len > 0 && len < sizeof(mod)) {
    char* sep = strrchr(mod, '\\');
    if (sep) {
      *(sep + 1) = '\0';
      strcat_s(mod, sizeof(mod), "config.cfg");
      if (GetFileAttributesA(mod) != INVALID_FILE_ATTRIBUTES) {
        strcpy_s(buf, bufsz, mod);
        return;
      }
    }
  }
  // Fall back to CWD
  strcpy_s(buf, bufsz, "config.cfg");
}

// Create a default config.cfg with all available settings documented.
// Called when no config file exists (first launch or manual deletion).
static void CreateDefaultConfig()
{
  char configPath[MAX_PATH];

  // Prefer EXE directory (shared with Carnivores2Menu), fall back to CWD
  char mod[MAX_PATH];
  std::uint32_t len = GetModuleFileNameA(nullptr, mod, sizeof(mod));
  char* writePath = configPath;
  if (len > 0 && len < sizeof(mod)) {
    char* sep = strrchr(mod, '\\');
    if (sep) {
      *(sep + 1) = '\0';
      strcat_s(mod, sizeof(mod), "config.cfg");
      writePath = mod;
    }
  }
  if (writePath == configPath) {
    strcpy_s(configPath, sizeof(configPath), "config.cfg");
  }

  // Don't overwrite if file already exists (e.g. created by menu)
  if (GetFileAttributesA(writePath) != INVALID_FILE_ATTRIBUTES) {
    return;
  }

  HANDLE hfile = CreateFileA(writePath, GENERIC_WRITE, 0, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hfile == INVALID_HANDLE_VALUE) {
    return;
  }

  // Build the default config content (kept in sync with the
  // clean config.cfg generated by tools/release/make-c2-package.ps1).
  char buf[4096];
  int len2 = snprintf(buf, sizeof(buf),
    "# Carnivores 2 Modder's Engine - Configuration\r\n"
    "# This file is auto-generated on first launch.\r\n"
    "# Edit values as needed — they are validated on load.\r\n"
    "#\r\n"
    "# The menu updates the settings it owns when you change them.\r\n"
    "# Comments and other keys (including audio overrides) are preserved.\r\n"
    "\r\n"
    "# Renderer: 0=Software, 1=OpenGL (default: 1)\r\n"
    "renderer 1\r\n"
    "\r\n"
    "# Field of view in degrees (default: 62, range: 35-90)\r\n"
    "fov %d\r\n"
    "\r\n"
    "# Object detail / view distance (default: 48, range: 24-96)\r\n"
    "object_detail %d\r\n"
    "\r\n"
    "# FPS limit: 0=Unlimited, 1=60, 2=120, 3=240 (default: 1)\r\n"
    "fps_limit 1\r\n"
    "\r\n"
    "# Verbose logging: 0=off, 1=on (default: 0)\r\n"
    "verbose_logging 0\r\n"
    "\r\n"
    "# Nightvision key VK code (default: 78 = 'N')\r\n"
    "nightvision_key 78\r\n"
    "\r\n"
    "# Window resolution, e.g. 1920x1080. Overrides the per-profile\r\n"
    "# resolution saved in trophy0N.sav. Omit to use the profile value.\r\n"
    "# The menu writes this automatically when you change Resolution.\r\n"
    "#resolution 1920x1080\r\n"
    "\r\n"
    "# Display mode: 0=windowed, 1=exclusive fullscreen, 2=borderless\r\n"
    "# fullscreen (default: 2). The menu writes this when you change the\r\n"
    "# Display Mode video option.\r\n"
    "display_mode 2\r\n"
    "\r\n"
    "# GPU features bitmask (default: all optimizations enabled)\r\n"
    "# Set to 0 to disable all GPU optimizations.\r\n"
    "gpufeatures %u\r\n"
    "\r\n"
    "# GL performance logging: 0=off, 1=on (default: 0)\r\n"
    "# Requires GL_PERF_HOOKS build. Writes glperf-*.log files.\r\n"
    "glperf_logging 0\r\n"
    "\r\n"
    "# Audio reverb tuning (OpenAL EFX only; restart the game to apply).\r\n"
    "# envN: 0=Generic, 1=Plate, 2=Forest, 3=Mountain, 4=Canyon,\r\n"
    "# 5=Cave, 6=Special-1, 7=intentional no-op, 8=Underwater.\r\n"
    "# decay: seconds [0.1,20]; decayhf: HF decay ratio [0.1,2];\r\n"
    "# diffusion: [0,1]; reverb: late-reverb level in mB [-10000,0].\r\n"
    "# Uncomment to override; commented lines use compiled defaults\r\n"
    "# (Generic: decay=1.49, decayhf=0.3, diffusion=0.4, reverb=-620).\r\n"
    "#env0_decay 1.49\r\n"
    "#env0_decayhf 0.3\r\n"
    "#env0_diffusion 0.4\r\n"
    "#env0_reverb -620\r\n"
    "\r\n"
    "# Last hunt setup (written by the menu when you launch a hunt).\r\n"
    "# The menu reselects these on the hunt screen; delete them to\r\n"
    "# return to entry defaults.\r\n",
    kFovDefault,
    kObjectDetailDefault,
    kGpuFeaturesDefault
  );

  std::uint32_t written = 0;
  Platform::WriteFile(hfile, buf, (std::uint32_t)len2, &written);
  CloseHandle(hfile);

  char msg[MAX_PATH + 64];
  snprintf(msg, sizeof(msg), "Config: Created default config.cfg at %s\n", writePath);
  PrintLog(msg);
}

static void LoadConfig()
{
  char configPath[MAX_PATH];
  GetConfigPath(configPath, sizeof(configPath));

  std::ifstream input(configPath, std::ios::binary);
  if (!input) {
    PrintLog("Config: config.cfg not found, using defaults.\n");
    return;
  }

  std::string text;
  size_t nulBytes = 0;
  if (!ReadConfigText(input, text, nulBytes)) {
    PrintLog("Config: unreadable, unsupported encoding, or larger than 1 MiB; using defaults.\n");
    return;
  }
  if (nulBytes) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Config: recovered %u NUL padding bytes; please resave config.cfg.\n",
              static_cast<unsigned>(nulBytes));
    PrintLog(msg);
  }

  // Simple line-by-line parser: "key value"
  char* ctx = nullptr;
  char* line = strtok_s(text.data(), "\r\n", &ctx);
  while (line) {
    // Skip comments and empty lines
    if (line[0] == '#' || line[0] == '\0') {
      line = strtok_s(nullptr, "\r\n", &ctx);
      continue;
    }

    char key[64];
    char keyval[64] = "";
    int tokens = sscanf_s(line, "%63s %63s", key, (unsigned)sizeof(key), keyval, (unsigned)sizeof(keyval));
    if (tokens >= 1) {
      if (tokens < 2) {
        char msg[96];
        snprintf(msg, sizeof(msg), "Config: '%s' missing value, ignoring.\n", key);
        PrintLog(msg);
      } else {
      int value = (int)strtol(keyval, nullptr, 10);
      if (LegacyText::Compare(key, "fov") == 0) {
        if (value >= kFovMin && value <= kFovMax) {
          OptFov = value;
        } else {
          char msg[128];
          snprintf(msg, sizeof(msg), "Config: fov %d out of range [%d..%d], ignoring.\n",
                    value, kFovMin, kFovMax);
          PrintLog(msg);
        }
      }
      else if (LegacyText::Compare(key, "object_detail") == 0) {
        if (value >= kObjectDetailMin && value <= kObjectDetailMax) {
          OptObjectDetail = value;
        } else {
          char msg[128];
          snprintf(msg, sizeof(msg), "Config: object_detail %d out of range [%d..%d], ignoring.\n",
                    value, kObjectDetailMin, kObjectDetailMax);
          PrintLog(msg);
        }
      }
      else if (LegacyText::Compare(key, "fps_limit") == 0) {
        // 0=unlimited, 1=60, 2=120, 3=240
        if (value >= 0 && value <= 3) {
          OptFpsLimit = value;
        }
      }
      else if (LegacyText::Compare(key, "verbose_logging") == 0) {
        g_VerboseLogging = (value != 0);
      }
      else if (LegacyText::Compare(key, "nightvision_key") == 0) {
        NightVisionKey = value;
      }
      else if (LegacyText::Compare(key, "gpufeatures") == 0) {
        // Runtime GPU-optimization kill-switch bitmask (see GpuFeature in GameState.h).
        // 0 disables all new GPU optimizations; bits toggle features individually.
        // Parse as unsigned: the default (all bits set) exceeds INT_MAX, so
        // atoi/strtol would clamp and could never restore the default.
        char *end = nullptr;
        unsigned long v = strtoul(keyval, &end, 10);
        if (end != keyval) g_gpuFeatures = static_cast<uint32_t>(v);
      }
      else if (LegacyText::Compare(key, "glperf_logging") == 0) {
        // Runtime toggle for GL performance harness logging.
        // Only effective when GL_PERF_HOOKS is compiled in.
        // 0=disabled (default), 1=enabled
        g_glperfLoggingEnabled = (value != 0);
        {
          char msg[64];
          snprintf(msg, sizeof(msg), "Config: glperf_logging = %d\n", g_glperfLoggingEnabled ? 1 : 0);
          PrintLog(msg);
        }
      }
      else if (LegacyText::Compare(key, "resolution") == 0) {
        // Override the saved profile resolution. Format: WxH, e.g. "1920x1080".
        // Applies after SetupRes() (trophy file) so config.cfg wins over the
        // per-profile legacy setting; a command-line -res= still overrides this.
        int w = 0, h = 0;
        char sep = 0;
        if (sscanf_s(keyval, "%d%c%d", &w, &sep, (unsigned)sizeof(sep), &h) == 3 &&
            (sep == 'x' || sep == 'X')) {
          if (w > 0 && h > 0) {
            // Sync OptRes when the size exists in the enumerated list so the
            // legacy index stays meaningful; WinW/H always win regardless.
            // Reset first: an exotic size must not leave a stale profile index.
            OptRes = -1;
            for (int r = 0; r < ResCount; r++) {
              if (ResolutionList[r].w == w && ResolutionList[r].h == h) { OptRes = r; break; }
            }
            WinW = w;
            WinH = h;
          } else {
            PrintLog("Config: resolution values must be positive, ignoring.\n");
          }
        } else {
          PrintLog("Config: resolution expects WxH (e.g. 1920x1080), ignoring.\n");
        }
      }
      else if (LegacyText::Compare(key, "display_mode") == 0) {
        // Display mode: 0=windowed, 1=exclusive fullscreen, 2=borderless.
        // Written by the menu's Display Mode video option. Command-line
        // flags (-windowed/-fullscreen/-borderless) override this later
        // in ProcessCommandLine().
        if (value >= 0 && value <= 2) {
#ifdef _soft
          // The software renderer has no borderless presentation; map its
          // "borderless" pick to exclusive fullscreen (classic behaviour).
          if (value == 2) value = 1;
#endif
          FULLSCREEN = (value == 1);
          BORDERLESS = (value == 2);
        } else {
          PrintLog("Config: display_mode must be 0 (windowed), 1 (fullscreen) or 2 (borderless), ignoring.\n");
        }
      }
      else if (LegacyText::Compare(key, "env", 3) == 0) {
        // Runtime reverb preset tuning: env<0-8>_<decay|decayhf|diffusion|reverb>.
        // Floats (e.g. env0_decay 1.49). Unset fields read compiled defaults;
        // out-of-range values are rejected with a log line. room/envID have
        // no keys — the EFX path does not consume them.
        // Require one preset digit and consume the entire numeric token.
        // Parsing after the case-insensitive prefix also permits ENV0_decay.
        const int env = key[3] - '0';
        const char* field = strlen(key) >= 5 ? key + 5 : "";
        char* end = nullptr;
        errno = 0;
        const float f = std::strtof(keyval, &end);
        if (env >= 0 && env <= 8 && key[4] == '_' &&
            end != keyval && *end == '\0' && errno != ERANGE) {
          int fi = -1;
          if (LegacyText::Compare(field, "decay") == 0) fi = 0;
          else if (LegacyText::Compare(field, "decayhf") == 0) fi = 1;
          else if (LegacyText::Compare(field, "diffusion") == 0) fi = 2;
          else if (LegacyText::Compare(field, "reverb") == 0) fi = 3;
          if (fi >= 0 && Audio_SetEnvParam(env, fi, f)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "Config: env%d_%s = %.3g\n", env, field, (double)f);
            PrintLog(msg);
          } else {
            // Two tokens of up to 63 chars plus the fixed diagnostic.
            char msg[256];
            snprintf(msg, sizeof(msg), "Config: '%s %s' invalid (env 0-8, known field, range), ignoring.\n", key, keyval);
            PrintLog(msg);
          }
        } else {
          char msg[256];
          snprintf(msg, sizeof(msg), "Config: '%s' expects env<0-8>_<decay|decayhf|diffusion|reverb> value, ignoring.\n", key);
          PrintLog(msg);
        }
      }
      // Future settings: add else-if branches here
      } // tokens == 2
    }

    line = strtok_s(nullptr, "\r\n", &ctx);
  }

  PrintLog("Config Loaded (config.cfg).\n");
}
