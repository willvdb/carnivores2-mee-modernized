// GameTypes.h -- Gameplay-related type definitions
// Extracted from Hunt.h (Phase 0.1 -- Split god header into focused headers)
#pragma once

#include "Core/ModelTypes.h"
#include "Core/RenderTypes.h"
#include <cstdint>


struct TCharacterInfo
{
  char ModelName[32];
  int AniCount,SfxCount;
  // Phase 5B.2: mptr is now unique_obj_ptr<TModel>. The model is
  // freed (via ~TModel + _HeapFree) automatically when the
  // TCharacterInfo is destroyed or when mptr is reset. TModel now
  // has a move ctor (added above) so this works with the smart
  // pointer.
  unique_obj_ptr<TModel> mptr;
  TAni Animation[64];
  TSFX SoundFX[64];
  int  Anifx[64];
};


struct TWeapon
{
  TCharacterInfo chinfo[10];
  TPicture       BulletPic[10];
  TPicture       ChambPic[10];
  TCharacterInfo Bullet[10];
  TPicture		 Flash[4];
  int FlashP;

  // Phase 5B.2: normals is now unique_heap_ptr<Vector3d[]>. Allocated
  // per-level in LoadResources (sized by maxWeaponVCount) and freed
  // automatically when the TWeapon is destroyed. Tagged as Level
  // (per-level) so it recycles with the arena in Phase 5C.
  unique_heap_ptr<Vector3d[]> normals;
  int state, FTime;
  float shakel;
  float breath;
  int BTime;
  bool HoldBreath;
  int breathPressed;
  int ammoIn;
};


struct TBullet
{
	float fallTotal;
	unsigned char aqState; //0 land //1 aqua //2 min
	Vector3d a,dif,ldif,rpos,orig;
	int parent, state;
	int FTime, RTime;
	float alpha, beta;
	bool Danger;//damage hunter
	bool cDanger;//damage creature
	bool enemy;//damage creature
//	float power, speed, fall;
};


struct TWCircle
{
  Vector3d pos;
  float scale;
  int FTime;
};


struct TSnowType  {
	int snow_vSpd;//vertical
	int snow_hSpd;//horizontal
	int snow_dens;//density

	unsigned char snow_r, snow_g, snow_b, snow_a;
	float snow_rad;//radius
	int addr; //start address in snow particle array
	int SnCount;//total number of snow particles
};


struct TSnowElement  {
	Vector3d pos;
	float hl, ftime;
};


struct TCharacter
{
  int CType, Clone;
  TCharacterInfo *pinfo;
  int StateF;
  int State;
  int NoWayCnt, NoFindCnt, AfraidTime, tgtime;
  int PPMorphTime, PrevPhase,PrevPFTime, Phase, FTime;

  int currentIdleGroup;
  int currentIdle2Group;

  float vspeed, rspeed, bend, scale;
  int Slide;
  float slidex, slidez;
  float tgx, tgz;

  Vector3d pos, rpos;
  float tgalpha, alpha, beta,
        tggamma,gamma,
        lookx, lookz;
  int Health, BloodTime, BloodTTime;

  //ICTH
  bool gliding = false;
  //  bool wingUp = false;
  bool notFlushed = false;
  int deathPhase;
  bool canSleep = false;
  float shakeTime = 0;
  float spawnAlt;

  //MOSA
  float depth, tdepth;
  float bdepth = 0;//bend
  float lastTBeta = 0;
  float turny = 0;

  int spcDepth;

  int SpawnGroupType;

  int packId;
  bool followLeader;

  int killType;
  int deathType;
  int roarAnim;
  int waterDieAnim;

  int dogPrey; // used by dog only. The dino currently being tracked

  bool awareHunter;
  bool heardShot;

  bool aquaticIdle;

  int tropAnim;

  int xdata, zdata, ydata;

  bool animateTrophy;

  int _PhaseM;

  Vector3d climbable;
  float climbY;
  BOOL gottaClimb;

  Vector3d sonar;
  BOOL showSonar;

  bool cpcpAquatic;//checkplacecollisionaquatic - can spawn in water,brach,icth,mosa,fish

//  int tropIndex;

  float packDensity;

  int tracker;
  int RTime;

  int tempScore; //stores killed stats
  int tempTime;
  int tempDate;
  float tempRange;
  bool claimed;

  //poacher
  int ammo;

};


struct TPlayer
{
  BOOL Active;
  unsigned int IPaddr;
  Vector3d pos;
  float alpha, beta, vspeed;
  int kbState;
  char NickName[16];
};


struct TDemoPoint
{
  Vector3d pos;
  int DemoTime, CIndex;
};


struct TLevelDef
{
  char FileName[64];
  char MapName[128];
  DWORD DinosAvail;
  WORD *lpMapImage;
};


struct TShipTask
{
  int tcount;
  int clist[255];
};


struct TShip
{
  Vector3d pos, rpos, tgpos, retpos;
  float alpha, tgalpha, speed, rspeed, DeltaY, beta, gamma, gspeed, bspeed;
  int State, cindex, FTime;
};


struct TBag
{
	Vector3d pos, rpos;
	int State;
	int FTime;
};


struct THitBox
{
	Vector3d pos, rpos;
	float alpha;
	int phase;
};


struct TLandingList
{
  int PCount;
  Vector2di list[64];
};


struct TPlayerR
{
  char PName[128];
  int  RegNumber;
  int  Score, Rank;
};


struct TTrophyItem
{
  int ctype, weapon, phase,
      height, weight, score,
      date, time;
  float scale, range;
  int r1, r2, r3, r4;
};


struct TStats
{
  int smade, success;
  float path, time;
};


struct TTrophyRoom
{
  char PlayerName[128];
  int  RegNumber;
  int  Score, Rank;

  TStats Last, Total;

  TTrophyItem Body[TROPHY_COUNT];
};


struct TTrophyItem2  //Add neccesary stuff here! (later, not now)
{
	int ctype, weapon, phase,
		height, weight, score,
		date, time;
	float scale, range;
	int r1, r2, r3, r4;
};


struct TTrophyRoom2
{
	int versionID;
	int survivalHighScore;
	TTrophyItem2 Body[TROPHY2_COUNT];
};


struct TDinoKill
{
	int anim;
	int offset;
	int hunteranim;
	int hunterswimanim;
	BOOL elevate, carryCorpse;
	BOOL dontloop;
	BOOL scream;
};


struct TTrophyType
{
	int group = -1;
	int ctype[TROPHY2_COUNT];
	int ctypeCh = 0;
	int xoffset, yoffset, zoffset;
	int xoffsetScale, yoffsetScale, zoffsetScale;
	int xdata, ydata, zdata;
	int alpha, beta, gamma; //degrees
	int anim;
	int trophyPos;
	bool playAnim;
};


struct TPackMember
{
	int ctype;
	float ratio;
};


struct TPackMember2
{
	int packGroup;
	float ratio;
};


struct TSpawnInfo
{
	int spawnGroup;//, spawnMax;
	float spawnRatio;
};


struct TPackType
{
	TSpawnInfo SpawnInfo[32];
	TPackMember packMember[32];
	int packMemberCh = 0;
	int SpawnInfoCh = 0;
	int packMax, packMin;
	float packDensity;
};


struct TDinoDeathType
{
	int die;
	int sleep;
	int fall;
	bool nosleep;
};


struct TDinoIdleType
{
	int anim[32];
	int count;
	float start;
	float end;
	bool endOnAny;
	bool startOnAny;
	bool instantRepeat;
};


struct TDinoInfo
{
	int menuDino = -1;

  char Name[48], FName[48], PName[48];
  int Health0, Clone;
  float Mass, Length, Radius,
        SmellK, HearK, LookK,
        ShDelta;
  int   Scale0, ScaleA;
  float	  BaseScore;

  BOOL fearCall[64];
  BOOL Aquatic;
  int maxDepth, minDepth, spacingDepth;
  BOOL dontSwimAway;

  BOOL survivalDino;

  BOOL dontBend;
  //float bendOffset;

  float weaveRange;
  BOOL dontWeave;

  BOOL defensive;
  BOOL fearShot;
  BOOL fearHearShot;

  //BOOL noMoveNoRot;

  //int hunterDeathAnim, hunterDeathOffset;
  int aggress, killDist, flyDist;

  bool onRadar;
  float runspd, jmpspd, wlkspd, swmspd, flyspd, gldspd, tkfspd, lndspd, divspd;

  int maxGrad;
  float rotspdmulti;

//  int packMax, packMin;
//  float packDensity;

  int jumpRange;

  int runAnim, jumpAnim, walkAnim, swimAnim, flyAnim, diveAnim, glideAnim, takeoffAnim, landAnim,
	  slideAnim, shakeLandAnim, shakeWaterAnim, climbAnim, fireAnim;
  int reloadAnim = -1;

  TDinoDeathType deathType[32];
  int deathTypeCount;

  TDinoKill killType[32];
  int killTypeCount;

//  int tCounter;
//  int trophyCode;
//  int trophyLocTotal1;//CURRENTLY IN SAVE FILE
//  int trophyLocTotal2;//CURRENTLY IN SESSION - REPLACE WITH tlt1 UPON RESTART
 
  bool trophy = false;//counts the number of trophy slots
//  int tCounter; // used to count off trophy locs

  int waterDieAnim[32];
  int waterDieCount;

  TDinoIdleType idleGroup[32];
  int idleGroupCount;

  TDinoIdleType idle2Group[32];
  int idle2GroupCount;

 
  int lookAnim[32];//trex look
  int lookCount;
 
  int smellAnim[32]; //icth wateridle   trex smell
  int smellCount;
 

  int roarAnim[32];
  int roarCount;
 
  bool canSwim;
  int waterLevel;

  BOOL dogSmell;

  //bool trophySession;

  int partFrame1[50], partFrame2[50], partDist[50], partCnt[50], partMag[50], partOffset[50];
  bool partAngled[50], partCircle[50];

  bool DangerFish;
  bool TRexObjCollide;
  bool Mystery;
  bool HideBinoc;

  float camDemoPoint, camBase, camDemoPointWater, camBaseWater;

  float climbDist;

  unsigned char radarRed, radarGreen, radarBlue, bloodRed, bloodGreen, bloodBlue;
  WORD radarColour565, radarColour555;


  int SpawnInfoCh=0;
  TSpawnInfo SpawnInfo[32];

  int packMember2Ch = 0;
  TPackMember2 packMember2[32];

  //poacher
  int Weapon;
  int Reload;

};


struct TPack
{
	TCharacter *leader;
	bool alert;
	bool _alert;
	bool attack;
	bool _attack;
};


struct TAIInfo  {
	float targetDistance;
	int noWayCntMin;
	int noFindWayMed;
	int noFindWayRange;
	float targetBendRotSpd;
	float targetBendMin;
	float targetBendDelta1;
	float targetBendDelta2;
	float walkTargetGammaRot;
	float targetGammaRot;
	int idleStart;
	float yBetaGamma1, yBetaGamma2, yBetaGamma3, yBetaGamma4;

	int agressMulti;
	float tGAIncrement;
	int idleStartD;
	bool jumper;

	bool iceAge;
	bool carnivore;

	float rot1, rot2, pWMin; //weaveRange

	bool sniffer;

};


struct TSpawnRegion
{
	int XMax, YMax, XMin, YMin;
};


struct TSpawnGroup
{
	int SpawnMax, SpawnMin;
	float SpawnRate;
	bool moveForward, Randomised, OnlyActiveNearby, stayInRegion;
	int densityMulti;

	int spawnRegionCh, avoidRegionCh;
	TSpawnRegion spawnRegion[16];
	TSpawnRegion avoidRegion[16];

	int packIndexCh;
	int packIndex[128];
	int spawnInfoIndex[128];
};


struct TWeapInfo
{
	bool pic2b = false;
	bool picch = false;
  char Name[48], FName[48], BFName[48], CFName[48], BLName[48], SFXName[48];
  bool MGSSound = false;
  bool bullet = false;
  bool retrieve;
  float Power, Prec, Loud, Rate, Veloc, Fall;
  int Shots, TraceC, Reload, SFXIndex;
  int shtAnim = -1;
  int getAnim = -1;
  int putAnim = -1;
  int rldAnim = -1;
  int rldAnimPart = -1;
  int pmpAnim = -1;
  int modAnim = -1;
  int emptyAnim = -1;
  int getEmpAnim = -1;
  int putEmpAnim = -1;
  int shtAqSnd = -1;
  int getAqSnd = -1;
  int putAqSnd = -1;
  int rldAqSnd = -1;
  int rldAqSndPart = -1;
  int pmpAqSnd = -1;
  int modAqSnd = -1;
  float shake, Optic;
  bool unzoom, harpoon;
  // Rest unzoomed; reach Optic only while holding breath (stock rifle).
  // Default false (zero-init): scoped weapons rest at Optic instead.
  bool breathaim = false;

  bool canRun;
  bool cannotMortal;

  bool fullauto = false;
  bool semiauto = false;

  bool aqLow; //parent velocAq < veloc

  bool mustPump;
  bool autoPump;
  bool autoReload;

  float PowerAq = -1;
  float PrecAq = -1;
  float VelocAq = -1;
  float FallAq = -1;

  bool onRadar;
  unsigned char radarRed, radarGreen, radarBlue;
  WORD radarColour565, radarColour555;
  int radarTime;

  bool MuzzFlash, ChamFlash;
  bool cross;
  unsigned char crossRed, crossGreen, crossBlue;
  WORD crossColour565, crossColour555;

  int recoil;

};


struct TWaterEntity
{
  int tindex, wlevel;
  float transp;
  int fogRGB;
};


struct TWind
{
  float alpha;
  float speed;
  Vector3d nv;
};


struct TElement
{
  Vector3d pos, speed;
  int     Flags;
  float   R;
};


struct TElements
{
  int Type, ECount, EDone, LifeTime;
  int Param1, Param2, Param3;
  DWORD RGBA, RGBA2;
  Vector3d pos;
  TElement EList[32];
};


struct TBloodP
{
  int LTime;
  Vector3d pos;
  int Owner;
};


struct TBTrail
{
  int Count;
  TBloodP Trail[512];
};


struct TMenuDinoInfo
{
	TPicture CallIcon;
};

// Serialized records: fixed byte contracts on both Windows x86 and x64.
// Do not condition these checks on native pointer width.
static_assert(sizeof(TTrophyRoom)    == 1516, "TTrophyRoom size changed — trophy .sav binary compat break");
static_assert(sizeof(TTrophyItem)    == 56,   "TTrophyItem size changed — trophy item binary compat break");
static_assert(sizeof(TTrophyRoom2)   == 7176, "TTrophyRoom2 size changed — trophy .sab binary compat break");
static_assert(sizeof(TTrophyItem2)   == 56,   "TTrophyItem2 size changed — trophy item2 binary compat break");
static_assert(sizeof(TStats)         == 16,   "TStats size changed — statistics record");
static_assert(sizeof(TObjInfo)       == 64,   "TObjInfo size changed — object info binary compat");
static_assert(sizeof(TWaterEntity)   == 16,   "TWaterEntity size changed — water entity");

// Runtime objects with native pointers; no whole-object disk/wire consumers.
static_assert(sizeof(TCharacter) == (sizeof(void*) == 8 ? 352 : 344), "TCharacter runtime layout changed");
static_assert(sizeof(TPack) == (sizeof(void*) == 8 ? 16 : 8), "TPack runtime layout changed");
static_assert(sizeof(TLevelDef) == (sizeof(void*) == 8 ? 208 : 200), "TLevelDef runtime layout changed");
// Preserve the x86 Release baseline. TWeapon contains STL containers whose
// sizes also depend on architecture and iterator debugging.
#ifndef _DEBUG
static_assert(sizeof(void*) != 4 || sizeof(TWeapon) == 88248, "TWeapon x86 runtime layout changed");
#endif

// Pointer-free runtime sanity checks, not serialized format contracts.
static_assert(sizeof(TBullet)        == 96,   "TBullet size changed — projectile state");
static_assert(sizeof(TDinoInfo)      == 13800,"TDinoInfo size changed — runtime dino configuration");
static_assert(sizeof(TWeapInfo)      == 468,  "TWeapInfo size changed — weapon configuration");
static_assert(sizeof(TTrophyType)    == 580,  "TTrophyType size changed — trophy type data");
static_assert(sizeof(TDinoKill)      == 32,   "TDinoKill size changed — kill tracking record");
static_assert(sizeof(TWind)          == 20,   "TWind size changed — wind state");
static_assert(sizeof(TElements)      == 1072, "TElements size changed — elements state");
static_assert(sizeof(TBloodP)        == 20,   "TBloodP size changed — blood particle");
static_assert(sizeof(TBTrail)        == 10244,"TBTrail size changed — runtime blood trail");
static_assert(sizeof(THitBox)        == 32,   "THitBox size changed — hitbox data");
static_assert(sizeof(TBag)           == 32,   "TBag size changed — bag state");
static_assert(sizeof(TShip)          == 96,   "TShip size changed — ship state");
static_assert(sizeof(TDemoPoint)     == 20,   "TDemoPoint size changed — demo playback");
static_assert(sizeof(TSpawnGroup)    == 1568, "TSpawnGroup size changed — spawn group config");
static_assert(sizeof(TSpawnInfo)     == 8,    "TSpawnInfo size changed — spawn info");
static_assert(sizeof(TSpawnRegion)   == 16,   "TSpawnRegion size changed — spawn region");
static_assert(sizeof(TAIInfo)        == 92,   "TAIInfo size changed — AI info");
static_assert(sizeof(TPackType)      == 532,  "TPackType size changed — pack type config");
static_assert(sizeof(TPackMember)    == 8,    "TPackMember size changed — pack member");
static_assert(sizeof(TPackMember2)   == 8,    "TPackMember2 size changed — pack member 2");
static_assert(sizeof(TElement)       == 32,   "TElement size changed — world element");
static_assert(sizeof(TSnowType)      == 28,   "TSnowType size changed — snow type");
static_assert(sizeof(TSnowElement)   == 20,   "TSnowElement size changed — snow element");
