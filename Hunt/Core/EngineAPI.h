// EngineAPI.h -- All game engine function declarations
// Extracted from Hunt.h (Phase 0.1 -- Split god header into focused headers)
#pragma once

#include "Core/ModelTypes.h"
#include "Core/RenderTypes.h"
#include "Core/GameTypes.h"
#include "Core/GameState.h"


void HLineTxB( void );
void HLineTxC( void );
void HLineTxGOURAUD( void );


void HLineTxModel25( void );
void HLineTxModel75( void );
void HLineTxModel50( void );

void HLineTxModel3( void );
void HLineTxModel2( void );
void HLineTxModel( void );

void HLineTDGlass75( void );
void HLineTDGlass50( void );
void HLineTDGlass25( void );
void HLineTBGlass25( void );


void SetVideoMode(int, int);
void SyncLegacyDisplayState(); // Publish engine configuration to renderer/SOFT adapters.
void SetFullScreen();
void CaptureMouse(std::int32_t);
void ResetMousePos();

void CreateDivTable();
void DrawTexturedFace();
#ifdef _WIN32
int GetTextW(HDC, const char*);
#endif
void wait_mouse_release();

void ShowControlElements();
void InsertModelList(TModel* mptr, float x0, float y0, float z0, int light, float al, float bt);
void RenderGround();
#ifdef _gl
void RenderProjectedShadows();
#endif
void RenderWater();
void RenderElements();
void CreateChRenderList();
void RenderModelsList();
void ProcessMap  (int x, int y, int r);
void ProcessMap2 (int x, int y, int r);
void ProcessMapW (int x, int y, int r);
void ProcessMapW2(int x, int y, int r);

void DrawTPlane(std::int32_t);
void DrawTPlaneClip(std::int32_t);
void ClearVideoBuf();
// Phase 5E follow-up: clear renderer-side per-level texture caches before
// LoadResources loads new models. Only the GL renderer currently has such
// caches (m_modelTextureCache / m_bmpTextureCache keyed by TModel*); the
// other renderers implement this as a no-op.
void ClearRendererLevelCache();
void ClearRendererTerrainCache();
void ReleaseModelTexture(const TModel* mptr);
void DrawScoreText(int, int);
void DrawTrophyText(int, int);
void DrawSurvivalText(int, int);
void DrawHMap();
void RenderCharacter(TCharacter*);
void RenderShip();
void RenderSShip();
void RenderBag();
void RenderBullet(int);
void RenderPlayer(int);
void RenderSkyPlane();
void DrawScene();
void DrawPostObjects();
void RenderHealthBar();
void Render_Cross(int, int);
void Render_LifeInfo(int);

void RenderModelClipEnvMap(TModel*, float, float, float, float, float);
void RenderModelClipPhongMap(TModel*, float, float, float, float, float);

void RenderModel         (TModel*, float, float, float, int, int, float, float);
void RenderBMPModel      (TBMPModel*, float, float, float, int);
void RenderModelClipWater(TModel*, float, float, float, int, int, float, float);
void RenderModelClip     (TModel*, float, float, float, int, int, float, float);
void RenderNearModel     (TModel*, float, float, float, int, float, float);
void DrawPicture         (int x, int y, TPicture &pic);
void DrawScaledPicture   (int x, int y, int w, int h, TPicture &pic);
void DrawFlash		 (int x, int y, int w, int h, TPicture &pic);

void InitClips();
void InitDirectDraw();
void WaitRetrace();

void Characters_AddSecondaryOne(TCharacter *cptr);
void AddDeadBody(TCharacter *cptr, int, bool);
void PlaceCharacters();
void PlaceCharactersSurvival();
void PlaceMHunters(); //multiplayer
void PlaceTrophy();
void AnimateCharacters();
void AnimateMHunters(); //multiplayer
void MakeNoise(Vector3d, float);
void CheckAfraid();
void CreateChMorphedModel(TCharacter* cptr);
void CreateMorphedObject(TModel* mptr, TVTL &vtl, int FTime);
void CreateMorphedModel(TModel* mptr, TAni *aptr, int FTime, float scale);
void CreateMorphedModelBetaGamma(TModel* mptr, TAni *aptr, int FTime, float scale, float beta, float gamma);


void CalcLights  (TModel* mptr);
void CalcModelGroundLight(TModel *mptr, float x0, float z0, int FI);
void CalcNormals (TModel* mptr, Vector3d *nvs);
void CalcGouraud (TModel* mptr, Vector3d *nvs);

void CalcPhongMapping(TModel* mptr, Vector3d *nv);
void CalcEnvMapping(TModel* mptr, Vector3d *nv);

void CalcBoundBox(TModel* mptr, TBound *bound);
void  NormVector(Vector3d&, float);
float SGN(float);
void  DeltaFunc(float &a, float b, float d);
void  MulVectorsScal(const Vector3d&, const Vector3d&, float&);
void  MulVectorsVect(const Vector3d&, const Vector3d&, Vector3d&);
Vector3d SubVectors( Vector3d&, Vector3d& );
Vector3d AddVectors( Vector3d&, Vector3d& );
Vector3d RotateVector(Vector3d&);
float VectorLength(Vector3d);
float VectorLengthSq(Vector3d);
int   siRand(int);
int   rRand(int);
void  CalcHitPoint(CLIPPLANE&, Vector3d&, Vector3d&, Vector3d&);
void  ClipVector(CLIPPLANE& C, int vn);
float FindVectorAlpha(float, float);
float AngleDifference(float a, float b);

int   TraceShot(float ax, float ay, float az,
                float &bx, float &by, float &bz,
	bool,bool);
int   TraceLook(float ax, float ay, float az,
                float bx, float by, float bz);


void CheckCollision(float&, float&);
// cachedFogIndex >= 0 reuses the map-cell lookup made by a caller that is
// already walking the terrain grid; -1 preserves the general-purpose path.
float CalcFogLevel(Vector3d v, int cachedFogIndex = -1);
void AddMessage(const char* mt);
void CreateTMap();


void LoadSky();
void LoadSkyMap();
void LoadTexture(unique_obj_ptr<TEXTURE>&);
void LoadWav(char* FName, TSFX &sfx);


void ApplyAlphaFlags(std::uint16_t*, int);
std::uint16_t conv_565(std::uint16_t c);
int  conv_xGx(int);
void conv_pic(TPicture &pic);
void LoadPicture(TPicture &pic, const char* pname, MemoryTag tag = MemoryTag::Global);
void LoadPictureTGA(TPicture &pic, const char* pname, MemoryTag tag = MemoryTag::Global);
void LoadCharacterInfo(TCharacterInfo&, char*, MemoryTag tag = MemoryTag::Global);
void LoadModelEx(unique_obj_ptr<TModel> &mptr, char* FName, MemoryTag tag = MemoryTag::Global);
void LoadModel(unique_obj_ptr<TModel> &mptr, MemoryTag tag = MemoryTag::Level);
void LoadResources();
void ReleaseResources();
void ReleaseGlobalResources();
void ReleaseCharacterInfo(TCharacterInfo &chinfo);
void ReleaseModel(unique_obj_ptr<TModel> &mptr);
// Idempotent release of TModel's raw gFace / VLight[0..3] backing blocks
// (ModelLoader.cpp). Every drop path must call this before releasing the
// TModel shell so the two never drift apart again.
void ReleaseModelBuffers(TModel* mptr);
void ReInitGame();



void SaveScreenShot();

#ifdef GL_PERF_HOOKS
// F11 key handler: triggers a 1-second per-frame GL perf CSV capture
// (glperf-frame.csv in the working directory). No-op when the GL perf
// harness is not compiled in.
void PerfTriggerCapture();

// Frame boundary hooks for the GL perf harness. PerfFrameBegin() is called at
// the start of Hunt.cpp::DrawScene(); PerfFrameEnd() runs in ShowVideo() after
// post-processing and before SwapBuffers. This brackets one rendered frame
// without measuring presentation/vsync wait. No-op when disabled.
void PerfFrameBegin();
void PerfFrameEnd();



#endif
// === TERRAIN QUERIES ===
float GetLandOH(int x, int y);
float GetLandH(float x, float y);
float GetLandUpH(float x, float y);
float GetLandCeilH(float CameraX, float CameraZ);
float GetLandQH(float CameraX, float CameraZ);
float GetLandHObj(float CameraX, float CameraZ);
float GetLandQHNoObj(float CameraX, float CameraZ);
float GetLandLt(float x, float y);
bool waterNear(float x, float y, float maxDist);
void CalcModelGroundLight(TModel *mptr, float x0, float z0, int FI);
std::int32_t PointOnBound(float &H, float px, float py, float cx, float cy, float oy, TBound *bound, int angle);

// === SHIP SYSTEM ===
void AddWCircle(float x, float z, float scale);
void SubmitDinoScore(int cindex);
void AddShipTask(int cindex);
void AddShipSupply(float tx, float tz);
void InitShip(int cindex);

// === COMMAND LINE ===
void ProcessCommandLine();

// === GAME CONTROLS / MOVEMENT (Hunt split) ===
void CaptureMouse(std::int32_t capture);
void ResetMousePos();
void SwitchMode(const char* lps, std::int32_t& b);
void ChangeViewR(int d1, int d2, int d3);
void ChangeCall();
void ToggleBinocular();
void ToggleRunMode();
void ToggleCrouchMode();
void ToggleMapMode();
void ShowShifts();
void ProcessControls();
void ProcessDemoMovement();

void HideWeapon();
void ProcessReload();
void ProcessFireMode();
void ProcessPump();
void ProcessShoot();
void ProcessSlide();
void ProcessPlayerMovement();

void AddShadowCircle(int x, int y, int R, int D);
void PreCashGroundModel();


void CreateWaterTab();
void CreateFadeTab();
void CreateVideoDIB();
void CreateVideoDIB(int W, int H);
void RenderLightMap();

void MulVectorsVect(const Vector3d& v1, const Vector3d& v2, Vector3d& r );
void MulVectorsScal(const Vector3d& v1, const Vector3d& v2, float& r);
Vector3d SubVectors( Vector3d& v1, Vector3d& v2 );
Vector3d SubVectors2d(Vector3d& v1, Vector3d& v2);
void NormVector(Vector3d& v, float Scale);

#ifdef MEM_DEBUG
// Park the Memory.h call-site macro while declaring the underlying
// functions (see Resources.cpp for the matching guard around the
// definitions).
#pragma push_macro("_HeapAlloc")
#undef _HeapAlloc
#endif
[[nodiscard]] void* _HeapAlloc(Platform::HeapHandle hHeap, std::uint32_t dwFlags, size_t bytes);
// Phase 5A: 4-arg overload with MemoryTag dispatch. No default for `tag` —
// MSVC's overload resolution treats a 3-arg call as ambiguous between this
// overload (using the default) and the 3-arg overload above, so the tag
// must be explicit. The 3-arg forwarder in Resources.cpp routes legacy
// 3-arg calls through this overload with MemoryTag::Global — the safe
// default that keeps untagged allocations on the persistent heap where
// LevelArena->Reset() cannot invalidate them. (Changed from MemoryTag::Level
// in Phase 5C.2 after untagged GL allocations caused arena corruption.)
// Migration phases 5B-5E updated individual call sites to the explicit
// 4-arg form with the appropriate tag (Global for session-lifetime, Level
// for per-level).
[[nodiscard]] void* _HeapAlloc(Platform::HeapHandle hHeap, std::uint32_t dwFlags, size_t bytes, MemoryTag tag);
#ifdef MEM_DEBUG
#pragma pop_macro("_HeapAlloc")
#endif
[[nodiscard]] std::int32_t _HeapFree(Platform::HeapHandle hHeap, std::uint32_t dwFlags, void* lpMem);

// Phase 5A: per-level arena. Constructed in InitEngine() and destroyed in
// ShutDownEngine() (both Phase 5C). nullptr in Phase 5A — _HeapAlloc
// checks for nullptr and falls through to HeapAlloc, so pre-5A call
// sites are bit-for-bit unaffected.

//============ game ===========================//
float GetLandCeilH(float, float);
float GetLandH(float, float);
float GetLandOH(int, int);
float GetLandLt(float, float);
float GetLandUpH(float, float);
float GetLandQH(float, float);
float GetLandQHNoObj(float, float);
float GetLandHObj(float, float);
bool waterNear(float, float, float);

void LoadResourcesScript();
void InitEngine();
void ShutDownServer();
void ShutDownClient();
void ShutDownEngine();
void ProcessSyncro();
void AddShipSupply(float,float);
void AddShipTask(int);
void SubmitDinoScore(int);
void LoadTrophy();
void SaveTrophy();
void RemoveCurrentTrophy();
void MakeCall();
void AddBullet(float ax, float ay, float az,
              float bx, float by, float bz,
			  float blx, float bly, float blz,
	int, bool);
int AnimateBullet(float ax, float ay, float az,
	float bx, float by, float bz, int b);
void AnimateBullets();
void refillWeapons(bool);
void registerDamage(int, bool);

void AddBloodTrail(TCharacter *cptr);

void AnimateBloodTrails();
void AddElements(float, float, float, int, int);
void AddElementsA(float, float, float, int, int, int, bool, float);
void AddWCircle(float, float, float);
void AnimateProcesses();
[[noreturn]] void DoHalt(const char*);
[[noreturn]] void DoHalt2(const char*);

void CreateLog();
void PrintLog(const char* l);
void PrintLogVerbose(const char* l);
void CloseLog();



//========== multiplayer =============//






void StartupServerCommsThread();
void StartupClientCommsThread();




//========== common ==================//


//========== map =====================//





// Phase 5B.2: Textures is now std::array<unique_obj_ptr<TEXTURE>, 1024>.
// std::array is used (not a raw C array) so the size is fixed at
// compile time (matching the original 1024-element behavior) and the
// type system enforces it. All elements default-construct to null
// unique_ptrs, matching the old BSS zero-initialization. Access
// pattern is unchanged: Textures[t] returns a unique_obj_ptr<TEXTURE>&
// that supports operator bool, operator->, and .reset() the same way
// a raw TEXTURE* did.

//========= WEATHER =================//


//========= GAME ====================//

//firing mode 0-semiauto 1-fullauto





// Score multipliers for accessories. Defaults are set in Hunt/Game.cpp
// InitEngine() and match the legacy hardcoded values from
// SubmitDinoScore() so a hunt launched without a Menu-supplied 'smod='
// argument behaves identically to the original game. The Menu passes
// 'smod=camo,radar,scent,double,tranq,observer' in the same order to
// override these from _RES.TXT (see Menu/Resources.cpp ReadAccessories()).






//======== MODEL ======================//






//Add these after dino positions alligned










//========== Render ==================//



























//#define AI_FINAL	  29 //Last AI of max huntable roster (menu can only display 10)

//#define AI_POACHER    22















//========== for audio ==============//
void  AddVoicev  (int, short int*, int);
void  AddVoice3dv(int, short int*, float, float, float, int);
void  AddVoice3d (int, short int*, float, float, float);

void SetAmbient3d(int, short int*, float, float, float);
void SetAmbient(int, short int*, int);
bool IsAmbient3dOwner(short int* lpdata); // true if the shared looping channel currently plays this sample
bool IsAmbient3dFree();                   // true if the shared looping channel is idle
void AudioSetCameraPos(float, float, float, float, float);
void InitAudioSystem(int);
void Audio_Restore();
void AudioStop();
void Audio_Shutdown();
void Audio_SetEnvironment(int, float);
bool Audio_SetEnvParam(int env, int field, float v);  // config.cfg envN_* tuning
void Audio_UploadGeometry();


//========== for 3d hardware =============//
void ShowVideo();
void Init3DHardware();
void Activate3DHardware();
void ShutDown3DHardware();
void Render3DHardwarePosts();
void CopyBackToDIB();
void CopyHARDToDIB();
void Hardware_ZBuffer(std::int32_t zb);
void AllocateRenderTables(void);

void EnumerateResolutions();

//=========== loading =============
void StartLoading();
void EndLoading();
void PrintLoad(char *t);


