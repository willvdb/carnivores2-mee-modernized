// ==========================================================================
// ScriptParser.cpp � _RES.TXT / _MENU.TXT script file parser
// ==========================================================================

#include "Hunt.h"
#include "LegacyAssetPath.h"
#include "LoadValidate.h"

// _RES.TXT string safety. Name/file fields are fixed char arrays
// (WeapInfo/DinoInfo [...][48]); an overlong modded value previously
// overflowed via strcpy, and value[strlen(value)-2] indexed before the
// buffer when the quoted value was shorter than ''. Halt loudly instead.
static void ScriptFieldFail(const char* what)
{
  char sz[256];
  sprintf_s(sz, sizeof(sz),
            "Script loading error: %s missing, too long, or malformed.", what);
  DoHalt(sz);
}

static void CopyScriptField(char* dst, size_t dstCap, char* value, const char* what)
{
  char* inner = StripQuoted(value);
  if (!inner || !CopyCapped(dst, dstCap, inner))
    ScriptFieldFail(what);
}

static void CopyProjectName(char* dst, const char* src)
{
  if (!CopyCapped(dst, 128, src))
    DoHalt("Script loading error: project path too long.");
  // The vanilla sixth slot stores assets as external.map/.rsc; every other
  // slot is areaN. The legacy area-filter logic below reads the fixed path
  // offset that holds the area digit, which a bare "external" basename
  // cannot satisfy (reproduced as an 0xC0000005 launch crash). Normalize the
  // parser-local copy to the slot's logical name so the offset logic keeps
  // working; the engine's file-open path (ProjectName) is untouched and still
  // opens external.map/.rsc. See ProjectBasenameIsExternal/RewriteExternalProjectAlias
  // in LoadValidate.h and tests/test_load_validate.cpp.
  RewriteExternalProjectAlias(dst, 128);
}

static void RequireScriptSlot(int index, int capacity, const char* what)
{
  if (!IsValidIndex(index, capacity))
  {
    char sz[192];
    sprintf_s(sz, sizeof(sz),
              "Script loading error: %s capacity exceeded (index=%d, max=%d).",
              what, index, capacity - 1);
    DoHalt(sz);
  }
}

static int ScriptIndex(const char* value, int capacity, const char* what)
{
  const int index = value ? atoi(value) : -1;
  RequireScriptSlot(index, capacity, what);
  return index;
}

static int CurrentJumpPartIndex()
{
  const int index = DinoInfo[TotalC].jumpAnim;
  RequireScriptSlot(index, 50, "jump particle animation");
  return index;
}

static int CurrentIdlePartIndex()
{
  if (DinoInfo[TotalC].lookCount <= 0)
    DoHalt("Script loading error: idle particle data has no preceding look animation.");
  const int index = DinoInfo[TotalC].lookAnim[DinoInfo[TotalC].lookCount - 1];
  RequireScriptSlot(index, 50, "idle particle animation");
  return index;
}

void readBool(char *value, BOOL &out) {
	if (strstr(value, "TRUE")) out = true;
	if (strstr(value, "FALSE")) out = false;
}

void readBool(char *value, bool &out) {
	if (strstr(value, "TRUE")) out = true;
	if (strstr(value, "FALSE")) out = false;
}

void SkipSector(FILE *stream)
{
	char line[256], *value;
	while (fgets(line, 255, stream))
	{
		if (strstr(line, "}")) break;
		if (strstr(line, "{"))
			while (fgets(line, 255, stream)) {
				if (strstr(line, "}")) break;
				if (strstr(line, "{"))
					while (fgets(line, 255, stream)) {
						if (strstr(line, "}")) break;
						if (strstr(line, "{"))
							while (fgets(line, 255, stream)) {
								if (strstr(line, "}")) break;
								if (strstr(line, "{"))
									while (fgets(line, 255, stream)) {
										if (strstr(line, "}")) break;
										if (strstr(line, "{"))
											while (fgets(line, 255, stream)) {
												if (strstr(line, "}")) break;
												if (strstr(line, "{"))
													while (fgets(line, 255, stream)) {
														if (strstr(line, "}")) break;
														if (strstr(line, "{"))
															while (fgets(line, 255, stream)) {
																if (strstr(line, "}")) break;
															}
													}
											}
									}
							}
					}
			}
	}
}

void ReadTrophyTypeInfo(FILE *stream, int trophyGroup)
{
	RequireScriptSlot(trophyTypeCount, TROPHY2_COUNT, "trophy types");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {

			if (trophyGroup >= 0) {
				trophyType[trophyTypeCount].group = trophyGroup;
			}
			else {
				RequireScriptSlot(trophyType[trophyTypeCount].ctypeCh, TROPHY2_COUNT,
				                  "trophy character list");
				trophyType[trophyTypeCount].ctype[trophyType[trophyTypeCount].ctypeCh] = TotalC;
				trophyType[trophyTypeCount].ctypeCh++;
				DinoInfo[TotalC].trophy = true;
			}
			trophyTypeCount++;
			break;

		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: TrophyType: %i", trophyTypeCount);
			DoHalt(errorBuff);
		}
		value++;


		if (strstr(line, "tropPos")) trophyType[trophyTypeCount].trophyPos = atoi(value);
		if (strstr(line, "alpha")) trophyType[trophyTypeCount].alpha = atoi(value);
		if (strstr(line, "beta")) trophyType[trophyTypeCount].beta = atoi(value);
		if (strstr(line, "gamma")) trophyType[trophyTypeCount].gamma = atoi(value);
		if (strstr(line, "xoffset")) trophyType[trophyTypeCount].xoffset = atoi(value);
		if (strstr(line, "yoffset")) trophyType[trophyTypeCount].yoffset = atoi(value);
		if (strstr(line, "zoffset")) trophyType[trophyTypeCount].zoffset = atoi(value);
		if (strstr(line, "xscale")) trophyType[trophyTypeCount].xoffsetScale = atoi(value);
		if (strstr(line, "yscale")) trophyType[trophyTypeCount].yoffsetScale = atoi(value);
		if (strstr(line, "zscale")) trophyType[trophyTypeCount].zoffsetScale = atoi(value);
		if (strstr(line, "tropAnim")) trophyType[trophyTypeCount].anim = atoi(value);
		if (strstr(line, "xdata")) trophyType[trophyTypeCount].xdata = atoi(value);
		if (strstr(line, "ydata")) trophyType[trophyTypeCount].ydata = atoi(value);
		if (strstr(line, "zdata")) trophyType[trophyTypeCount].zdata = atoi(value);
		if (strstr(line, "playAnim")) readBool(value, trophyType[trophyTypeCount].playAnim);



	}
}


void WipePackMembers2() {
	if (DinoInfo[TotalC].packMember2Ch) {
		for (int i = 0; i < DinoInfo[TotalC].packMember2Ch; i++) {
			DinoInfo[TotalC].packMember2[i] = {};
		}
	}
	DinoInfo[TotalC].packMember2Ch = 0;
}


void WipeSpawnInfo() {
	if (DinoInfo[TotalC].SpawnInfoCh) {
		for (int i = 0; i < DinoInfo[TotalC].SpawnInfoCh; i++) {
			DinoInfo[TotalC].SpawnInfo[i] = {};
		}
	}
	DinoInfo[TotalC].SpawnInfoCh = 0;
}

void ReadSpawnInfo(FILE *stream)
{
	RequireScriptSlot(DinoInfo[TotalC].SpawnInfoCh, 32, "character spawn-info list");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			//DinoInfo[TotalC].RType0[DinoInfo[TotalC].RegionCount] = TotalRegion;
			DinoInfo[TotalC].SpawnInfoCh++;
			//TotalRegion++;
			break;
		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: SpawnInfo: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "spawnratio")) DinoInfo[TotalC].SpawnInfo[DinoInfo[TotalC].SpawnInfoCh].spawnRatio = static_cast<float>(atof(value));
		if (strstr(line, "spawngroup")) DinoInfo[TotalC].SpawnInfo[DinoInfo[TotalC].SpawnInfoCh].spawnGroup = atoi(value);
		//if (strstr(line, "spawnmax")) DinoInfo[TotalC].SpawnInfo[DinoInfo[TotalC].SpawnInfoCh].spawnMax = atoi(value);
	}
}



void WipeSpawnInfoPack() {
	RequireScriptSlot(packTypeCount, 1024, "pack types");
	if (packType[packTypeCount].SpawnInfoCh) {
		for (int i = 0; i < packType[packTypeCount].SpawnInfoCh; i++) {
			packType[packTypeCount].SpawnInfo[i] = {};
		}
	}
	packType[packTypeCount].SpawnInfoCh = 0;
}

void ReadSpawnInfoPack(FILE *stream)
{
	RequireScriptSlot(packTypeCount, 1024, "pack types");
	RequireScriptSlot(packType[packTypeCount].SpawnInfoCh, 32, "pack spawn-info list");
	char *value;
	char line[256];



	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			packType[packTypeCount].SpawnInfoCh++;
			break;
		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: SpawnInfo: Pack%i", packTypeCount);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "spawnratio")) packType[packTypeCount].SpawnInfo[packType[packTypeCount].SpawnInfoCh].spawnRatio = static_cast<float>(atof(value));
		if (strstr(line, "spawngroup")) packType[packTypeCount].SpawnInfo[packType[packTypeCount].SpawnInfoCh].spawnGroup = atoi(value);
	}
}


void ReadPackMember2(FILE *stream) {
	RequireScriptSlot(DinoInfo[TotalC].packMember2Ch, 32, "character pack-member list");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {

			DinoInfo[TotalC].packMember2Ch++;
			break;
		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: PackInfo: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "group")) DinoInfo[TotalC].packMember2[DinoInfo[TotalC].packMember2Ch].packGroup = atoi(value);
		if (strstr(line, "ratio")) DinoInfo[TotalC].packMember2[DinoInfo[TotalC].packMember2Ch].ratio = static_cast<float>(atof(value));

	}
}

void ReadAvoid(FILE *stream)
{
	RequireScriptSlot(TotalSpawnGroup, 256, "spawn groups");
	RequireScriptSlot(spawnGroup[TotalSpawnGroup].avoidRegionCh, 16, "spawn-group avoid regions");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			spawnGroup[TotalSpawnGroup].avoidRegionCh++;
			break;
		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: Avoid: SpawnGroup %i", TotalSpawnGroup);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "xmax")) spawnGroup[TotalSpawnGroup].avoidRegion[spawnGroup[TotalSpawnGroup].avoidRegionCh].XMax = atoi(value);
		if (strstr(line, "xmin")) spawnGroup[TotalSpawnGroup].avoidRegion[spawnGroup[TotalSpawnGroup].avoidRegionCh].XMin = atoi(value);
		if (strstr(line, "ymax")) spawnGroup[TotalSpawnGroup].avoidRegion[spawnGroup[TotalSpawnGroup].avoidRegionCh].YMax = atoi(value);
		if (strstr(line, "ymin")) spawnGroup[TotalSpawnGroup].avoidRegion[spawnGroup[TotalSpawnGroup].avoidRegionCh].YMin = atoi(value);

	}
}

void ReadRegion(FILE *stream)
{
	RequireScriptSlot(TotalSpawnGroup, 256, "spawn groups");
	RequireScriptSlot(spawnGroup[TotalSpawnGroup].spawnRegionCh, 16, "spawn-group regions");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			spawnGroup[TotalSpawnGroup].spawnRegionCh++;
			break;
		}
		value = strstr(line, "=");

		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: Region: SpawnGroup %i", TotalSpawnGroup);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "xmax")) spawnGroup[TotalSpawnGroup].spawnRegion[spawnGroup[TotalSpawnGroup].spawnRegionCh].XMax = atoi(value);
		if (strstr(line, "xmin")) spawnGroup[TotalSpawnGroup].spawnRegion[spawnGroup[TotalSpawnGroup].spawnRegionCh].XMin = atoi(value);
		if (strstr(line, "ymax")) spawnGroup[TotalSpawnGroup].spawnRegion[spawnGroup[TotalSpawnGroup].spawnRegionCh].YMax = atoi(value);
		if (strstr(line, "ymin")) spawnGroup[TotalSpawnGroup].spawnRegion[spawnGroup[TotalSpawnGroup].spawnRegionCh].YMin = atoi(value);

	}
}

void WipeAvoidReg() {
	RequireScriptSlot(TotalSpawnGroup, 256, "spawn groups");

	if (spawnGroup[TotalSpawnGroup].avoidRegionCh) {

		for (int i = 0; i < spawnGroup[TotalSpawnGroup].avoidRegionCh; i++) {
			spawnGroup[TotalSpawnGroup].avoidRegion[i] = {};
		}
		spawnGroup[TotalSpawnGroup].avoidRegionCh = 0;

	}

}

void WipeSpawnReg() {
	RequireScriptSlot(TotalSpawnGroup, 256, "spawn groups");

	if (spawnGroup[TotalSpawnGroup].spawnRegionCh) {

		for (int i = 0; i < spawnGroup[TotalSpawnGroup].spawnRegionCh; i++) {
			spawnGroup[TotalSpawnGroup].spawnRegion[i] = {};
		}
		spawnGroup[TotalSpawnGroup].spawnRegionCh = 0;

	}

}


void ReadSpawnTableLine(FILE *stream, char *_value, char line[256], bool &spawnOverwrite, bool &avoidOverwrite) {
	char *value = _value;

	if (strstr(line, "region")) {

		if (spawnOverwrite) {
			WipeSpawnReg();
			spawnOverwrite = false;
		}

		ReadRegion(stream);
	}

	if (strstr(line, "avoid")) {

		if (avoidOverwrite) {
			WipeAvoidReg();
			avoidOverwrite = false;
		}

		ReadAvoid(stream);
	}



	if (strstr(line, "spawnrate")) spawnGroup[TotalSpawnGroup].SpawnRate = static_cast<float>(atof(value));
	if (strstr(line, "spawnmax")) spawnGroup[TotalSpawnGroup].SpawnMax = atoi(value);
	if (strstr(line, "spawnmin")) spawnGroup[TotalSpawnGroup].SpawnMin = atoi(value);

	if (strstr(line, "densityMulti")) spawnGroup[TotalSpawnGroup].densityMulti = atoi(value);
	if (strstr(line, "moveForward")) readBool(value, spawnGroup[TotalSpawnGroup].moveForward);
	if (strstr(line, "randomise")) readBool(value, spawnGroup[TotalSpawnGroup].Randomised);
	if (strstr(line, "onlyActiveNearby")) readBool(value, spawnGroup[TotalSpawnGroup].OnlyActiveNearby);
	if (strstr(line, "styInRgn")) readBool(value, spawnGroup[TotalSpawnGroup].stayInRegion);

}

//mode 0 = spawntable
//mode 1 = characters
//mode 2 = packtable
void ReadSpawnGroup(FILE *stream, char line[256], int mode) {
	RequireScriptSlot(TotalSpawnGroup, 256, "spawn groups");

	//area
	char tempProjectName[128];
	int timeOfDay, dinSelect;
	for (int a = 0; a < __argc; a++)
	{
		LPSTR s = __argv[a];
		if (strstr(s, "prj="))
		{
			CopyProjectName(tempProjectName, (s + 4));
		}
		if (strstr(s, "dtm=")) timeOfDay = atoi(&s[4]);
		if (strstr(s, "din=")) dinSelect = (atoi(&s[4]) * 1024);
	}

	//time
	for (int a = 0; a < __argc; a++)
	{
		LPSTR s = __argv[a];
	}

	char *value;


	while (fgets(line, 255, stream))
	{
		if (strstr(line, "}"))
		{
			if (mode == 2) {
				RequireScriptSlot(packTypeCount, 1024, "pack types");
				RequireScriptSlot(packType[packTypeCount].SpawnInfoCh, 32, "pack spawn-info list");
				packType[packTypeCount].SpawnInfo[packType[packTypeCount].SpawnInfoCh].spawnRatio = 1;
				packType[packTypeCount].SpawnInfo[packType[packTypeCount].SpawnInfoCh].spawnGroup = TotalSpawnGroup;
				packType[packTypeCount].SpawnInfoCh++;
			}
			if (mode == 1) {
				RequireScriptSlot(DinoInfo[TotalC].SpawnInfoCh, 32, "character spawn-info list");
				DinoInfo[TotalC].SpawnInfo[DinoInfo[TotalC].SpawnInfoCh].spawnRatio = 1;
				DinoInfo[TotalC].SpawnInfo[DinoInfo[TotalC].SpawnInfoCh].spawnGroup = TotalSpawnGroup;
				DinoInfo[TotalC].SpawnInfoCh++;
			}
			TotalSpawnGroup++;
			break;
		}
		value = strstr(line, "=");



		if (strstr(line, "overwrite") || strstr(line, "addition")) {

			bool readThis = true;
			char mapNo1 = tempProjectName[18];
			while (readThis == true) {

				//trophy
				if (tempProjectName[18] == 'h') break;

				if (strstr(line, "area")) {

					switch ((char)tempProjectName[18]) {
					case '1':
						if (tempProjectName[19]) {
							if (!strstr(line, "area0")) readThis = false;//area10
						}
						else if (!strstr(line, "area1")) readThis = false;
						break;
					case '2':
						if (!strstr(line, "area2")) readThis = false;
						break;
					case '3':
						if (!strstr(line, "area3")) readThis = false;
						break;
					case '4':
						if (!strstr(line, "area4")) readThis = false;
						break;
					case '5':
						if (!strstr(line, "area5")) readThis = false;
						break;
					case '6':
						if (!strstr(line, "area6")) readThis = false;
						break;
					case '7':
						if (!strstr(line, "area7")) readThis = false;
						break;
					case '8':
						if (!strstr(line, "area8")) readThis = false;
						break;
					case '9':
						if (!strstr(line, "area9")) readThis = false;
						break;
					}
				}

				if (strstr(line, "time")) {
					switch (timeOfDay) {
					case 0:
						if (!strstr(line, "time0")) readThis = false;
						break;
					case 1:
						if (!strstr(line, "time1")) readThis = false;
						break;
					case 2:
						if (!strstr(line, "time2")) readThis = false;
						break;
					}
				}


				if (strstr(line, "char")) {
					bool readChar = false;
					if (strstr(line, "char0") && (dinSelect & (1 << 10))) readChar = true;
					if (strstr(line, "char1") && (dinSelect & (1 << 11))) readChar = true;
					if (strstr(line, "char2") && (dinSelect & (1 << 12))) readChar = true;
					if (strstr(line, "char3") && (dinSelect & (1 << 13))) readChar = true;
					if (strstr(line, "char4") && (dinSelect & (1 << 14))) readChar = true;
					if (strstr(line, "char5") && (dinSelect & (1 << 15))) readChar = true;
					if (strstr(line, "char6") && (dinSelect & (1 << 16))) readChar = true;
					if (strstr(line, "char7") && (dinSelect & (1 << 17))) readChar = true;
					if (strstr(line, "char8") && (dinSelect & (1 << 18))) readChar = true;
					if (strstr(line, "char9") && (dinSelect & (1 << 19))) readChar = true;
					if (!readChar) readThis = false;
				}

				break;
			}




			if (readThis) {

				bool regionOverwrite = strstr(line, "overwrite");
				bool avoidOverwrite = strstr(line, "overwrite");

				while (fgets(line, 255, stream)) {
					if (strstr(line, "}")) break;

					value = strstr(line, "=");
					if (!value
						&& !strstr(line, "region")
						&& !strstr(line, "avoid"))
						DoHalt("Script loading error: SpawnTable");
					value = value ? value + 1 : line;

					ReadSpawnTableLine(stream, value, line, regionOverwrite, avoidOverwrite);

				}

			}
			else {
				SkipSector(stream);
			}
		}
		else {
			bool temp, temp2;
			temp = false;
			temp2 = false;
			value = value ? value + 1 : line;
			ReadSpawnTableLine(stream, value, line, temp, temp2);

		}


	}
}

void ReadSpawnTable(FILE *stream)
{

	if (g_GameMode == GameMode::SurvivalMode)
	{
		spawnGroup[0].spawnRegion[0].XMax = SurvivalDinoSpawn.XMax;
		spawnGroup[0].spawnRegion[0].XMin = SurvivalDinoSpawn.XMin;
		spawnGroup[0].spawnRegion[0].YMax = SurvivalDinoSpawn.YMax;
		spawnGroup[0].spawnRegion[0].YMin = SurvivalDinoSpawn.YMin;
		spawnGroup[0].spawnRegionCh = 1;
		TotalSpawnGroup = 1;
		SkipSector(stream);
		return;
	}






	TotalSpawnGroup = 0;
	char line[256];
	while (fgets(line, 255, stream))
	{
		if (strstr(line, "}")) break;

		if (strstr(line, "spawngroup")) ReadSpawnGroup(stream, line, 0);
	}
}


void ReadPackTableLine(FILE *stream, char *_value, char line[256], bool &spawnIOverwrite) {
	RequireScriptSlot(packTypeCount, 1024, "pack types");

	char *value = _value;

	if (strstr(line, "spawninfo")) {
		if (spawnIOverwrite) {
			WipeSpawnInfoPack();
			spawnIOverwrite = false;
		}
		ReadSpawnInfoPack(stream);
	}

	if (strstr(line, "spawngroup")) {
		if (spawnIOverwrite) {//use same overwrite
			WipeSpawnInfoPack();
			spawnIOverwrite = false;
		}
		ReadSpawnGroup(stream, line, 2);
	}

	if (strstr(line, "packMax")) packType[packTypeCount].packMax = atoi(value);
	if (strstr(line, "packMin")) packType[packTypeCount].packMin = atoi(value);
	if (strstr(line, "packDensity")) packType[packTypeCount].packDensity = static_cast<float>(atof(value));
	
}

//mode 0 = packtable
//mode 1 = character
void ReadPackGroup(FILE *stream, char line[256], int mode) {
	RequireScriptSlot(packTypeCount, 1024, "pack types");

	//area
	char tempProjectName[128];
	int timeOfDay, dinSelect;
	for (int a = 0; a < __argc; a++)
	{
		LPSTR s = __argv[a];
		if (strstr(s, "prj="))
		{
			CopyProjectName(tempProjectName, (s + 4));
		}
		if (strstr(s, "dtm=")) timeOfDay = atoi(&s[4]);
		if (strstr(s, "din=")) dinSelect = (atoi(&s[4]) * 1024);
	}

	//time
	for (int a = 0; a < __argc; a++)
	{
		LPSTR s = __argv[a];
	}

	char *value;

	while (fgets(line, 255, stream))
	{
		if (strstr(line, "}"))
		{
			if (mode == 1) {
				RequireScriptSlot(DinoInfo[TotalC].packMember2Ch, 32, "character pack-member list");
				DinoInfo[TotalC].packMember2[DinoInfo[TotalC].packMember2Ch].packGroup = packTypeCount;
				DinoInfo[TotalC].packMember2[DinoInfo[TotalC].packMember2Ch].ratio = 1;
				DinoInfo[TotalC].packMember2Ch++;
			}
			packTypeCount++;
			break;
		}

		value = strstr(line, "=");
		value = value ? value + 1 : line;
		bool temp1;
		temp1 = false;
		ReadPackTableLine(stream, value, line, temp1);

		if (strstr(line, "overwrite") || strstr(line, "addition")) {



			bool readThis = true;
			char mapNo1 = tempProjectName[18];
			while (readThis == true) {

				//trophy
				if (tempProjectName[18] == 'h') break;

				if (strstr(line, "area")) {

					switch ((char)tempProjectName[18]) {
					case '1':
						if (tempProjectName[19]) {
							if (!strstr(line, "area0")) readThis = false;//area10
						}
						else if (!strstr(line, "area1")) readThis = false;
						break;
					case '2':
						if (!strstr(line, "area2")) readThis = false;
						break;
					case '3':
						if (!strstr(line, "area3")) readThis = false;
						break;
					case '4':
						if (!strstr(line, "area4")) readThis = false;
						break;
					case '5':
						if (!strstr(line, "area5")) readThis = false;
						break;
					case '6':
						if (!strstr(line, "area6")) readThis = false;
						break;
					case '7':
						if (!strstr(line, "area7")) readThis = false;
						break;
					case '8':
						if (!strstr(line, "area8")) readThis = false;
						break;
					case '9':
						if (!strstr(line, "area9")) readThis = false;
						break;
					}
				}

				if (strstr(line, "time")) {
					switch (timeOfDay) {
					case 0:
						if (!strstr(line, "time0")) readThis = false;
						break;
					case 1:
						if (!strstr(line, "time1")) readThis = false;
						break;
					case 2:
						if (!strstr(line, "time2")) readThis = false;
						break;
					}
				}


				if (strstr(line, "char")) {
					bool readChar = false;
					if (strstr(line, "char0") && (dinSelect & (1 << 10))) readChar = true;
					if (strstr(line, "char1") && (dinSelect & (1 << 11))) readChar = true;
					if (strstr(line, "char2") && (dinSelect & (1 << 12))) readChar = true;
					if (strstr(line, "char3") && (dinSelect & (1 << 13))) readChar = true;
					if (strstr(line, "char4") && (dinSelect & (1 << 14))) readChar = true;
					if (strstr(line, "char5") && (dinSelect & (1 << 15))) readChar = true;
					if (strstr(line, "char6") && (dinSelect & (1 << 16))) readChar = true;
					if (strstr(line, "char7") && (dinSelect & (1 << 17))) readChar = true;
					if (strstr(line, "char8") && (dinSelect & (1 << 18))) readChar = true;
					if (strstr(line, "char9") && (dinSelect & (1 << 19))) readChar = true;
					if (!readChar) readThis = false;
				}

				break;
			}




			if (readThis) {

				bool spawnInfoOverwrite = strstr(line, "overwrite");

				while (fgets(line, 255, stream)) {
					if (strstr(line, "}")) break;

					value = strstr(line, "=");
					value = value ? value + 1 : line;

					ReadPackTableLine(stream, value, line, spawnInfoOverwrite);

				}

			}
			else {
				SkipSector(stream);
			}
		}

	}
}

void ReadPackTable(FILE *stream)
{
	packTypeCount = 0;

	if (g_GameMode == GameMode::SurvivalMode) {
		SkipSector(stream);
		return;
	}

	char line[256];
	while (fgets(line, 255, stream))
	{
		if (strstr(line, "}")) break;


		if (strstr(line, "packgroup"))ReadPackGroup(stream, line, 0);
			

	}

}

void ReadTrophyTable(FILE *stream)
{
	trophyGroupCount = 0;

	char line[256], *value;
	while (fgets(line, 255, stream))
	{
		if (strstr(line, "}")) break;

		if (strstr(line, "trophygroup"))
			while (fgets(line, 255, stream))
			{
				if (strstr(line, "}"))
				{
					trophyGroupCount++;
					break;
				}

				if (strstr(line, "tropinfo")) {
					ReadTrophyTypeInfo(stream, trophyGroupCount);
				}

			}

	}
}


void ReadSnowType(FILE *stream)
{
	RequireScriptSlot(SnowCh, 32, "snow types");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			SnowCh++;
			break;
		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: snowtype: %i", SnowCh);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "vSpd"))  SnowInfo[SnowCh].snow_vSpd = atoi(value);
		if (strstr(line, "hSpd"))  SnowInfo[SnowCh].snow_hSpd = atoi(value);
		if (strstr(line, "dens")) {
			const int density = atoi(value);
			if (density < 0 || density > (1 << 20))
				DoHalt("Script loading error: snow density out of range.");
			SnowInfo[SnowCh].snow_dens = density;
		}

		if (strstr(line, "red"))  SnowInfo[SnowCh].snow_r = atoi(value);
		if (strstr(line, "gre"))  SnowInfo[SnowCh].snow_g = atoi(value);
		if (strstr(line, "blu"))  SnowInfo[SnowCh].snow_b = atoi(value);
		if (strstr(line, "alp"))  SnowInfo[SnowCh].snow_a = atoi(value);
		if (strstr(line, "rad"))  SnowInfo[SnowCh].snow_rad = atof(value);

	}
}


void ReadAreaTable (FILE *stream, int areaNumber)
{
	SnowCh = 0;

	TotalAreaInfo = 0;
	char line[256], *value;
	while (fgets(line, 255, stream))
	{
		if (strstr(line, "}")) break;
		if (strstr(line, "{"))
			while (fgets(line, 255, stream))
			{
				if (strstr(line, "}"))
				{
					if (SnowCh){
						int totalSnowTemp=0;
						for (int st = 0; st < SnowCh; st++) {
							if (SnowInfo[st].snow_dens < 0 ||
							    SnowInfo[st].snow_dens > (1 << 20) - totalSnowTemp)
								DoHalt("Script loading error: total snow density out of range.");
							SnowInfo[st].addr = totalSnowTemp;
							totalSnowTemp += SnowInfo[st].snow_dens;
						}
						// Allocate through the memory grid so MEM_DEBUG can track it.
						// Tag as Global because LoadResourcesScript runs once in
						// InitEngine and Snow is never re-allocated on level loads.
						// Freed in ReleaseGlobalResources.
						// Free-before-realloc: _RES.TXT holds a snow sector per
						// area table, so a second sector orphaned the first
						// block (12,600-byte stock leak on snow maps).
						if (Snow) {
						    (void)_HeapFree(Heap, 0, Snow);
						    Snow = nullptr;
						}
						size_t snowBytes = 0;
						if (!CheckedBytes2(static_cast<size_t>(totalSnowTemp), sizeof(TSnowElement), snowBytes))
						    DoHalt("Snow allocation size overflow.");
						Snow = totalSnowTemp > 0
						    ? (TSnowElement*)_HeapAlloc(Heap, 0, snowBytes, MemoryTag::Global)
						    : nullptr;
					}
					TotalAreaInfo++;
					break;
				}
				value = strstr(line, "=");
				if (!value && !strstr(line, "snow")) DoHalt("Script loading error: AreaTable");
				value = value ? value + 1 : line;

				char testBuff[100];
				sprintf(testBuff, "\n TEST: %i", TotalAreaInfo);
				PrintLogVerbose(testBuff);
				sprintf(testBuff, "\n TES2: %i", areaNumber);
				PrintLogVerbose(testBuff);


				if (TotalAreaInfo == areaNumber) {

					if (strstr(line, "snow")) {
						ReadSnowType(stream);
					}

					if (strstr(line, "tree"))
						TreeTable[ScriptIndex(value, 255, "tree table")] = true;

					if (strstr(line, "survivalPlayerX")) SurvivalSpawnX = atoi(value);
					if (strstr(line, "survivalPlayerY")) SurvivalSpawnZ = atoi(value);
					if (strstr(line, "survivalPlayerA")) SurvivalSpawnA = atof(value);
					if (strstr(line, "survivalDinoXMax")) SurvivalDinoSpawn.XMax = atoi(value);
					if (strstr(line, "survivalDinoYMax")) SurvivalDinoSpawn.YMax = atoi(value);
					if (strstr(line, "survivalDinoXMin")) SurvivalDinoSpawn.XMin = atoi(value);
					if (strstr(line, "survivalDinoYMin")) SurvivalDinoSpawn.YMin = atoi(value);

				} else {
					if (strstr(line, "snow")) SkipSector(stream);
				}

			}

	}

}


void ReadWeaponLine(FILE *stream, char *_value, char line[256]) {
	RequireScriptSlot(TotalW, 10, "weapons");
	char *value = _value;

	if (strstr(line, "getAnim"))  WeapInfo[TotalW].getAnim = atoi(value);
	if (strstr(line, "putAnim"))  WeapInfo[TotalW].putAnim = atoi(value);
	if (strstr(line, "shtAnim"))  WeapInfo[TotalW].shtAnim = atoi(value);
	if (strstr(line, "rldAnimFull"))  WeapInfo[TotalW].rldAnim = atoi(value);
	if (strstr(line, "rldAnimPart"))  WeapInfo[TotalW].rldAnimPart = atoi(value);
	if (strstr(line, "rckAnim"))  WeapInfo[TotalW].pmpAnim = atoi(value);
	if (strstr(line, "modAnim"))  WeapInfo[TotalW].modAnim = atoi(value);

	if (strstr(line, "emptyAnim")) WeapInfo[TotalW].emptyAnim = atoi(value);
	if (strstr(line, "getEmpAnim")) WeapInfo[TotalW].getEmpAnim = atoi(value);
	if (strstr(line, "putEmpAnim")) WeapInfo[TotalW].putEmpAnim = atoi(value);

	if (strstr(line, "getAqSnd"))  WeapInfo[TotalW].getAqSnd = atoi(value);
	if (strstr(line, "putAqSnd"))  WeapInfo[TotalW].putAqSnd = atoi(value);
	if (strstr(line, "shtAqSnd"))  WeapInfo[TotalW].shtAqSnd = atoi(value);
	if (strstr(line, "rldAqSndFull"))  WeapInfo[TotalW].rldAqSnd = atoi(value);
	if (strstr(line, "rldAqSndPart"))  WeapInfo[TotalW].rldAqSndPart = atoi(value);
	if (strstr(line, "rckAqSnd"))  WeapInfo[TotalW].pmpAqSnd = atoi(value);
	if (strstr(line, "modAqSnd"))  WeapInfo[TotalW].modAqSnd = atoi(value);

	if (strstr(line, "mustRack")) readBool(value, WeapInfo[TotalW].mustPump);
	if (strstr(line, "autoRack")) readBool(value, WeapInfo[TotalW].autoPump);
	if (strstr(line, "autoReload")) readBool(value, WeapInfo[TotalW].autoReload);

	if (strstr(line, "canRun")) readBool(value, WeapInfo[TotalW].canRun);
	if (strstr(line, "cannotMortal")) readBool(value, WeapInfo[TotalW].cannotMortal);


	if (strstr(line, "semiAuto")) readBool(value, WeapInfo[TotalW].semiauto);
	if (strstr(line, "fullAuto")) readBool(value, WeapInfo[TotalW].fullauto);

	if (strstr(line, "land_power"))  WeapInfo[TotalW].Power = static_cast<float>(atof(value));
	if (strstr(line, "land_veloc"))  WeapInfo[TotalW].Veloc = static_cast<float>(atof(value));
	if (strstr(line, "land_prec"))   WeapInfo[TotalW].Prec = static_cast<float>(atof(value));
	if (strstr(line, "land_fall"))   WeapInfo[TotalW].Fall = static_cast<float>(atof(value));

	if (strstr(line, "aqua_power"))  WeapInfo[TotalW].PowerAq = static_cast<float>(atof(value));
	if (strstr(line, "aqua_veloc"))  WeapInfo[TotalW].VelocAq = static_cast<float>(atof(value));
	if (strstr(line, "aqua_prec"))   WeapInfo[TotalW].PrecAq = static_cast<float>(atof(value));
	if (strstr(line, "aqua_fall"))   WeapInfo[TotalW].FallAq = static_cast<float>(atof(value));

	if (strstr(line, "loud"))   WeapInfo[TotalW].Loud = static_cast<float>(atof(value));
	if (strstr(line, "rate"))   WeapInfo[TotalW].Rate = static_cast<float>(atof(value));
	if (strstr(line, "shots"))  WeapInfo[TotalW].Shots = atoi(value);
	if (strstr(line, "reload")) WeapInfo[TotalW].Reload = atoi(value);
	if (strstr(line, "trace"))  WeapInfo[TotalW].TraceC = atoi(value) - 1;
	if (strstr(line, "optic"))  WeapInfo[TotalW].Optic = static_cast<float>(atof(value));
	//if (strstr(line, "price")) WeapInfo[TotalW].Price =        atoi(value);

	if (strstr(line, "unzoom")) readBool(value, WeapInfo[TotalW].unzoom);
	if (strstr(line, "breathaim")) readBool(value, WeapInfo[TotalW].breathaim);

	if (strstr(line, "harpoon")) readBool(value, WeapInfo[TotalW].harpoon);

	if (strstr(line, "cross")) readBool(value, WeapInfo[TotalW].cross);
	if (strstr(line, "croR")) WeapInfo[TotalW].crossRed = atoi(value);
	if (strstr(line, "croG")) WeapInfo[TotalW].crossGreen = atoi(value);
	if (strstr(line, "croB")) WeapInfo[TotalW].crossBlue = atoi(value);

	if (strstr(line, "shake"))   WeapInfo[TotalW].shake = static_cast<float>(atof(value));

	if (strstr(line, "radar")) readBool(value, WeapInfo[TotalW].onRadar);
	if (strstr(line, "radR")) WeapInfo[TotalW].radarRed = atoi(value);
	if (strstr(line, "radG")) WeapInfo[TotalW].radarGreen = atoi(value);
	if (strstr(line, "radB")) WeapInfo[TotalW].radarBlue = atoi(value);
	if (strstr(line, "radTime"))  WeapInfo[TotalW].radarTime = atoi(value);

	if (strstr(line, "muzzflash")) readBool(value, WeapInfo[TotalW].MuzzFlash);
	if (strstr(line, "chamflash")) readBool(value, WeapInfo[TotalW].ChamFlash);

	if (strstr(line, "recoil"))  WeapInfo[TotalW].recoil = atoi(value);

	if (strstr(line, "retrieve")) readBool(value, WeapInfo[TotalW].retrieve);

	if (strstr(line, "name"))
	{
		value = strstr(line, "'");
		if (!value) DoHalt("Script loading error: Weapons name");
		CopyScriptField(WeapInfo[TotalW].Name, sizeof(WeapInfo[TotalW].Name), value, "Weapons name");
	}

	if (strstr(line, "file"))
	{
		value = strstr(line, "'");
		if (!value) DoHalt("Script loading error: Weapons file");
		CopyScriptField(WeapInfo[TotalW].FName, sizeof(WeapInfo[TotalW].FName), value, "Weapons file");
	}

	if (strstr(line, "gunshot"))
	{
		value = strstr(line, "'");
		if (!value) DoHalt("Script loading error: Weapons gunshot");
		CopyScriptField(WeapInfo[TotalW].SFXName, sizeof(WeapInfo[TotalW].SFXName), value, "Weapons gunshot");
		WeapInfo[TotalW].MGSSound = true;
	}


	if (strstr(line, "pic1"))
	{
		value = strstr(line, "'");
		if (!value) DoHalt("Script loading error: Weapons pic");
		CopyScriptField(WeapInfo[TotalW].BFName, sizeof(WeapInfo[TotalW].BFName), value, "Weapons pic");
	}


	if (strstr(line, "picc"))
	{
		value = strstr(line, "'");
		if (!value) DoHalt("Script loading error: Chamber pic");
		CopyScriptField(WeapInfo[TotalW].CFName, sizeof(WeapInfo[TotalW].CFName), value, "Chamber pic");
		WeapInfo[TotalW].picch = true;
	}


	if (strstr(line, "bModel"))
	{
		value = strstr(line, "'");
		if (!value) DoHalt("Script loading error: Weapons bullet");
		CopyScriptField(WeapInfo[TotalW].BLName, sizeof(WeapInfo[TotalW].BLName), value, "Weapons bullet");
		WeapInfo[TotalW].bullet = true;
	}

}


void ReadWeapons(FILE *stream)
{

	//area
	char tempProjectName[128];
	int timeOfDay, dinSelect;
	for (int a = 0; a < __argc; a++)
	{
		LPSTR s = __argv[a];
		if (strstr(s, "prj="))
		{
			CopyProjectName(tempProjectName, (s + 4));
		}
		if (strstr(s, "dtm=")) timeOfDay = atoi(&s[4]);
		if (strstr(s, "din=")) dinSelect = (atoi(&s[4]) * 1024);
	}

	//time
	for (int a = 0; a < __argc; a++)
	{
		LPSTR s = __argv[a];
	}

  TotalW = 0;

  char line[256], *value;
  while (fgets( line, 255, stream))
  {
    if (strstr(line, "}")) break;
    if (strstr(line, "{"))
      while (fgets( line, 255, stream))
      {
        // Validate before any field or end-of-section calculation indexes
        // WeapInfo; an empty eleventh section must be rejected too.
        RequireScriptSlot(TotalW, 10, "weapons");
        if (strstr(line, "}"))
        {

			WeapInfo[TotalW].radarColour565 = ((WeapInfo[TotalW].radarRed >> 3) << 11) | ((WeapInfo[TotalW].radarGreen >> 2) << 5) | (WeapInfo[TotalW].radarBlue >> 3);
			WeapInfo[TotalW].radarColour555 = ((WeapInfo[TotalW].radarRed >> 3) << 10) | ((WeapInfo[TotalW].radarGreen >> 3) << 5) | (WeapInfo[TotalW].radarBlue >> 3);

			WeapInfo[TotalW].crossColour565 = ((WeapInfo[TotalW].crossRed >> 3) << 11) | ((WeapInfo[TotalW].crossGreen >> 2) << 5) | (WeapInfo[TotalW].crossBlue >> 3);
			WeapInfo[TotalW].crossColour555 = ((WeapInfo[TotalW].crossRed >> 3) << 10) | ((WeapInfo[TotalW].crossGreen >> 3) << 5) | (WeapInfo[TotalW].crossBlue >> 3);

			if (WeapInfo[TotalW].Veloc > WeapInfo[TotalW].VelocAq) WeapInfo[TotalW].aqLow = true;
			else WeapInfo[TotalW].aqLow = false;

          // WeapInfo has 10 entries; an 11th weapon section would overflow.
          if (TotalW >= 10)
            DoHalt("Script loading error: too many weapons (max 10).");
          TotalW++;
          break;
        }
        value = strstr(line, "=");

		if (!value &&
			!strstr(line, "overwrite") &&
			!strstr(line, "addition")) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: Weapons: %s", WeapInfo[TotalW].Name);
			DoHalt(errorBuff);
		}
        value = value ? value + 1 : line;

		ReadWeaponLine(stream, value, line);



		if (strstr(line, "overwrite") || strstr(line, "addition")) {



			bool readThis = true;
			char mapNo1 = tempProjectName[18];
			while (readThis == true) {

				//trophy
				if (tempProjectName[18] == 'h') break;

				if (strstr(line, "area")) {

					switch ((char)tempProjectName[18]) {
					case '1':
						if (tempProjectName[19]) {
							if (!strstr(line, "area0")) readThis = false;//area10
						}
						else if (!strstr(line, "area1")) readThis = false;
						break;
					case '2':
						if (!strstr(line, "area2")) readThis = false;
						break;
					case '3':
						if (!strstr(line, "area3")) readThis = false;
						break;
					case '4':
						if (!strstr(line, "area4")) readThis = false;
						break;
					case '5':
						if (!strstr(line, "area5")) readThis = false;
						break;
					case '6':
						if (!strstr(line, "area6")) readThis = false;
						break;
					case '7':
						if (!strstr(line, "area7")) readThis = false;
						break;
					case '8':
						if (!strstr(line, "area8")) readThis = false;
						break;
					case '9':
						if (!strstr(line, "area9")) readThis = false;
						break;
					}
				}

				if (strstr(line, "time")) {
					switch (timeOfDay) {
					case 0:
						if (!strstr(line, "time0")) readThis = false;
						break;
					case 1:
						if (!strstr(line, "time1")) readThis = false;
						break;
					case 2:
						if (!strstr(line, "time2")) readThis = false;
						break;
					}
				}


				if (strstr(line, "char")) {
					bool readChar = false;
					if (strstr(line, "char0") && (dinSelect & (1 << 10))) readChar = true;
					if (strstr(line, "char1") && (dinSelect & (1 << 11))) readChar = true;
					if (strstr(line, "char2") && (dinSelect & (1 << 12))) readChar = true;
					if (strstr(line, "char3") && (dinSelect & (1 << 13))) readChar = true;
					if (strstr(line, "char4") && (dinSelect & (1 << 14))) readChar = true;
					if (strstr(line, "char5") && (dinSelect & (1 << 15))) readChar = true;
					if (strstr(line, "char6") && (dinSelect & (1 << 16))) readChar = true;
					if (strstr(line, "char7") && (dinSelect & (1 << 17))) readChar = true;
					if (strstr(line, "char8") && (dinSelect & (1 << 18))) readChar = true;
					if (strstr(line, "char9") && (dinSelect & (1 << 19))) readChar = true;
					if (!readChar) readThis = false;
				}

				break;
			}




			if (readThis) {

				while (fgets(line, 255, stream)) {
					if (strstr(line, "}")) break;

					value = strstr(line, "=");
					if (!value){
						char errorBuff[100];
						sprintf(errorBuff, "Script loading error: Weapons: %s", WeapInfo[TotalW].Name);
						DoHalt(errorBuff);
					}
					value++;

					ReadWeaponLine(stream, value, line);

				}

			}
			else {
				SkipSector(stream);
			}
		}


		
		
      }

  }

}



void WipeKillTypes() {
	if (DinoInfo[TotalC].killTypeCount) {
		for (int i = 0; i < DinoInfo[TotalC].killTypeCount; i++) {
			DinoInfo[TotalC].killType[i] = {};
		}
	}
	DinoInfo[TotalC].killTypeCount = 0;
}

/*
void WipeTrophyTypes() {
	if (DinoInfo[TotalC].trophyTypeCount) {
		for (int i = 0; i < DinoInfo[TotalC].trophyTypeCount; i++) {
			DinoInfo[TotalC].trophyType[i] = {};
		}
	}
	DinoInfo[TotalC].trophyTypeCount = 0;
}
*/

void WipeDeathTypes() {
	if (DinoInfo[TotalC].deathTypeCount) {
		for (int i = 0; i < DinoInfo[TotalC].deathTypeCount; i++) {
			DinoInfo[TotalC].deathType[i] = {};
		}
	}
	DinoInfo[TotalC].deathTypeCount = 0;
}

void WipeIdleGroups() {
	if (DinoInfo[TotalC].idleGroupCount) {
		for (int i = 0; i < DinoInfo[TotalC].idleGroupCount; i++) {
			DinoInfo[TotalC].idleGroup[i] = {};
		}
	}
	DinoInfo[TotalC].idleGroupCount = 0;
}

void WipeIdle2Groups() {
	if (DinoInfo[TotalC].idle2GroupCount) {
		for (int i = 0; i < DinoInfo[TotalC].idle2GroupCount; i++) {
			DinoInfo[TotalC].idle2Group[i] = {};
		}
	}
	DinoInfo[TotalC].idle2GroupCount = 0;
}

/*void WipeAvoidances() {

	if (DinoInfo[TotalC].AvoidCount) {

		for (int i = 0; i < DinoInfo[TotalC].AvoidCount; i++) {
			TotalAvoid--;
			Avoid[TotalAvoid] = {};
			DinoInfo[TotalC].Avoidances[i] = 0;
		}
		DinoInfo[TotalC].AvoidCount = 0;

	}

}
*/

void ReadIdleGroupInfo(FILE *stream)
{
	RequireScriptSlot(DinoInfo[TotalC].idleGroupCount, 32, "idle groups");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			DinoInfo[TotalC].idleGroupCount++;
			break;
		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: IdleGroup: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "startChance")) DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].start = atof(value);
		if (strstr(line, "endChance")) DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].end = atof(value);
		if (strstr(line, "randomStarting")) readBool(value, DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].startOnAny);
		if (strstr(line, "randomEnding")) readBool(value, DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].endOnAny);
		if (strstr(line, "instantRepeat")) readBool(value, DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].instantRepeat);

		if (strstr(line, "anim")) {
			//if (idleOverwrite) {
			//	DinoInfo[TotalC].idleCount = 0;
			//	idleOverwrite = false;
			//}
			RequireScriptSlot(DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].count,
			                  32, "idle-group animations");
			DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].anim[DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].count] = atoi(value);
			DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].count++;
		}

	}
}


void ReadIdle2GroupInfo(FILE *stream)
{
	RequireScriptSlot(DinoInfo[TotalC].idle2GroupCount, 32, "secondary idle groups");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			DinoInfo[TotalC].idle2GroupCount++;
			break;
		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: IdleGroup2: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "startChance")) DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].start = atof(value);
		if (strstr(line, "endChance")) DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].end = atof(value);
		if (strstr(line, "randomStarting")) readBool(value, DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].startOnAny);
		if (strstr(line, "randomEnding")) readBool(value, DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].endOnAny);
		if (strstr(line, "instantRepeat")) readBool(value, DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].instantRepeat);

		if (strstr(line, "anim")) {
			//if (idleOverwrite) {
			//	DinoInfo[TotalC].idleCount = 0;
			//	idleOverwrite = false;
			//}
			RequireScriptSlot(DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].count,
			                  32, "secondary idle-group animations");
			DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].anim[DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].count] = atoi(value);
			DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].count++;
		}

	}
}


void ReadDeathTypeInfo(FILE *stream)
{
	RequireScriptSlot(DinoInfo[TotalC].deathTypeCount, 32, "death types");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			DinoInfo[TotalC].deathTypeCount++;
			break;
		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: DeathType: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "dieAnim")) DinoInfo[TotalC].deathType[DinoInfo[TotalC].deathTypeCount].die = atoi(value);
		if (strstr(line, "sleepAnim")) DinoInfo[TotalC].deathType[DinoInfo[TotalC].deathTypeCount].sleep = atoi(value);
		if (strstr(line, "fallAnim")) DinoInfo[TotalC].deathType[DinoInfo[TotalC].deathTypeCount].fall = atoi(value);
		if (strstr(line, "noSleep")) readBool(value, DinoInfo[TotalC].deathType[DinoInfo[TotalC].deathTypeCount].nosleep);

	}
}

void ReadKillTypeInfo(FILE *stream)
{
	RequireScriptSlot(DinoInfo[TotalC].killTypeCount, 32, "kill types");
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			DinoInfo[TotalC].killTypeCount++;
			break;
		}
		value = strstr(line, "=");
		if (!value) {
			char errorBuff[100];
			sprintf(errorBuff, "Script loading error: KillInfo: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "hunterAnim")) DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].hunteranim = atoi(value);
		if (strstr(line, "hunterCarryAnim")) DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].hunterswimanim = atoi(value);
		if (strstr(line, "hunterOffset")) DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].offset = atoi(value);
		if (strstr(line, "eatAnim")) DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].anim = atoi(value);
		if (strstr(line, "hunterSync")) readBool(value, DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].elevate);
		if (strstr(line, "carryCorpse")) readBool(value, DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].carryCorpse);
		if (strstr(line, "dontloop")) readBool(value, DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].dontloop);
		if (strstr(line, "scream")) readBool(value, DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].scream);
		

	}
}







/*
void ReadSpawnInfo(FILE *stream)
{
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			DinoInfo[TotalC].RType0[DinoInfo[TotalC].RegionCount] = TotalRegion;
			DinoInfo[TotalC].RegionCount++;
			TotalRegion++;
			break;
		}
		value = strstr(line, "=");
		if (!value)
			DoHalt("Script loading error");
		value++;

		if (strstr(line, "spawnrate")) Region[TotalRegion].SpawnRate = static_cast<float>(atof(value));
		if (strstr(line, "spawnmax")) Region[TotalRegion].SpawnMax = atoi(value);
		if (strstr(line, "spawnmin")) Region[TotalRegion].SpawnMin = atoi(value);

		if (strstr(line, "xmax")) Region[TotalRegion].XMax = atoi(value);
		if (strstr(line, "xmin")) Region[TotalRegion].XMin = atoi(value);
		if (strstr(line, "ymax")) Region[TotalRegion].YMax = atoi(value);
		if (strstr(line, "ymin")) Region[TotalRegion].YMin = atoi(value);
		if (strstr(line, "styInRgn")) readBool(value, Region[TotalRegion].stayInRegion);

	}
}

void ReadAvoidInfo(FILE *stream)
{
	char *value;
	char line[256];

	while (fgets(line, 255, stream)) {
		if (strstr(line, "}")) {
			if (Avoid[TotalAvoid].XMax ||
				Avoid[TotalAvoid].YMax ||
				Avoid[TotalAvoid].XMin ||
				Avoid[TotalAvoid].YMin) {
				DinoInfo[TotalC].Avoidances[DinoInfo[TotalC].AvoidCount] = TotalAvoid;
				DinoInfo[TotalC].AvoidCount++;
				TotalAvoid++;
			}
			break;
		}
		value = strstr(line, "=");
		if (!value)
			DoHalt("Script loading error");
		value++;

		if (strstr(line, "xmax")) Avoid[TotalAvoid].XMax = atoi(value);
		if (strstr(line, "xmin")) Avoid[TotalAvoid].XMin = atoi(value);
		if (strstr(line, "ymax")) Avoid[TotalAvoid].YMax = atoi(value);
		if (strstr(line, "ymin")) Avoid[TotalAvoid].YMin = atoi(value);

	}

}
*/



void ReadCharacterLine(FILE *stream, char *_value, char line[256], bool &spawnInfoOverwrite, bool &spawnGroupOverwrite,
	bool &idleOverwrite, bool &idle2Overwrite, bool &roarOverwrite, bool &killOverwrite, bool &waterDieOverwrite,
	bool &deathTypeOverwrite, bool &trophyTypeOverwrite, bool &idleGroupOverwrite, bool &idle2GroupOverwrite,
	bool &memberOverwrite) {

	RequireScriptSlot(TotalC, DINOINFO_MAX, "characters");
	char *value = _value;
//	bool overwrite = _overwrite;

	if (strstr(line, "packinfo")) {
		if (memberOverwrite) {
			WipePackMembers2();
			memberOverwrite = false;
		}
		ReadPackMember2(stream);
	}

	if (strstr(line, "packgroup")) {
		if (memberOverwrite) {//reuse same overwrite
			WipePackMembers2();
			memberOverwrite = false;
		}
		ReadPackGroup(stream,line,1);
	}

	if (strstr(line, "tropgroup")) {						
		int gr = atoi(value);
		for (int i = 0; i < trophyTypeCount; i++){
			if (trophyType[i].group == gr) {
				RequireScriptSlot(trophyType[i].ctypeCh, TROPHY2_COUNT,
				                  "trophy character list");
				trophyType[i].ctype[trophyType[i].ctypeCh] = TotalC;
				trophyType[i].ctypeCh++;
				DinoInfo[TotalC].trophy = true;
			}
		}
	}

	if (strstr(line, "callNo")) DinoInfo[TotalC].menuDino = atoi(value);

	if (strstr(line, "mass")) DinoInfo[TotalC].Mass = static_cast<float>(atof(value));
	if (strstr(line, "length")) DinoInfo[TotalC].Length = static_cast<float>(atof(value));
	if (strstr(line, "radius")) DinoInfo[TotalC].Radius = static_cast<float>(atof(value));
	if (strstr(line, "health")) DinoInfo[TotalC].Health0 = atoi(value);
	if (strstr(line, "basescore")) DinoInfo[TotalC].BaseScore = static_cast<float>(atof(value));

	if (strstr(line, "ai")) DinoInfo[TotalC].Clone = atoi(value);

	if (strstr(line, "smellK")) DinoInfo[TotalC].SmellK = static_cast<float>(atof(value));
	if (strstr(line, "hearK")) DinoInfo[TotalC].HearK = static_cast<float>(atof(value));
	if (strstr(line, "lookK")) DinoInfo[TotalC].LookK = static_cast<float>(atof(value));
	if (strstr(line, "shipdelta")) DinoInfo[TotalC].ShDelta = static_cast<float>(atof(value));
	if (strstr(line, "scale0")) DinoInfo[TotalC].Scale0 = atoi(value);
	if (strstr(line, "scaleA")) DinoInfo[TotalC].ScaleA = atoi(value);
	if (strstr(line, "fearCall")) DinoInfo[TotalC].fearCall[ScriptIndex(value, 64, "fear-call table")] = true;
	if (strstr(line, "dontFear")) DinoInfo[TotalC].fearCall[ScriptIndex(value, 64, "fear-call table")] = false;
	if (strstr(line, "maxdepth")) DinoInfo[TotalC].maxDepth = atoi(value);
	if (strstr(line, "maxalt")) DinoInfo[TotalC].maxDepth = atoi(value);
	if (strstr(line, "mindepth")) DinoInfo[TotalC].minDepth = atoi(value);
	if (strstr(line, "minalt")) DinoInfo[TotalC].minDepth = atoi(value);
	if (strstr(line, "spcdepth")) DinoInfo[TotalC].spacingDepth = atoi(value);
	if (strstr(line, "runspd")) DinoInfo[TotalC].runspd = static_cast<float>(atof(value));
	if (strstr(line, "jmpspd")) DinoInfo[TotalC].jmpspd = static_cast<float>(atof(value));
	if (strstr(line, "wlkspd")) DinoInfo[TotalC].wlkspd = static_cast<float>(atof(value));
	if (strstr(line, "swmspd")) DinoInfo[TotalC].swmspd = static_cast<float>(atof(value));
	if (strstr(line, "flyspd")) DinoInfo[TotalC].flyspd = static_cast<float>(atof(value));
	if (strstr(line, "gldspd")) DinoInfo[TotalC].gldspd = static_cast<float>(atof(value));
	if (strstr(line, "tkfspd")) DinoInfo[TotalC].tkfspd = static_cast<float>(atof(value));
	if (strstr(line, "lndspd")) DinoInfo[TotalC].lndspd = static_cast<float>(atof(value));
	if (strstr(line, "divspd")) DinoInfo[TotalC].divspd = static_cast<float>(atof(value));
	if (strstr(line, "aggress")) DinoInfo[TotalC].aggress = atoi(value);
	if (strstr(line, "flydist")) DinoInfo[TotalC].flyDist = atoi(value);
	if (strstr(line, "killdist")) DinoInfo[TotalC].killDist = atoi(value);
	if (strstr(line, "radar")) readBool(value, DinoInfo[TotalC].onRadar);
	if (strstr(line, "dontswimaway")) readBool(value, DinoInfo[TotalC].dontSwimAway);


	if (strstr(line, "survivalIndex")) {
		DinoInfo[TotalC].survivalDino = true;
		RequireScriptSlot(SurvivalIndexCh, 128, "survival character list");
		SurvivalIndex[ScriptIndex(value, 128, "survival index")] = TotalC;
		SurvivalIndexCh++;
	}


	if (strstr(line, "dontBend")) readBool(value, DinoInfo[TotalC].dontBend);
	//if (strstr(line, "bendOffset")) DinoInfo[TotalC].bendOffset = atof(value);

	if (strstr(line, "defensive")) readBool(value, DinoInfo[TotalC].defensive);
	if (strstr(line, "fearShotHear")) readBool(value, DinoInfo[TotalC].fearHearShot);
	if (strstr(line, "fearShotHit")) readBool(value, DinoInfo[TotalC].fearShot);

	if (strstr(line, "weaveRange")) DinoInfo[TotalC].weaveRange = static_cast<float>(atof(value));
	if (strstr(line, "dontWeave")) readBool(value, DinoInfo[TotalC].dontWeave);

	//if (strstr(line, "noMoveNoRotate")) readBool(value, DinoInfo[TotalC].noMoveNoRot);

	if (strstr(line, "radR")) DinoInfo[TotalC].radarRed = atoi(value);
	if (strstr(line, "radG")) DinoInfo[TotalC].radarGreen = atoi(value);
	if (strstr(line, "radB")) DinoInfo[TotalC].radarBlue = atoi(value);
	
	if (strstr(line, "bloR")) DinoInfo[TotalC].bloodRed = atoi(value);
	if (strstr(line, "bloG")) DinoInfo[TotalC].bloodGreen = atoi(value);
	if (strstr(line, "bloB")) DinoInfo[TotalC].bloodBlue = atoi(value);

	if (strstr(line, "collisiondist")) DinoInfo[TotalC].maxGrad = atoi(value);
	if (strstr(line, "runrotatespeed")) DinoInfo[TotalC].rotspdmulti = static_cast<float>(atof(value));

	if (strstr(line, "waterLevel")) DinoInfo[TotalC].waterLevel = atoi(value);


	if (strstr(line, "CamYLand")) DinoInfo[TotalC].camDemoPoint = static_cast<float>(atof(value));
	if (strstr(line, "CamYWater")) DinoInfo[TotalC].camDemoPointWater = static_cast<float>(atof(value));
	if (strstr(line, "CamBaseLand")) DinoInfo[TotalC].camBase = static_cast<float>(atof(value));
	if (strstr(line, "CamBaseWater")) DinoInfo[TotalC].camBaseWater = static_cast<float>(atof(value));


	if (strstr(line, "dogSmell")) readBool(value, DinoInfo[TotalC].dogSmell);

	if (strstr(line, "climbDist")) DinoInfo[TotalC].climbDist = static_cast<float>(atof(value));

	if (strstr(line, "canswim")) readBool(value, DinoInfo[TotalC].canSwim); //check animate subroutines for what this includes. LandBrach needs this attribute, but maybe rename to wade? (and default to off for landbrach ai? maybe?)

	if (strstr(line, "jumpPartFrame1")) DinoInfo[TotalC].partFrame1[CurrentJumpPartIndex()] = 1000 * atoi(value); // x1000
	if (strstr(line, "jumpPartFrame2")) DinoInfo[TotalC].partFrame2[CurrentJumpPartIndex()] = 1000 * atoi(value); // x1000
	if (strstr(line, "jumpPartDist")) DinoInfo[TotalC].partDist[CurrentJumpPartIndex()] = atoi(value);
	if (strstr(line, "jumpPartCnt")) DinoInfo[TotalC].partCnt[CurrentJumpPartIndex()] = atoi(value);
	if (strstr(line, "jumpPartMag")) DinoInfo[TotalC].partMag[CurrentJumpPartIndex()] = atoi(value);
	if (strstr(line, "jumpPartOffset")) DinoInfo[TotalC].partOffset[CurrentJumpPartIndex()] = atoi(value);
	if (strstr(line, "jumpPartAngled")) readBool(value, DinoInfo[TotalC].partAngled[CurrentJumpPartIndex()]);
	if (strstr(line, "jumpPartCircle")) readBool(value, DinoInfo[TotalC].partCircle[CurrentJumpPartIndex()]);

	if (strstr(line, "idlePartFrame1")) DinoInfo[TotalC].partFrame1[CurrentIdlePartIndex()] = 1000 * atoi(value); // x1000
	if (strstr(line, "idlePartFrame2")) DinoInfo[TotalC].partFrame2[CurrentIdlePartIndex()] = 1000 * atoi(value); // x1000
	if (strstr(line, "idlePartDist")) DinoInfo[TotalC].partDist[CurrentIdlePartIndex()] = atoi(value);
	if (strstr(line, "idlePartCnt")) DinoInfo[TotalC].partCnt[CurrentIdlePartIndex()] = atoi(value);
	if (strstr(line, "idlePartMag")) DinoInfo[TotalC].partMag[CurrentIdlePartIndex()] = atoi(value);
	if (strstr(line, "idlePartOffset")) DinoInfo[TotalC].partOffset[CurrentIdlePartIndex()] = atoi(value);
	if (strstr(line, "idlePartAngled")) readBool(value, DinoInfo[TotalC].partAngled[CurrentIdlePartIndex()]);
	if (strstr(line, "idlePartCircle")) readBool(value, DinoInfo[TotalC].partCircle[CurrentIdlePartIndex()]);

	if (strstr(line, "DangerFish")) readBool(value, DinoInfo[TotalC].DangerFish);

	if (strstr(line, "TRexObjCollide")) readBool(value, DinoInfo[TotalC].TRexObjCollide);

	if (strstr(line, "Mystery")) readBool(value, DinoInfo[TotalC].Mystery);
	if (strstr(line, "HideBinoc")) readBool(value, DinoInfo[TotalC].HideBinoc);

	if (strstr(line, "JumpRange")) DinoInfo[TotalC].jumpRange = atoi(value);

	if (strstr(line, "Weapon")) {
		DinoInfo[TotalC].Weapon = ScriptIndex(value, TotalW, "character weapon");
		DinoInfo[TotalC].Reload = WeapInfo[DinoInfo[TotalC].Weapon].Shots;
		if (WeapInfo[DinoInfo[TotalC].Weapon].Reload)
			DinoInfo[TotalC].Reload = WeapInfo[DinoInfo[TotalC].Weapon].Reload;
	}

	if (strstr(line, "runAnim")) DinoInfo[TotalC].runAnim = atoi(value);
	if (strstr(line, "jumpAnim")) DinoInfo[TotalC].jumpAnim = atoi(value);
	if (strstr(line, "walkAnim")) DinoInfo[TotalC].walkAnim = atoi(value);
	if (strstr(line, "swimAnim")) DinoInfo[TotalC].swimAnim = atoi(value);
	if (strstr(line, "flyAnim")) DinoInfo[TotalC].flyAnim = atoi(value);
	if (strstr(line, "diveAnim")) DinoInfo[TotalC].diveAnim = atoi(value);
	if (strstr(line, "glideAnim")) DinoInfo[TotalC].glideAnim = atoi(value);
	if (strstr(line, "takeoffAnim")) DinoInfo[TotalC].takeoffAnim = atoi(value);
	if (strstr(line, "landAnim")) DinoInfo[TotalC].landAnim = atoi(value);
	if (strstr(line, "slideAnim")) DinoInfo[TotalC].slideAnim = atoi(value);
	if (strstr(line, "shakeLAnim")) DinoInfo[TotalC].shakeLandAnim = atoi(value);
	if (strstr(line, "shakeWAnim")) DinoInfo[TotalC].shakeWaterAnim = atoi(value);
	if (strstr(line, "climbAnim")) DinoInfo[TotalC].climbAnim = atoi(value);
	if (strstr(line, "fireAnim")) DinoInfo[TotalC].fireAnim = atoi(value);
	if (strstr(line, "reloadAnim")) DinoInfo[TotalC].reloadAnim = atoi(value);

	
	if (strstr(line, "lookAnim") || strstr(line, "fishIdleAnim")) {
		if (idleOverwrite) {
			DinoInfo[TotalC].lookCount = 0;
			idleOverwrite = false;
		}
		RequireScriptSlot(DinoInfo[TotalC].lookCount, 32, "look animations");
		DinoInfo[TotalC].lookAnim[DinoInfo[TotalC].lookCount] = atoi(value);
		DinoInfo[TotalC].lookCount++;
	}

	if (strstr(line, "smellAnim")) {
		if (idle2Overwrite) {
			DinoInfo[TotalC].smellCount = 0;
			idle2Overwrite = false;
		}
		RequireScriptSlot(DinoInfo[TotalC].smellCount, 32, "smell animations");
		DinoInfo[TotalC].smellAnim[DinoInfo[TotalC].smellCount] = atoi(value);
		DinoInfo[TotalC].smellCount++;
	}
	

	if (strstr(line, "roarAnim")) {
		if (roarOverwrite) {
			DinoInfo[TotalC].roarCount = 0;
			roarOverwrite = false;
		}
		RequireScriptSlot(DinoInfo[TotalC].roarCount, 32, "roar animations");
		DinoInfo[TotalC].roarAnim[DinoInfo[TotalC].roarCount] = atoi(value);
		DinoInfo[TotalC].roarCount++;
	}

	if (strstr(line, "waterDAnim")) {
		if (waterDieOverwrite) {
			DinoInfo[TotalC].waterDieCount = 0;
			waterDieOverwrite = false;
		}
		RequireScriptSlot(DinoInfo[TotalC].waterDieCount, 32, "water-death animations");
		DinoInfo[TotalC].waterDieAnim[DinoInfo[TotalC].waterDieCount] = atoi(value);
		DinoInfo[TotalC].waterDieCount++;
	}





	/*
	if (strstr(line, "trophy")) {
		if (!DinoInfo[TotalC].trophyCode) {
			bool temp = false;
			readBool(value, temp);
			if (temp) {
				TotalTrophy++;
				DinoInfo[TotalC].trophyCode = TotalTrophy;
				TrophyIndex[TotalTrophy] = TotalC;
			}
		}
		readBool(value, DinoInfo[TotalC].trophySession);
	}
	*/

	if (strstr(line, "tropinfo")) {
		ReadTrophyTypeInfo(stream, -1);
	}


	if (strstr(line, "name"))
	{
		value = strstr(line, "'");
		if (!value) DoHalt("Script loading error: Characters name");
		CopyScriptField(DinoInfo[TotalC].Name, sizeof(DinoInfo[TotalC].Name), value, "Characters name");
	}

	if (strstr(line, "file"))
	{
		value = strstr(line, "'");
		if (!value) DoHalt("Script loading error: Characters file");
		CopyScriptField(DinoInfo[TotalC].FName, sizeof(DinoInfo[TotalC].FName), value, "Characters file");
	}

	
	if (strstr(line, "killtype")) {

		if (killOverwrite) {
			WipeKillTypes();
			killOverwrite = false;
		}

		ReadKillTypeInfo(stream);
	}

	if (strstr(line, "deathtype")) {

		if (deathTypeOverwrite) {
			WipeDeathTypes();
			deathTypeOverwrite = false;
		}

		ReadDeathTypeInfo(stream);
	}

	if (strstr(line, "idlegroup")) {

		if (idleGroupOverwrite) {
			WipeIdleGroups();
			idleGroupOverwrite = false;
		}

		ReadIdleGroupInfo(stream);
	}

	if (strstr(line, "waterIgroup")) {

		if (idle2GroupOverwrite) {
			WipeIdle2Groups();
			idle2GroupOverwrite = false;
		}

		ReadIdle2GroupInfo(stream);
	}

	if (strstr(line, "spawninfo")) {
		if (g_GameMode == GameMode::SurvivalMode) {
			SkipSector(stream);
		} else {
			if (spawnInfoOverwrite) {
				WipeSpawnInfo();
				spawnInfoOverwrite = false;
			}
			ReadSpawnInfo(stream);
		}
	}

	if (strstr(line, "spawngroup")) {
		if (g_GameMode == GameMode::SurvivalMode) {
			SkipSector(stream);
		} else {
			if (spawnGroupOverwrite) {
				WipeSpawnInfo();
				spawnGroupOverwrite = false;
			}
			ReadSpawnGroup(stream, line, 1);
		}
	}

	/*
	if (strstr(line, "avoid")) {

		if (avoidOverwrite) {
			WipeAvoidances();
			avoidOverwrite = false;
		}
		ReadAvoidInfo(stream);
		
	}
	*/


}


void ReadCharacters(FILE *stream)
{


	//area
	char tempProjectName[128];
	int timeOfDay, dinSelect;
	for (int a = 0; a < __argc; a++)
	{
		LPSTR s = __argv[a];
		if (strstr(s, "prj="))
		{
			CopyProjectName(tempProjectName, (s + 4));
		}
		if (strstr(s, "dtm=")) timeOfDay = atoi(&s[4]);
		if (strstr(s, "din=")) dinSelect = (atoi(&s[4]) * 1024);
	}

	//time
	for (int a = 0; a < __argc; a++)
	{
		LPSTR s = __argv[a];
	}



  char line[256], *value;
  while (fgets( line, 255, stream))
  {
    if (strstr(line, "}")) break;
    if (strstr(line, "{"))
      while (fgets( line, 255, stream))
      {
        // Validate before any field or end-of-section calculation indexes
        // DinoInfo; an empty 129th section must be rejected too.
        RequireScriptSlot(TotalC, DINOINFO_MAX, "characters");

        if (strstr(line, "}"))
        {

			if (g_GameMode == GameMode::SurvivalMode && DinoInfo[TotalC].survivalDino)
			{
				DinoInfo[TotalC].SpawnInfoCh = 1;
				DinoInfo[TotalC].SpawnInfo[0].spawnGroup = 0;
			}

			/*
			// if ((tempProjectName[18] == 'h' && !DinoInfo[TotalC].trophyNo) || (!DinoInfo[TotalC].SpawnInfoCh && TotalC > 0)) { //totalc = 0 for hunter char
		  if (tempProjectName[18] != 'h' && !DinoInfo[TotalC].SpawnInfoCh && TotalC > 0) { //totalc = 0 for hunter char
				  
			  //WipeRegions();
			  //WipeAvoidances();

				  DinoInfo[TotalC] = {};
				  DinoInfo[TotalC].Scale0 = 800;
				  DinoInfo[TotalC].ScaleA = 600;
				  DinoInfo[TotalC].ShDelta = 0;
				  break;
		  }
		  */
		  
		  //int _ctype = DinoInfo[TotalC].AI;
		  //if (mapamb) {
		  //	TotalMA ++;
		  //	DinoInfo[TotalC].AI = -TotalMA;
		  //	_ctype = AI_FINAL + TotalMA;
		  //}
          //AI_to_CIndex[_ctype] = TotalC;
		  if (DinoInfo[TotalC].Clone == AI_MOSA ||
			  DinoInfo[TotalC].Clone == AI_FISH) {
			  DinoInfo[TotalC].Aquatic = true;
		  } else {
			  DinoInfo[TotalC].Aquatic = false;
		  }
	
		  DinoInfo[TotalC].radarColour565 = ((DinoInfo[TotalC].radarRed>>3) << 11) | ((DinoInfo[TotalC].radarGreen>>2) << 5) | (DinoInfo[TotalC].radarBlue>>3);
		  DinoInfo[TotalC].radarColour555 = ((DinoInfo[TotalC].radarRed >> 3) << 10) | ((DinoInfo[TotalC].radarGreen >> 3) << 5) | (DinoInfo[TotalC].radarBlue >> 3);
		 

		  // DinoInfo has DINOINFO_MAX (128) entries.
		  if (TotalC >= DINOINFO_MAX)
		    DoHalt("Script loading error: too many characters.");
		  TotalC++;

          break;

        }

			value = strstr(line, "=");
			if (!value &&
				!strstr(line, "overwrite") &&
				!strstr(line, "addition") &&
				!strstr(line, "spawninfo") &&
				!strstr(line, "spawngroup") &&
				!strstr(line, "killtype") &&
				//!strstr(line, "avoid") &&
				!strstr(line, "tropinfo") &&
				!strstr(line, "deathtype") &&
				!strstr(line, "idlegroup") &&
				!strstr(line, "packinfo") &&
				!strstr(line, "packgroup") &&
				!strstr(line, "waterIgroup"))
				DoHalt("Script loading error: Characters");
			value = value ? value + 1 : line;


			bool temp, temp3, temp4, temp5, temp6, temp7, temp8, temp9, temp10, temp11, temp12, temp13;
			temp = false;
			temp3 = false;
			temp4 = false;
			temp5 = false;
			temp6 = false;
			temp7 = false;
			temp8 = false;
			temp9 = false;
			temp10 = false;
			temp11 = false;
			temp12 = false;
			temp13 = false;
			ReadCharacterLine(stream, value, line, temp, temp3, temp4, temp5, temp6, temp7, temp8, temp9, temp12, temp10, temp11, temp13);

			if (strstr(line, "overwrite") || strstr(line, "addition")) {



				bool readThis = true;
				char mapNo1 = tempProjectName[18];
				while (readThis == true) {
					
					//trophy
					if (tempProjectName[18] == 'h') break;

					if (strstr(line, "area")) {

						switch ((char)tempProjectName[18]) {
						case '1':
							if (tempProjectName[19]) {
								if (!strstr(line, "area0")) readThis = false;//area10
							}
							else if (!strstr(line, "area1")) readThis = false;
							break;
						case '2':
							if (!strstr(line, "area2")) readThis = false;
							break;
						case '3':
							if (!strstr(line, "area3")) readThis = false;
							break;
						case '4':
							if (!strstr(line, "area4")) readThis = false;
							break;
						case '5':
							if (!strstr(line, "area5")) readThis = false;
							break;
						case '6':
							if (!strstr(line, "area6")) readThis = false;
							break;
						case '7':
							if (!strstr(line, "area7")) readThis = false;
							break;
						case '8':
							if (!strstr(line, "area8")) readThis = false;
							break;
						case '9':
							if (!strstr(line, "area9")) readThis = false;
							break;
						}
					}

					if (strstr(line, "time")) {
						switch (timeOfDay) {
						case 0:
							if (!strstr(line, "time0")) readThis = false;
							break;
						case 1:
							if (!strstr(line, "time1")) readThis = false;
							break;
						case 2:
							if (!strstr(line, "time2")) readThis = false;
							break;
						}
					}


					if (strstr(line, "char")){
						bool readChar = false;
						if (strstr(line, "char0") && (dinSelect & (1<<10))) readChar = true;
						if (strstr(line, "char1") && (dinSelect & (1<<11))) readChar = true;
						if (strstr(line, "char2") && (dinSelect & (1<<12))) readChar = true;
						if (strstr(line, "char3") && (dinSelect & (1<<13))) readChar = true;
						if (strstr(line, "char4") && (dinSelect & (1<<14))) readChar = true;
						if (strstr(line, "char5") && (dinSelect & (1<<15))) readChar = true;
						if (strstr(line, "char6") && (dinSelect & (1<<16))) readChar = true;
						if (strstr(line, "char7") && (dinSelect & (1<<17))) readChar = true;
						if (strstr(line, "char8") && (dinSelect & (1<<18))) readChar = true;
						if (strstr(line, "char9") && (dinSelect & (1<<19))) readChar = true;
						if (!readChar) readThis = false;
					}
					
					break;
				}




				if (readThis) {

					bool spawnInfoOverwrite = strstr(line, "overwrite");
					bool spawnGroupOverwrite = strstr(line, "overwrite");
					// bool avoidOverwrite = strstr(line, "overwrite");
					bool idleOverwrite = strstr(line, "overwrite");
					bool killOverwrite = strstr(line, "overwrite");
					bool idle2Overwrite = strstr(line, "overwrite");
					bool roarOverwrite = strstr(line, "overwrite");
					bool waterDieOverwrite = strstr(line, "overwrite");
					bool deathTypeOverwrite = strstr(line, "overwrite");
					bool trophyTypeOverwrite = strstr(line, "overwrite");
					bool idleGroupOverwrite = strstr(line, "overwrite");
					bool idle2GroupOverwrite = strstr(line, "overwrite");
					bool memberOverwrite = strstr(line, "overwrite");

					while (fgets(line, 255, stream)) {
						if (strstr(line, "}")) break;

						value = strstr(line, "=");
						if (!value
							&& !strstr(line, "spawninfo")
							&& !strstr(line, "spawngroup")
							//&& !strstr(line, "avoid")
							&& !strstr(line, "deathtype")
							&& !strstr(line, "idlegroup")
							&& !strstr(line, "waterIgroup")
							&& !strstr(line, "tropinfo")
							&& !strstr(line, "packinfo")
							&& !strstr(line, "packgroup")
							&& !strstr(line, "killtype"))
							DoHalt("Script loading error: Characters");
						value = value ? value + 1 : line;

						ReadCharacterLine(stream, value, line, spawnInfoOverwrite, spawnGroupOverwrite,
							idleOverwrite, idle2Overwrite, roarOverwrite, killOverwrite,
							waterDieOverwrite, deathTypeOverwrite, trophyTypeOverwrite,
							idleGroupOverwrite, idle2GroupOverwrite,memberOverwrite);

					}

				} else {
					SkipSector(stream);
				}
			}

			/*
			if (strstr(line, "pic"))
			{
				value = strstr(line, "'");
				if (!value) DoHalt("Script loading error");
				value[strlen(value) - 2] = 0;
				strcpy(DinoInfo[TotalC].PName, &value[1]);
			}
			*/


		//}

      }

  }
}



void LoadResourcesScript()
{

	SurvivalIndexCh = 0;

	//mosh/dimet waterlevel 50
	//gall waterlevel 100

	//these ai can detect player with sight or scent. Maybe make this a res option at some point.
	AIInfo[AI_PARA].sniffer = true;
	AIInfo[AI_ANKY].sniffer = true;
	AIInfo[AI_STEGO].sniffer = true;
	AIInfo[AI_CHASM].sniffer = true;
	AIInfo[AI_ALLO].sniffer = true;
	AIInfo[AI_VELO].sniffer = true;
	AIInfo[AI_SPINO].sniffer = true;
	AIInfo[AI_CERAT].sniffer = true;
	AIInfo[AI_TREX].sniffer = true;
	AIInfo[AI_PACH].sniffer = true;
	AIInfo[AI_MICRO].sniffer = true;
	AIInfo[AI_TITAN].sniffer = true;
	AIInfo[AI_BRONT].sniffer = true;
	AIInfo[AI_HOG].sniffer = true;
	AIInfo[AI_WOLF].sniffer = true;
	AIInfo[AI_RHINO].sniffer = true;
	AIInfo[AI_DEER].sniffer = true;
	AIInfo[AI_SMILO].sniffer = true;
	AIInfo[AI_MAMM].sniffer = true;
	AIInfo[AI_BEAR].sniffer = true;

	AIInfo[AI_HUNTDOG].targetDistance = 8048.f;
	AIInfo[AI_HUNTDOG].noWayCntMin = 8;
	AIInfo[AI_HUNTDOG].noFindWayMed = 44;
	AIInfo[AI_HUNTDOG].noFindWayRange = 80;
	AIInfo[AI_HUNTDOG].targetBendRotSpd = 3.0f;
	AIInfo[AI_HUNTDOG].targetBendMin = 2.f;
	AIInfo[AI_HUNTDOG].targetBendDelta1 = 1600.f;
	AIInfo[AI_HUNTDOG].targetBendDelta2 = 1200.f;
	AIInfo[AI_HUNTDOG].walkTargetGammaRot = 12.0f;
	AIInfo[AI_HUNTDOG].targetGammaRot = 8.0f;
	AIInfo[AI_HUNTDOG].idleStart = 120;
	AIInfo[AI_HUNTDOG].yBetaGamma4 = 0.4f;
	AIInfo[AI_HUNTDOG].idleStartD = 128;

	
	AIInfo[AI_PARA].targetDistance = 8048.f;
	AIInfo[AI_PARA].agressMulti = 128;
	AIInfo[AI_PARA].noWayCntMin = 8;
	AIInfo[AI_PARA].noFindWayMed = 44;
	AIInfo[AI_PARA].noFindWayRange = 80;
	AIInfo[AI_PARA].targetBendRotSpd = 3.0f;
	AIInfo[AI_PARA].targetBendMin = 2.f;
	AIInfo[AI_PARA].targetBendDelta1 = 1600.f;
	AIInfo[AI_PARA].targetBendDelta2 = 1200.f;
	AIInfo[AI_PARA].walkTargetGammaRot = 12.0f;
	AIInfo[AI_PARA].targetGammaRot = 8.0f;
	AIInfo[AI_PARA].idleStart = 120;
	AIInfo[AI_PARA].yBetaGamma1 = 128;
	AIInfo[AI_PARA].yBetaGamma2 = 64;
	AIInfo[AI_PARA].yBetaGamma3 = 0.6f;
	AIInfo[AI_PARA].yBetaGamma4 = 0.4f;
	AIInfo[AI_PARA].tGAIncrement = 3.f;
	AIInfo[AI_PARA].rot1 = 0.2f;
	AIInfo[AI_PARA].rot2 = 1.0f;
	//AIInfo[AI_PARA].weaveRange = 0;
	AIInfo[AI_PARA].pWMin = 2048;
	//waterLevel = 160;


	AIInfo[AI_BRONT].iceAge = true;
	AIInfo[AI_BRONT].targetDistance = 8048.f;
	AIInfo[AI_BRONT].agressMulti = 256;
	AIInfo[AI_BRONT].noWayCntMin = 8;
	AIInfo[AI_BRONT].noFindWayMed = 48;
	AIInfo[AI_BRONT].noFindWayRange = 80;
	AIInfo[AI_BRONT].targetBendRotSpd = 3.5f;
	AIInfo[AI_BRONT].targetBendMin = 2.f;
	AIInfo[AI_BRONT].targetBendDelta1 = 1600.f;
	AIInfo[AI_BRONT].targetBendDelta2 = 1200.f;
	AIInfo[AI_BRONT].walkTargetGammaRot = 12.0f;
	AIInfo[AI_BRONT].targetGammaRot = 8.0f;
	AIInfo[AI_BRONT].idleStart = 124;
	AIInfo[AI_BRONT].yBetaGamma1 = 128;
	AIInfo[AI_BRONT].yBetaGamma2 = 64;
	AIInfo[AI_BRONT].yBetaGamma3 = 0.6f;
	AIInfo[AI_BRONT].yBetaGamma4 = 0.3f;
	AIInfo[AI_BRONT].tGAIncrement = 3.f;
	AIInfo[AI_BRONT].rot1 = 0.2f;
	AIInfo[AI_BRONT].rot2 = 1.0f;
	//AIInfo[AI_BRONT].weaveRange = 3072;
	AIInfo[AI_BRONT].pWMin = 2048;

	AIInfo[AI_HOG].iceAge = true;
	AIInfo[AI_HOG].targetDistance = 8048.f;
	AIInfo[AI_HOG].agressMulti = 256;
	AIInfo[AI_HOG].noWayCntMin = 8;
	AIInfo[AI_HOG].noFindWayMed = 48;
	AIInfo[AI_HOG].noFindWayRange = 80;
	AIInfo[AI_HOG].targetBendRotSpd = 3.5f;
	AIInfo[AI_HOG].targetBendMin = 2.f;
	AIInfo[AI_HOG].targetBendDelta1 = 1600.f;
	AIInfo[AI_HOG].targetBendDelta2 = 1200.f;
	AIInfo[AI_HOG].walkTargetGammaRot = 12.0f;
	AIInfo[AI_HOG].targetGammaRot = 8.0f;
	AIInfo[AI_HOG].idleStart = 124;
	AIInfo[AI_HOG].yBetaGamma1 = 128;
	AIInfo[AI_HOG].yBetaGamma2 = 64;
	AIInfo[AI_HOG].yBetaGamma3 = 0.6f;
	AIInfo[AI_HOG].yBetaGamma4 = 0.3f;
	AIInfo[AI_HOG].tGAIncrement = 3.f;
	AIInfo[AI_HOG].rot1 = 0.3f;
	AIInfo[AI_HOG].rot2 = 1.4f;
	//AIInfo[AI_HOG].weaveRange = 3072;
	AIInfo[AI_HOG].pWMin = 2048;


	AIInfo[AI_BEAR].iceAge = true;
	AIInfo[AI_BEAR].targetDistance = 8048.f;
	AIInfo[AI_BEAR].agressMulti = 256;
	AIInfo[AI_BEAR].noWayCntMin = 8;
	AIInfo[AI_BEAR].noFindWayMed = 48;
	AIInfo[AI_BEAR].noFindWayRange = 80;
	AIInfo[AI_BEAR].targetBendRotSpd = 3.5f;
	AIInfo[AI_BEAR].targetBendMin = 2.f;
	AIInfo[AI_BEAR].targetBendDelta1 = 1600.f;
	AIInfo[AI_BEAR].targetBendDelta2 = 1200.f;
	AIInfo[AI_BEAR].walkTargetGammaRot = 12.0f;
	AIInfo[AI_BEAR].targetGammaRot = 8.0f;
	AIInfo[AI_BEAR].idleStart = 124;
	AIInfo[AI_BEAR].yBetaGamma1 = 128;
	AIInfo[AI_BEAR].yBetaGamma2 = 64;
	AIInfo[AI_BEAR].yBetaGamma3 = 0.6f;
	AIInfo[AI_BEAR].yBetaGamma4 = 0.3f;
	AIInfo[AI_BEAR].tGAIncrement = 3.f;
	AIInfo[AI_BEAR].rot1 = 0.2f;
	AIInfo[AI_BEAR].rot2 = 1.0f;
	//AIInfo[AI_BEAR].weaveRange = 0;
	AIInfo[AI_BEAR].pWMin = 2048;
	//AIInfo[AI_BEAR].carnivore = true; //Bear has more in common with herbivore ai

	AIInfo[AI_RHINO].iceAge = true;
	AIInfo[AI_RHINO].targetDistance = 8048.f;
	AIInfo[AI_RHINO].agressMulti = 256;
	AIInfo[AI_RHINO].noWayCntMin = 8;
	AIInfo[AI_RHINO].noFindWayMed = 48;
	AIInfo[AI_RHINO].noFindWayRange = 80;
	AIInfo[AI_RHINO].targetBendRotSpd = 3.5f;
	AIInfo[AI_RHINO].targetBendMin = 2.f;
	AIInfo[AI_RHINO].targetBendDelta1 = 1600.f;
	AIInfo[AI_RHINO].targetBendDelta2 = 1200.f;
	AIInfo[AI_RHINO].walkTargetGammaRot = 12.0f;
	AIInfo[AI_RHINO].targetGammaRot = 8.0f;
	AIInfo[AI_RHINO].idleStart = 124;
	AIInfo[AI_RHINO].yBetaGamma1 = 128;
	AIInfo[AI_RHINO].yBetaGamma2 = 64;
	AIInfo[AI_RHINO].yBetaGamma3 = 0.6f;
	AIInfo[AI_RHINO].yBetaGamma4 = 0.3f;
	AIInfo[AI_RHINO].tGAIncrement = 3.f;
	AIInfo[AI_RHINO].rot1 = 0.3f;
	AIInfo[AI_RHINO].rot2 = 1.2f;
	//AIInfo[AI_RHINO].weaveRange = 3072;
	AIInfo[AI_RHINO].pWMin = 2048;


	AIInfo[AI_DEER].iceAge = true;
	AIInfo[AI_DEER].targetDistance = 8048.f;
	AIInfo[AI_DEER].agressMulti = 256;
	AIInfo[AI_DEER].noWayCntMin = 12;
	AIInfo[AI_DEER].noFindWayMed = 32;
	AIInfo[AI_DEER].noFindWayRange = 60;
	AIInfo[AI_DEER].targetBendRotSpd = 2.f;
	AIInfo[AI_DEER].targetBendMin = 3.f;
	AIInfo[AI_DEER].targetBendDelta1 = 2000.f;
	AIInfo[AI_DEER].targetBendDelta2 = 2000.f;
	AIInfo[AI_DEER].walkTargetGammaRot = 16.0f;
	AIInfo[AI_DEER].targetGammaRot = 10.0f;
	AIInfo[AI_DEER].idleStart = 124;
	AIInfo[AI_DEER].yBetaGamma1 = 128;
	AIInfo[AI_DEER].yBetaGamma2 = 64;
	AIInfo[AI_DEER].yBetaGamma3 = 0.6f;
	AIInfo[AI_DEER].yBetaGamma4 = 0.4f;
	AIInfo[AI_DEER].tGAIncrement = 3.f;
	AIInfo[AI_DEER].rot1 = 0.2f;
	AIInfo[AI_DEER].rot2 = 1.0f;
	//AIInfo[AI_DEER].weaveRange = 0;
	AIInfo[AI_DEER].pWMin = 2048;

	AIInfo[AI_MAMM].iceAge = true;
	AIInfo[AI_MAMM].targetDistance = 8048.f;
	AIInfo[AI_MAMM].agressMulti = 256;
	AIInfo[AI_MAMM].noWayCntMin = 12;
	AIInfo[AI_MAMM].noFindWayMed = 32;
	AIInfo[AI_MAMM].noFindWayRange = 60;
	AIInfo[AI_MAMM].targetBendRotSpd = 2.f;
	AIInfo[AI_MAMM].targetBendMin = 3.f;
	AIInfo[AI_MAMM].targetBendDelta1 = 2000.f;
	AIInfo[AI_MAMM].targetBendDelta2 = 2000.f;
	AIInfo[AI_MAMM].walkTargetGammaRot = 16.0f;
	AIInfo[AI_MAMM].targetGammaRot = 10.0f;
	AIInfo[AI_MAMM].idleStart = 124;
	AIInfo[AI_MAMM].yBetaGamma1 = 128;
	AIInfo[AI_MAMM].yBetaGamma2 = 64;
	AIInfo[AI_MAMM].yBetaGamma3 = 0.6f;
	AIInfo[AI_MAMM].yBetaGamma4 = 0.4f;
	AIInfo[AI_MAMM].tGAIncrement = 3.f;
	AIInfo[AI_MAMM].rot1 = 0.2f;
	AIInfo[AI_MAMM].rot2 = 1.0f;
	//AIInfo[AI_MAMM].weaveRange = 0;
	AIInfo[AI_MAMM].pWMin = 3048;




	AIInfo[AI_SMILO].iceAge = true;
	AIInfo[AI_SMILO].carnivore = true;
	AIInfo[AI_SMILO].targetDistance = 8048.f;
	AIInfo[AI_SMILO].agressMulti = 256;
	AIInfo[AI_SMILO].noWayCntMin = 8;
	AIInfo[AI_SMILO].noFindWayMed = 48;
	AIInfo[AI_SMILO].noFindWayRange = 80;
	AIInfo[AI_SMILO].targetBendRotSpd = 3.5f;
	AIInfo[AI_SMILO].targetBendMin = 3.f;
	AIInfo[AI_SMILO].targetBendDelta1 = 1600.f;
	AIInfo[AI_SMILO].targetBendDelta2 = 1200.f;
	AIInfo[AI_SMILO].walkTargetGammaRot = 12.0f;
	AIInfo[AI_SMILO].targetGammaRot = 8.0f;
	AIInfo[AI_SMILO].idleStartD = 120;
	AIInfo[AI_SMILO].yBetaGamma1 = 128;
	AIInfo[AI_SMILO].yBetaGamma2 = 64;
	AIInfo[AI_SMILO].yBetaGamma3 = 0.6f;
	AIInfo[AI_SMILO].yBetaGamma4 = 0.3f;
	AIInfo[AI_SMILO].tGAIncrement = 3.f;
	AIInfo[AI_SMILO].rot1 = 0.3f;
	AIInfo[AI_SMILO].rot2 = 1.5f;
	//AIInfo[AI_SMILO].weaveRange = 3072;
	AIInfo[AI_SMILO].pWMin = 2048;
	AIInfo[AI_SMILO].jumper = true;




	AIInfo[AI_WOLF].iceAge = true;
	AIInfo[AI_WOLF].carnivore = true;
	AIInfo[AI_WOLF].targetDistance = 8048.f;
	AIInfo[AI_WOLF].agressMulti = 256;
	AIInfo[AI_WOLF].noWayCntMin = 8;
	AIInfo[AI_WOLF].noFindWayMed = 48;
	AIInfo[AI_WOLF].noFindWayRange = 80;
	AIInfo[AI_WOLF].targetBendRotSpd = 3.5f;
	AIInfo[AI_WOLF].targetBendMin = 3.f;
	AIInfo[AI_WOLF].targetBendDelta1 = 1600.f;
	AIInfo[AI_WOLF].targetBendDelta2 = 1200.f;
	AIInfo[AI_WOLF].walkTargetGammaRot = 12.0f;
	AIInfo[AI_WOLF].targetGammaRot = 8.0f;
	AIInfo[AI_WOLF].idleStartD = 120;
	AIInfo[AI_WOLF].yBetaGamma1 = 128;
	AIInfo[AI_WOLF].yBetaGamma2 = 64;
	AIInfo[AI_WOLF].yBetaGamma3 = 0.6f;
	AIInfo[AI_WOLF].yBetaGamma4 = 0.3f;
	AIInfo[AI_WOLF].tGAIncrement = 3.f;
	AIInfo[AI_WOLF].rot1 = 0.4f;
	AIInfo[AI_WOLF].rot2 = 1.5f;
	//AIInfo[AI_WOLF].weaveRange = 3072;
	AIInfo[AI_WOLF].pWMin = 2048;
	AIInfo[AI_WOLF].jumper = true;





	AIInfo[AI_ANKY].targetDistance = 8048.f;
	AIInfo[AI_ANKY].agressMulti = 128;
	AIInfo[AI_ANKY].noWayCntMin = 12;
	AIInfo[AI_ANKY].noFindWayMed = 32;
	AIInfo[AI_ANKY].noFindWayRange = 60;
	AIInfo[AI_ANKY].targetBendRotSpd = 2.f;
	AIInfo[AI_ANKY].targetBendMin = 3.f;
	AIInfo[AI_ANKY].targetBendDelta1 = 2000.f;
	AIInfo[AI_ANKY].targetBendDelta2 = AIInfo[AI_ANKY].targetBendDelta1;
	AIInfo[AI_ANKY].walkTargetGammaRot = 16.0f;
	AIInfo[AI_ANKY].targetGammaRot = 10.0f;
	AIInfo[AI_ANKY].idleStart = 120;
	AIInfo[AI_ANKY].yBetaGamma1 = 128;
	AIInfo[AI_ANKY].yBetaGamma2 = 64;
	AIInfo[AI_ANKY].yBetaGamma3 = 0.6f;
	AIInfo[AI_ANKY].yBetaGamma4 = 0.4f;
	AIInfo[AI_ANKY].tGAIncrement = 3.f;
	AIInfo[AI_ANKY].rot1 = 0.2f;
	AIInfo[AI_ANKY].rot2 = 1.0f;
	//AIInfo[AI_ANKY].weaveRange = 0;
	AIInfo[AI_ANKY].pWMin = 2048;
	//waterLevel = 60;

	AIInfo[AI_PACH].targetDistance = 6048.f;
	AIInfo[AI_PACH].agressMulti = 128;
	AIInfo[AI_PACH].noWayCntMin = 12;
	AIInfo[AI_PACH].noFindWayMed = 32;
	AIInfo[AI_PACH].noFindWayRange = 60;
	AIInfo[AI_PACH].targetBendRotSpd = 3.0f;
	AIInfo[AI_PACH].targetBendMin = 2.f;
	AIInfo[AI_PACH].targetBendDelta1 = 1600.f;
	AIInfo[AI_PACH].targetBendDelta2 = 1200.f;
	AIInfo[AI_PACH].walkTargetGammaRot = 12.0f;
	AIInfo[AI_PACH].targetGammaRot = 8.0f;
	AIInfo[AI_PACH].idleStart = 120;
	AIInfo[AI_PACH].yBetaGamma1 = 128;
	AIInfo[AI_PACH].yBetaGamma2 = 64;
	AIInfo[AI_PACH].yBetaGamma3 = 0.6f;
	AIInfo[AI_PACH].yBetaGamma4 = 0.4f;
	AIInfo[AI_PACH].tGAIncrement = 3.f;
	AIInfo[AI_PACH].rot1 = 0.2f;
	AIInfo[AI_PACH].rot2 = 1.0f;
	//AIInfo[AI_PACH].weaveRange = 0;
	AIInfo[AI_PACH].pWMin = 2048;
	//waterLevel = 140;

	AIInfo[AI_STEGO].targetDistance = 8048.f;
	AIInfo[AI_STEGO].agressMulti = 128;
	AIInfo[AI_STEGO].noWayCntMin = 12;
	AIInfo[AI_STEGO].noFindWayMed = 32;
	AIInfo[AI_STEGO].noFindWayRange = 60;
	AIInfo[AI_STEGO].targetBendRotSpd = 2.f;
	AIInfo[AI_STEGO].targetBendMin = 3.f;
	AIInfo[AI_STEGO].targetBendDelta1 = 2000.f;
	AIInfo[AI_STEGO].targetBendDelta2 = AIInfo[AI_STEGO].targetBendDelta1;
	AIInfo[AI_STEGO].walkTargetGammaRot = 16.0f;
	AIInfo[AI_STEGO].targetGammaRot = 10.0f;
	AIInfo[AI_STEGO].idleStart = 120;
	AIInfo[AI_STEGO].yBetaGamma1 = 128;
	AIInfo[AI_STEGO].yBetaGamma2 = 64;
	AIInfo[AI_STEGO].yBetaGamma3 = 0.6f;
	AIInfo[AI_STEGO].yBetaGamma4 = 0.4f;
	AIInfo[AI_STEGO].tGAIncrement = 3.f;
	AIInfo[AI_STEGO].rot1 = 0.2f;
	AIInfo[AI_STEGO].rot2 = 1.0f;
	//AIInfo[AI_STEGO].weaveRange = 0;
	AIInfo[AI_STEGO].pWMin = 2048;
	//waterLevel = 160;

	AIInfo[AI_CHASM].targetDistance = 8048.f;
	AIInfo[AI_CHASM].agressMulti = 128;
	AIInfo[AI_CHASM].noWayCntMin = 8;
	AIInfo[AI_CHASM].noFindWayMed = 48;
	AIInfo[AI_CHASM].noFindWayRange = 80;
	AIInfo[AI_CHASM].idleStart = 124;
	AIInfo[AI_CHASM].targetBendRotSpd = 3.5f;
	AIInfo[AI_CHASM].targetBendMin = 2.f;
	AIInfo[AI_CHASM].targetBendDelta1 = 1600.f;
	AIInfo[AI_CHASM].targetBendDelta2 = 1200.f;
	AIInfo[AI_CHASM].yBetaGamma1 = 128;
	AIInfo[AI_CHASM].yBetaGamma2 = 64;
	AIInfo[AI_CHASM].yBetaGamma3 = 0.6f;
	AIInfo[AI_CHASM].yBetaGamma4 = 0.3f;
	AIInfo[AI_CHASM].walkTargetGammaRot = 12.0f;
	AIInfo[AI_CHASM].targetGammaRot = 8.0f;
	AIInfo[AI_CHASM].tGAIncrement = 3.f;
	AIInfo[AI_CHASM].rot1 = 0.2f;
	AIInfo[AI_CHASM].rot2 = 1.0f;
	//AIInfo[AI_CHASM].weaveRange = 0;
	AIInfo[AI_CHASM].pWMin = 2048;
	//waterLevel = 120;

	AIInfo[AI_ALLO].agressMulti = 4;
	AIInfo[AI_ALLO].targetBendRotSpd = 2;
	//AIInfo[AI_ALLO].waterLevel = 180;
	AIInfo[AI_ALLO].yBetaGamma1 = 64;
	AIInfo[AI_ALLO].yBetaGamma2 = 32;
	AIInfo[AI_ALLO].yBetaGamma3 = 0.5f;
	AIInfo[AI_ALLO].yBetaGamma4 = 0.4f;
	AIInfo[AI_ALLO].walkTargetGammaRot = 10.0f;
	AIInfo[AI_ALLO].targetGammaRot = 8.0f;
	AIInfo[AI_ALLO].tGAIncrement = 2.f;
	AIInfo[AI_ALLO].idleStartD = 118;
	AIInfo[AI_ALLO].jumper = true;
	AIInfo[AI_ALLO].carnivore = true;
	AIInfo[AI_ALLO].noWayCntMin = 12;
	AIInfo[AI_ALLO].noFindWayMed = 16;
	AIInfo[AI_ALLO].noFindWayRange = 20;
	AIInfo[AI_ALLO].targetDistance = 8048.f;
	//AIInfo[AI_ALLO].weaveRange = 1648;
	AIInfo[AI_ALLO].pWMin = 2048;

	AIInfo[AI_VELO].agressMulti = 8;
	AIInfo[AI_VELO].targetBendRotSpd = 3;
	//AIInfo[AI_VELO].waterLevel = 140;
	AIInfo[AI_VELO].yBetaGamma1 = 48;
	AIInfo[AI_VELO].yBetaGamma2 = 24;
	AIInfo[AI_VELO].yBetaGamma3 = 0.5f;
	AIInfo[AI_VELO].yBetaGamma4 = 0.4f;
	AIInfo[AI_VELO].walkTargetGammaRot = 7.0f;
	AIInfo[AI_VELO].targetGammaRot = 5.0f;
	AIInfo[AI_VELO].tGAIncrement = 2.f;
	AIInfo[AI_VELO].idleStartD = 118;
	AIInfo[AI_VELO].jumper = true;
	AIInfo[AI_VELO].carnivore = true;
	AIInfo[AI_VELO].noWayCntMin = 12;
	AIInfo[AI_VELO].noFindWayMed = 16;
	AIInfo[AI_VELO].noFindWayRange = 20;
	AIInfo[AI_VELO].targetDistance = 8048.f;
	//AIInfo[AI_VELO].weaveRange = 1648;
	AIInfo[AI_VELO].pWMin = 2048;

	AIInfo[AI_SPINO].agressMulti = 8;
	//AIInfo[AI_SPINO].waterLevel = 140;
	AIInfo[AI_SPINO].tGAIncrement = 4.f;
	AIInfo[AI_SPINO].idleStartD = 128;
	AIInfo[AI_SPINO].targetBendRotSpd = 3;
	AIInfo[AI_SPINO].yBetaGamma1 = 98;
	AIInfo[AI_SPINO].yBetaGamma2 = 84;
	AIInfo[AI_SPINO].yBetaGamma3 = 0.4f;
	AIInfo[AI_SPINO].yBetaGamma4 = 0.3f;
	AIInfo[AI_SPINO].walkTargetGammaRot = 9.0f;
	AIInfo[AI_SPINO].targetGammaRot = 6.0f;
	AIInfo[AI_SPINO].jumper = true;
	AIInfo[AI_SPINO].carnivore = true;
	AIInfo[AI_SPINO].noWayCntMin = 12;
	AIInfo[AI_SPINO].noFindWayMed = 16;
	AIInfo[AI_SPINO].noFindWayRange = 20;
	AIInfo[AI_SPINO].targetDistance = 8048.f;
	//AIInfo[AI_SPINO].weaveRange = 1648;
	AIInfo[AI_SPINO].pWMin = 2048;

	AIInfo[AI_CERAT].jumper = false;
	AIInfo[AI_CERAT].agressMulti = 8;
	//AIInfo[AI_CERAT].waterLevel = 140;
	AIInfo[AI_CERAT].tGAIncrement = 4.f;
	AIInfo[AI_CERAT].idleStartD = 128;
	AIInfo[AI_CERAT].targetBendRotSpd = 3;
	AIInfo[AI_CERAT].yBetaGamma1 = 348;
	AIInfo[AI_CERAT].yBetaGamma2 = 324;
	AIInfo[AI_CERAT].yBetaGamma3 = 0.5f;
	AIInfo[AI_CERAT].yBetaGamma4 = 0.4f;
	AIInfo[AI_CERAT].walkTargetGammaRot = 9.0f;
	AIInfo[AI_CERAT].targetGammaRot = 6.0f;
	AIInfo[AI_CERAT].carnivore = true;
	AIInfo[AI_CERAT].noWayCntMin = 12;
	AIInfo[AI_CERAT].noFindWayMed = 16;
	AIInfo[AI_CERAT].noFindWayRange = 20;
	AIInfo[AI_CERAT].targetDistance = 8048.f;
	//AIInfo[AI_CERAT].weaveRange = 1648;
	AIInfo[AI_CERAT].pWMin = 2048;


	AIInfo[AI_TITAN].jumper = false;
	AIInfo[AI_TITAN].agressMulti = 8;
	//AIInfo[AI_TITAN].waterLevel = 140;
	AIInfo[AI_TITAN].tGAIncrement = 4.f;
	AIInfo[AI_TITAN].idleStartD = 128;
	AIInfo[AI_TITAN].targetBendRotSpd = 3;
	AIInfo[AI_TITAN].yBetaGamma1 = 348;
	AIInfo[AI_TITAN].yBetaGamma2 = 324;
	AIInfo[AI_TITAN].yBetaGamma3 = 0.5f;
	AIInfo[AI_TITAN].yBetaGamma4 = 0.4f;
	AIInfo[AI_TITAN].walkTargetGammaRot = 9.0f;
	AIInfo[AI_TITAN].targetGammaRot = 6.0f;
	AIInfo[AI_TITAN].carnivore = true;
	AIInfo[AI_TITAN].noWayCntMin = 12;
	AIInfo[AI_TITAN].noFindWayMed = 16;
	AIInfo[AI_TITAN].noFindWayRange = 20;
	AIInfo[AI_TITAN].targetDistance = 8048.f;
	//AIInfo[AI_TITAN].weaveRange = 6592;
	AIInfo[AI_TITAN].pWMin = 2048;

	AIInfo[AI_MICRO].agressMulti = 8;
	AIInfo[AI_MICRO].targetBendRotSpd = 3;
	//AIInfo[AI_MICRO].waterLevel = 140;
	AIInfo[AI_MICRO].yBetaGamma1 = 48;
	AIInfo[AI_MICRO].yBetaGamma2 = 24;
	AIInfo[AI_MICRO].yBetaGamma3 = 0.5f;
	AIInfo[AI_MICRO].yBetaGamma4 = 0.4f;
	AIInfo[AI_MICRO].walkTargetGammaRot = 7.0f;
	AIInfo[AI_MICRO].targetGammaRot = 5.0f;
	AIInfo[AI_MICRO].tGAIncrement = 2.f;
	AIInfo[AI_MICRO].idleStartD = 118;
	//AIInfo[AI_MICRO].jumper = true;
	AIInfo[AI_MICRO].carnivore = true;
	AIInfo[AI_MICRO].noWayCntMin = 12;
	AIInfo[AI_MICRO].noFindWayMed = 16;
	AIInfo[AI_MICRO].noFindWayRange = 20;
	AIInfo[AI_MICRO].targetDistance = 8048.f;
	//AIInfo[AI_MICRO].weaveRange = 1648;
	AIInfo[AI_MICRO].pWMin = 2048;

	//AIInfo[AI_TREX].waterLevel = 560;



	AIInfo[AI_FISH].jumper = false;
	AIInfo[AI_FISH].agressMulti = 4;
	AIInfo[AI_FISH].idleStart = 126;

	AIInfo[AI_MOSA].jumper = true;
	AIInfo[AI_MOSA].agressMulti = 4;
	AIInfo[AI_MOSA].idleStart = 126; //SET THIS BACK TO 126


	AIInfo[AI_MOSH].idleStart = 76;
	AIInfo[AI_DIMET].idleStart = 76;
	AIInfo[AI_GALL].idleStart = 76;
	AIInfo[AI_PIG].idleStart = 96;




  FILE *stream;
  char line[256];

//  int nextTrophySlot = 0;

  stream = fopen(ResolveLegacyAssetReadPath("HUNTDAT\\_res.txt").c_str(), "r");
  if (!stream) DoHalt("Can't open resources file _res.txt");

  TotalC = 0;
  TotalMA = 0;

  char tempProjectName[128];
  for (int a = 0; a < __argc; a++)
  {
	  LPSTR s = __argv[a];
	  if (strstr(s, "prj="))
	  {
		  CopyProjectName(tempProjectName, (s + 4));
		  //break;
	  }
	  if (strstr(s, "-survival")) g_GameMode = GameMode::SurvivalMode;
  }
  

  int areaNumber = -1;
  switch ((char)tempProjectName[18]) {
  case '1':
	  if (tempProjectName[19]) areaNumber = 9;
	  else areaNumber = 0;
	  break;
  case '2':
	  areaNumber = 1;
	  break;
  case '3':
	  areaNumber = 2;
	  break;
  case '4':
	  areaNumber = 3;
	  break;
  case '5':
	  areaNumber = 4;
	  break;
  case '6':
	  areaNumber = 5;
	  break;
  case '7':
	  areaNumber = 6;
	  break;
  case '8':
	  areaNumber = 7;
	  break;
  case '9':
	  areaNumber = 8;
	  break;
  }

  trophyTypeCount = 0;


  while (fgets( line, 255, stream))
  {
    if (line[0] == '.') break;
	if (strstr(line, "spawntable")) ReadSpawnTable(stream);
	if (strstr(line, "packtable")) ReadPackTable(stream);
	if (strstr(line, "trophytable")) ReadTrophyTable(stream);
	if (strstr(line, "areatable")) ReadAreaTable(stream, areaNumber);
    if (strstr(line, "weapons") ) ReadWeapons(stream);
	//if (strstr(line, "hunterinfo")) ReadCharacters(stream, false, nextTrophySlot);
	//if (strstr(line, "oldambients")) ReadCharacters(stream, false, nextTrophySlot);
	//if (strstr(line, "corpseambients")) ReadCharacters(stream, false, nextTrophySlot);
    if (strstr(line, "characters") ) ReadCharacters(stream);
	//if (strstr(line, "mapambients")) ReadCharacters(stream, true, nextTrophySlot);

  }

  //default region
  if (g_GameMode != GameMode::SurvivalMode)
  for (int sg = 0; sg < TotalSpawnGroup; sg++) {    
	  if (!spawnGroup[sg].spawnRegionCh) {
		  spawnGroup[sg].spawnRegion[0].XMax = 988;
		  spawnGroup[sg].spawnRegion[0].YMax = 988;
		  spawnGroup[sg].spawnRegion[0].XMin = 12;
		  spawnGroup[sg].spawnRegion[0].YMin = 12;
		  spawnGroup[sg].spawnRegionCh = 1;
	  }
  }

  fclose (stream);
}