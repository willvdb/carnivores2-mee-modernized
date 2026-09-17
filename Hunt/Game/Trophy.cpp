// Trophy.cpp — auto-extracted from Game.cpp
// ==========================================================================
// Auto-extracted from Game.cpp
// ==========================================================================

#include "Hunt.h"
#include "ProfileSerialization.h"


// Constants from Projectiles.cpp
#define partBlood   1
#define partWater   2
#define partGround  3
#define partBubble  4

// Forward declarations from Ships.cpp
extern void AnimateShip();
extern void AnimateSShip();
extern void AnimateBag();

// Forward declaration from EngineInit.cpp
extern void SetupRes();


void ProcessTrophy()
{
  TrophyBody = -1;

  for (int c=0; c<ChCount; c++)
  {
  // Only stuffed mounts feed the plaque: live characters carry AI
  // states that can numerically equal a body slot (a companion at
  // state 5 beside an occupied slot 5 shows a stranger trophy plaque).
  // Mounts are exactly the StateF == 0xFF characters (see PlaceTrophy).
  // Hunt claim plaques are populated separately by AnimateCharacters.
  if (Characters[c].StateF != 0xFF) continue;
  const int slot = Characters[c].State;
  if (slot < 0 || slot >= TROPHY2_COUNT) continue;


	  //Vector3d p = Characters[c].pos;
	  Vector3d p;
    //p.x+=Characters[c].lookx * 256*2.5f;
    //p.z+=Characters[c].lookz * 256*2.5f;
	  p.x = Characters[c].xdata;
	  p.z = Characters[c].zdata;
	  p.y = GetLandH(p.x, p.z) + Characters[c].ydata;

	//Characters[c].Phase = 1;

	if (VectorLength(SubVectors(p, PlayerPos)) < 148 && TrophyRoom2.Body[Characters[c].State].ctype) {
      TrophyBody = Characters[c].State;
	  TrophyDisplayBody.ctype = TrophyRoom2.Body[Characters[c].State].ctype;
	  TrophyDisplayBody.scale = TrophyRoom2.Body[Characters[c].State].scale;
	  TrophyDisplayBody.weapon = TrophyRoom2.Body[Characters[c].State].weapon;
	  TrophyDisplayBody.score = TrophyRoom2.Body[Characters[c].State].score;
	  TrophyDisplayBody.phase = TrophyRoom2.Body[Characters[c].State].phase;
 	  TrophyDisplayBody.time = TrophyRoom2.Body[Characters[c].State].time;
	  TrophyDisplayBody.date = TrophyRoom2.Body[Characters[c].State].date;
	  TrophyDisplayBody.range = TrophyRoom2.Body[Characters[c].State].range;
	}
  }

  //if (TrophyBody==-1) return;

  //TrophyBody = Characters[TrophyBody].State;
}
void RespawnSnow(int st, int s, std::int32_t rand)
{
	Snow[s].pos.x = PlayerX + nv.x + siRand(12 * 256);//12
	Snow[s].pos.z = PlayerZ + nv.z + siRand(12 * 256);//12
	Snow[s].hl = GetLandUpH(Snow[s].pos.x, Snow[s].pos.z);
	Snow[s].ftime = 0;
	if (rand) Snow[s].pos.y = Snow[s].hl + 256 + rRand(12 * 256);
	else Snow[s].pos.y = Snow[s].hl + (8 + rRand(5)) * 256;
}
void AnimateElements()
{
  for (int eg=0; eg<ElCount; eg++)
  {

    if  (Elements[eg].Type == partGround)
    {
      int a1 = Elements[eg].RGBA >> 24;
      a1-=TimeDt/4;
      if (a1<0) a1=0;
      Elements[eg].RGBA = (Elements[eg].RGBA  & 0x00FFFFFF) + (a1<<24);
      int a2 = Elements[eg].RGBA2>> 24;
      a2-=TimeDt/4;
      if (a2<0) a2=0;
      Elements[eg].RGBA2= (Elements[eg].RGBA2 & 0x00FFFFFF) + (a2<<24);
      if (a1 == 0 && a2==0) Elements[eg].ECount = 0;
    }

    if  (Elements[eg].Type == partWater)
      if (Elements[eg].EDone == Elements[eg].ECount)
        Elements[eg].ECount = 0;

    if  (Elements[eg].Type == partBubble)
      if (Elements[eg].EDone == Elements[eg].ECount)
        Elements[eg].ECount = 0;

	if (Elements[eg].Type == partBlood)
		if ((Takt & 3) == 0)
			
		  if (Elements[eg].EDone == Elements[eg].ECount)
		  {
			int a1 = Elements[eg].RGBA >> 24;
			a1--;
			if (a1<0) a1=0;
			Elements[eg].RGBA = (Elements[eg].RGBA  & 0x00FFFFFF) + (a1<<24);
			int a2 = Elements[eg].RGBA2>> 24;
			a2--;
			if (a2<0) a2=0;
			Elements[eg].RGBA2= (Elements[eg].RGBA2 & 0x00FFFFFF) + (a2<<24);
			if (a1 == 0 && a2==0) Elements[eg].ECount = 0;
		  }
		  

//====== remove finished process =========//
    if (!Elements[eg].ECount)
    {
      memcpy(&Elements[eg], &Elements[eg+1], (ElCount+1-eg) * sizeof(TElements));
      ElCount--;
      eg--;
      continue;
    }


    for (int e=0; e<Elements[eg].ECount; e++)
    {
      if (Elements[eg].EList[e].Flags) continue;
      Elements[eg].EList[e].pos.x+=Elements[eg].EList[e].speed.x * TimeDt / 1000.f;
      Elements[eg].EList[e].pos.y+=Elements[eg].EList[e].speed.y * TimeDt / 1000.f;
      Elements[eg].EList[e].pos.z+=Elements[eg].EList[e].speed.z * TimeDt / 1000.f;

      float h;
      h = GetLandUpH(Elements[eg].EList[e].pos.x, Elements[eg].EList[e].pos.z);
      std::int32_t OnWater = GetLandH(Elements[eg].EList[e].pos.x, Elements[eg].EList[e].pos.z) < h;

      switch (Elements[eg].Type)
      {
      case partBubble:
        Elements[eg].EList[e].speed.y += 2.0 * 256 * TimeDt / 1000.f;
        if (Elements[eg].EList[e].speed.y > 824) Elements[eg].EList[e].speed.y = 824;
        if (Elements[eg].EList[e].pos.y > h)
        {
          AddWCircle(Elements[eg].EList[e].pos.x, Elements[eg].EList[e].pos.z, 0.6);
          Elements[eg].EDone++;
          Elements[eg].EList[e].Flags = 1;
          if (OnWater) Elements[eg].EList[e].pos.y-= 10240;
        }
        break;

      default:
        Elements[eg].EList[e].speed.y -= 9.8 * 256 * TimeDt / 1000.f;
        if (Elements[eg].EList[e].pos.y < h)
        {
          if (OnWater) AddWCircle(Elements[eg].EList[e].pos.x, Elements[eg].EList[e].pos.z, 0.6);
          Elements[eg].EDone++;
          Elements[eg].EList[e].Flags = 1;
          if (OnWater) Elements[eg].EList[e].pos.y-= 10240;
          else Elements[eg].EList[e].pos.y = h + 4;
        }
        break;

      } //== switch ==//

    } // for(e) //
  } // for(eg) //

  AnimateBloodTrails();


  
  
  for (int st = 0; st < SnowCh; st++) {
	  nv = Wind.nv;
	  NormVector(nv, (4 + Wind.speed) * SnowInfo[st].snow_hSpd * TimeDt / 1000);//4

	  while (SnowInfo[st].SnCount < SnowInfo[st].snow_dens) {//2000
		  RespawnSnow(st, SnowInfo[st].addr + SnowInfo[st].SnCount, true);
		  SnowInfo[st].SnCount++;
	  }

	  for (int s = SnowInfo[st].addr; s < SnowInfo[st].addr+SnowInfo[st].SnCount; s++) {

		  if ((fabs(Snow[s].pos.x - PlayerX) > 14 * 256) ||
			  (fabs(Snow[s].pos.z - PlayerZ) > 14 * 256)) {
			  Snow[s].pos.x = PlayerX + siRand(12 * 256);
			  Snow[s].pos.z = PlayerZ + siRand(12 * 256);
			  Snow[s].pos.y = Snow[s].pos.y - Snow[s].hl;
			  Snow[s].hl = GetLandUpH(Snow[s].pos.x, Snow[s].pos.z);
			  Snow[s].pos.y += Snow[s].hl;
		  }

		  if (!Snow[s].ftime) {
			  float v = (((RealTime + s * 23) % 800) - 400) * TimeDt / 16000;
			  Snow[s].pos.x += ca * v;
			  Snow[s].pos.z += sa * v;

			  Snow[s].pos = AddVectors(Snow[s].pos, nv);
			  Snow[s].hl = GetLandUpH(Snow[s].pos.x, Snow[s].pos.z);
			  Snow[s].pos.y -= TimeDt * SnowInfo[st].snow_vSpd / 1000.f; //192
			  if (Snow[s].pos.y < Snow[s].hl + 8) {
				  Snow[s].pos.y = Snow[s].hl + 8;
				  Snow[s].ftime = 1;
			  }
		  }
		  else {
			  Snow[s].ftime += TimeDt;
			  Snow[s].pos.y -= TimeDt * (SnowInfo[st].snow_vSpd / 64) / 1000.f; //3
			  if (Snow[s].ftime > (2000 / (SnowInfo[st].snow_vSpd / 192)))  RespawnSnow(st, s, false); //2000
		  }

	  }


  }
  

}
void AnimateProcesses()
{
  AnimateElements();

  if ((Takt & 63)==0)
  {
    float al2 = CameraAlpha + siRand(60) * pi / 180.f;
    float c2 = cos(al2);
    float s2 = sin(al2);
    float l = 1024 + rRand(3120);
    float xx = CameraX + s2 * l;
    float zz = CameraZ - c2 * l;
    if (GetLandUpH(xx,zz) > GetLandH(xx,zz)+256)
      AddElements(xx, GetLandH(xx,zz), zz, 4, 6 + rRand(6));
  }

  if (!Multiplayer || Host) {
	  if (Takt & 1)
	  {
		  Wind.alpha += siRand(16) / 4096.f;
		  Wind.speed += siRand(400) / 6400.f;
	  }

	  if (Wind.speed < 4.f) Wind.speed = 4.f;
	  if (Wind.speed > 18.f) Wind.speed = 18.f;
  }
  Wind.nv.x = static_cast<float>(sin(Wind.alpha));
  Wind.nv.z = static_cast<float>(-cos(Wind.alpha));
  Wind.nv.y = 0.f;

  if (answtime)
  {
    answtime-=TimeDt;
    if (answtime<=0)
    {
      answtime = 0;
      int r = rRand(128) % 3;
      AddVoice3d(fxCall[answcall-10][r].length,  fxCall[answcall-10][r].lpData.data(),
                 answpos.x, answpos.y, answpos.z);
    }
  }



  if (CallLockTime)
  {
    CallLockTime-=TimeDt;
    if (CallLockTime<0) CallLockTime=0;
  }

  CheckAfraid();
  AnimateShip();
  AnimateSShip();
  AnimateBag();
  // The proximity scan feeds the info plaque (TrophyBody). It must run
  // in the trophy room too: the room runs in Normal mode (trophy-ness
  // is only the map name), so gating on TrophyMode alone left every
  // mount mute. Harmless elsewhere: in hunts TrophyBody was already set.
  if (InTrophyRoom())
    ProcessTrophy();

  for (int w=0; w<WCCount; w++)
  {
    if (WCircles[w].scale > 1)
      WCircles[w].FTime+=static_cast<int>((TimeDt*3 / WCircles[w].scale));
    else
      WCircles[w].FTime+=TimeDt*3;
    if (WCircles[w].FTime >= 2000)
    {
      // Shift [w+1 .. WCCount-1] down to [w .. WCCount-2]. That is
      // (WCCount-1-w) elements. The old code used (WCCount+1-w), which
      // copied 2 extra elements and read/wrote one past the array end
      // when the buffer was full (w == WCCount-1 == 2095).
      memmove(&WCircles[w], &WCircles[w+1], sizeof(TWCircle) * (WCCount - 1 - w));
      w--;
      WCCount--;
    }
  }

  if (WaveNoteTime) {
	  WaveNoteTime -= TimeDt;
	  if (WaveNoteTime <= 0) WaveNoteTime = 0;
  }

  if (ExitTime)
  {
    ExitTime-=TimeDt;
    if (ExitTime<=0)
    {
      TrophyRoom.Total.time   +=TrophyRoom.Last.time;
      TrophyRoom.Total.smade  +=TrophyRoom.Last.smade;
      TrophyRoom.Total.success+=TrophyRoom.Last.success;
      TrophyRoom.Total.path   +=TrophyRoom.Last.path;

      if (MyHealth) SaveTrophy();
      else LoadTrophy();
      DoHalt("");
    }
  }
}
void RemoveCurrentTrophy()
{
  if (!InTrophyRoom()) return;
  if (TrophyBody < 0 || TrophyBody >= TROPHY2_COUNT) return;
  if (!TrophyRoom2.Body[TrophyBody].ctype) return;

  // Placement may skip invalid saved species, so find the mount by slot.
  int p = 0;
  while (p < ChCount &&
         (Characters[p].StateF != 0xFF || Characters[p].State != TrophyBody)) ++p;
  if (p == ChCount) return;



  PrintLogVerbose("Trophy removed: ");
  //PrintLog(DinoInfo[TrophyRoom.Body[TrophyBody].ctype].Name);
  PrintLogVerbose(DinoInfo[TrophyRoom2.Body[TrophyBody].ctype].Name);
  PrintLogVerbose("\n");

  
  TrophyRoom2.Body[TrophyBody] = {};

  for (int c = p; c + 1 < ChCount; ++c)
    Characters[c] = Characters[c + 1];
  Characters[--ChCount] = {};





  TrophyDisplay = false;
  TrophyBody = -1;
}
void LoadTrophy2(int RegNumber) {
    TrophyRoom2 = {};
    char fname2[128];
    snprintf(fname2, sizeof(fname2), "trophy0%d.sab", RegNumber);
    Platform::FileHandle hfile2 = Platform::OpenFile(fname2, Platform::FileMode::Read);
    if (hfile2 == Platform::InvalidFile) {
        PrintLog("===> Error loading trophyB!\n");
        return;
    }
    LegacyProfile::RoomBytes bytes{};
    std::uint32_t count = 0;
    const std::int32_t ok = Platform::ReadFile(hfile2, bytes.data(), LegacyProfile::RoomSize, &count);
    Platform::CloseFile(hfile2);
    if (!ok || !EngineProfile::LoadRoom(bytes.data(), count, TrophyRoom2)) {
        PrintLog("===> Short or invalid trophyB!\n");
        return;
    }
    PrintLog("TrophyB Loaded.\n");
}

void LoadTrophy()
{
    const int registration = TrophyRoom.RegNumber;
    TrophyRoom = {};
    TrophyRoom.RegNumber = registration;
    char fname[128];
    snprintf(fname, sizeof(fname), "trophy0%d.sav", registration);
    Platform::FileHandle hfile = Platform::OpenFile(fname, Platform::FileMode::Read);
    if (hfile == Platform::InvalidFile) {
        PrintLog("===> Error loading trophy!\n");
        return;
    }
    LegacyProfile::SaveBytes bytes{};
    std::uint32_t count = 0;
    const std::int32_t ok = Platform::ReadFile(hfile, bytes.data(), LegacyProfile::SaveSize, &count);
    Platform::CloseFile(hfile);
    if (!ok || !EngineProfile::LoadProfile(bytes.data(), count, TrophyRoom)) {
        PrintLog("===> Short or invalid trophy prefix!\n");
        return;
    }
    if (count < LegacyProfile::KeyEnd)
        PrintLog("Trophy: short KeyMap read, keeping defaults.\n");
    SetupRes();
    PrintLog("Trophy Loaded.\n");
    if (TrophyRoom.Body[0].ctype) LoadTrophy2(TrophyRoom.RegNumber);
    else TrophyRoom.Body[0].ctype = 1;
}

void SaveTrophy2(int RegNumber) {
    char fname2[128];
    snprintf(fname2, sizeof(fname2), "trophy0%d.sab", RegNumber);
    const auto bytes = LegacyProfile::EncodeRoom(EngineProfile::FromRuntime(TrophyRoom2));
    Platform::FileHandle hfile2 = Platform::OpenFile(fname2, Platform::FileMode::Write);
    if (hfile2 == Platform::InvalidFile) {
        PrintLog("==>> Error saving trophy!\n");
        return;
    }
    std::uint32_t count = 0;
    const std::int32_t ok = Platform::WriteFile(hfile2, bytes.data(), LegacyProfile::RoomSize, &count);
    Platform::CloseFile(hfile2);
    if (!ok || count != LegacyProfile::RoomSize) {
        PrintLog("==>> Error writing trophyB!\n");
        return;
    }
    PrintLog("TrophyB Saved.\n");
}

void SaveTrophy()
{
    char fname[128];
    snprintf(fname, sizeof(fname), "trophy0%d.sav", TrophyRoom.RegNumber);
    EngineProfile::UpdateRank(TrophyRoom);

    const auto bytes = LegacyProfile::EncodeSave({EngineProfile::FromRuntime(TrophyRoom),
                                                  EngineProfile::CaptureOptions()});
    Platform::FileHandle hfile = Platform::OpenFile(fname, Platform::FileMode::Write);
    if (hfile == Platform::InvalidFile) {
        PrintLog("==>> Error saving trophy!\n");
        return;
    }
    std::uint32_t count = 0;
    const std::int32_t ok = Platform::WriteFile(hfile, bytes.data(), LegacyProfile::SaveSize, &count);
    Platform::CloseFile(hfile);
    if (!ok || count != LegacyProfile::SaveSize) {
        PrintLog("==>> Error writing trophy!\n");
        return;
    }
    PrintLog("Trophy Saved.\n");
    SaveTrophy2(TrophyRoom.RegNumber);
}
