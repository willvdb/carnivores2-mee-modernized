// ==========================================================================
// ScriptParser.cpp � _RES.TXT / _MENU.TXT script file parser
// ==========================================================================

#include "Hunt.h"
#include "Platform/System.h"
#include "Session/Session.h"
#include "Core/CommandLineParse.h"
#include "LoadDiagnostics.h"
#include "LoadValidate.h"
#include "ScriptBlockParse.h"
#include "ScriptValueParse.h"

// All diagnostics recorded while a script is parsed are reported through one
// policy (LoadDiagnostics.h): lenient recovers and logs, strict halts. Keep
// the group name stable so CI/tests can filter it.
static const char* kScriptLoadGroup = "ScriptParser";

static void ReportScriptRecovery(const char* field, const char* reason,
                                 const char* line)
{
  LoadDiagnostics::Instance().Report(kScriptLoadGroup, field, reason, line);
}

// Write the recovered-value summary to the hunt log once per full script
// parse and clear it, so repeated parses do not repeat old entries. Strict
// mode never reaches this point with entries: it halts on the first one.
static void FlushScriptDiagnostics()
{
  LoadDiagnostics& diagnostics = LoadDiagnostics::Instance();
  if (diagnostics.Count() == 0)
    return;

  std::string summary = diagnostics.Summary();
  PrintLog(const_cast<char*>(summary.c_str()));
  diagnostics.Clear();
}

// _RES.TXT string safety. Name/file fields are fixed char arrays
// (WeapInfo/DinoInfo fixed arrays, GameTypes.h); an overlong modded
// value previously overflowed via strcpy, and value[strlen(value)-2] indexed
// before the buffer when the quoted value was shorter than ''. In lenient
// mode the value is truncated to the field with a diagnostic; strict mode
// halts. The offending line goes into the message as well: the field name
// alone left modders hunting through the whole script for the bad entry.
static void ScriptFieldFail(const char* what, const char* line,
                            const char* reason = nullptr)
{
  char bad[256];
  bad[0] = 0;
  if (line && !CopyCapped(bad, sizeof(bad), line))
    bad[0] = 0;
  size_t len = strlen(bad);
  while (len > 0 && (bad[len - 1] == '\n' || bad[len - 1] == '\r'))
    bad[--len] = 0;

  const bool hasReason = reason && reason[0] != 0;
  char sz[512];
  if (bad[0] && hasReason)
    snprintf(sz, sizeof(sz),
              "Script loading error: %s missing, too long, or malformed (%s).\n"
              "Line: %s",
              what, reason, bad);
  else if (bad[0])
    snprintf(sz, sizeof(sz),
              "Script loading error: %s missing, too long, or malformed.\n"
              "Line: %s",
              what, bad);
  else if (hasReason)
    snprintf(sz, sizeof(sz),
              "Script loading error: %s missing, too long, or malformed (%s).",
              what, reason);
  else
    snprintf(sz, sizeof(sz),
              "Script loading error: %s missing, too long, or malformed.", what);
  DoHalt(sz);
}

// Read the quoted value out of `value` (the text after '='). The line is
// never modified, so a second field on the same line still parses, and the
// key is matched by name (ScriptKeyIs) instead of by searching the line for
// a substring. Recovery policy: a missing/unclosed quote leaves the field
// empty (and is fatal in strict mode); an overlong value is truncated in
// lenient mode instead of halting the whole hunt.
static void CopyScriptField(char* dst, size_t dstCap, const char* value,
                            const char* what, const char* line)
{
  const char* quoted = nullptr;
  size_t length = 0;
  if (!dst || dstCap == 0)
    ScriptFieldFail(what, line, "invalid destination field");
  if (!FindQuotedValue(value, &quoted, &length))
  {
    ReportScriptRecovery(what, "missing or unclosed quoted value", line);
    if (LoadDiagnostics::Instance().Strict())
      ScriptFieldFail(what, line, "missing or unclosed quoted value");
    dst[0] = 0;
    return;
  }
  if (length >= dstCap)
  {
    char reason[96];
    snprintf(reason, sizeof(reason),
              "value longer than the %u-byte field; truncated",
              static_cast<unsigned>(dstCap));
    ReportScriptRecovery(what, reason, line);
    if (LoadDiagnostics::Instance().Strict())
      ScriptFieldFail(what, line, reason);
    length = dstCap - 1;
  }
  memcpy(dst, quoted, length);
  dst[length] = 0;
}

static void CopyProjectName(char* dst, const char* src)
{
  if (!CopyCapped(dst, 128, src)) {
    // ProcessCommandLine() rejects the same option without changing the last
    // valid value. Keep this second argv pass consistent so a malformed later
    // token cannot turn an otherwise valid launch into an abnormal halt.
    PrintLog("Script loading: ignoring overlong project path.\n");
    return;
  }
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

struct ScriptCommonOptions
{
  bool hasSurvivalArea = false;
  bool hasSurvivalWeapon = false;
  bool hasSurvivalDayNight = false;
  int survivalArea = 0;
  int survivalWeapon = 0;
  int survivalDayNight = 0;
};

static int ReadScriptIntField(const char* value, const char* line,
                              const char* field);
static int RecoverScriptRange(int value, int minimum, int maximum,
                              const char* what, const char* line);

static bool HasCommandLineOption(const char* option)
{
  for (std::size_t a = EngineSession::Active() ? 1 : 0; a < Platform::Arguments().size(); ++a)
  {
    if (LegacyText::Compare(Platform::Arguments()[a].c_str(), option) == 0)
      return true;
  }
  return false;
}

static bool HasCommandLineValue(const char* option)
{
  for (std::size_t a = EngineSession::Active() ? 1 : 0; a < Platform::Arguments().size(); ++a)
  {
    const char* value = nullptr;
    if (CommandLineOptionValue(Platform::Arguments()[a].c_str(), option, &value))
      return true;
  }
  return false;
}

static bool IsScriptBlockLine(const char* line, const char* name)
{
  while (*line == ' ' || *line == '\t')
    ++line;

  const size_t nameLength = strlen(name);
  if (strncmp(line, name, nameLength) != 0)
    return false;

  line += nameLength;
  while (*line == ' ' || *line == '\t')
    ++line;
  return *line == '{';
}

static void ReadCommonOptions(FILE* stream, ScriptCommonOptions& options)
{
  char line[256];
  while (fgets(line, 255, stream))
  {
    if (strstr(line, "}"))
      return;

    char* value = strstr(line, "=");
    if (!value)
      DoHalt("Script loading error: common");
    ++value;

    if (ScriptKeyIs(line, "survivalArea"))
    {
      options.survivalArea = RecoverScriptRange(
          ReadScriptIntField(value, line, "survival area"), 1, 10,
          "survival area", line);
      options.hasSurvivalArea = true;
    }
    else if (ScriptKeyIs(line, "survivalWeapon"))
    {
      options.survivalWeapon = RecoverScriptRange(
          ReadScriptIntField(value, line, "survival weapon"), 1, 10,
          "survival weapon", line);
      options.hasSurvivalWeapon = true;
    }
    else if (ScriptKeyIs(line, "survivalDTM"))
    {
      options.survivalDayNight = RecoverScriptRange(
          ReadScriptIntField(value, line, "survival day/night"), 0, 2,
          "survival day/night", line);
      options.hasSurvivalDayNight = true;
    }
  }

  DoHalt("Script loading error: unterminated common block.");
}

static void ReadCommonOptionsFromScript(FILE* stream, ScriptCommonOptions& options)
{
  char line[256];
  while (fgets(line, 255, stream))
  {
    if (IsScriptBlockLine(line, "common"))
    {
      ReadCommonOptions(stream, options);
      return;
    }
  }
}

static void ApplySurvivalCommonOptions(const ScriptCommonOptions& options)
{
  if (g_GameMode != GameMode::SurvivalMode)
    return;

  if (options.hasSurvivalArea && !HasCommandLineValue("prj="))
  {
    char projectName[128];
    snprintf(projectName, sizeof(projectName), "huntdat/areas/area%d",
              options.survivalArea);
    CopyProjectName(ProjectName, projectName);
  }

  if (options.hasSurvivalWeapon && !HasCommandLineValue("wep="))
  {
    // _RES.TXT numbers the survival weapon from one, while WeaponPres is a
    // zero-based bitmask. Stock data uses 8 for its eighth weapon.
    WeaponPres = 1 << (options.survivalWeapon - 1);
  }

  if (options.hasSurvivalDayNight && !HasCommandLineValue("dtm="))
    OptDayNight = options.survivalDayNight;
}

static void ReadScriptCommandLineOptions(char projectName[128], int& timeOfDay,
                                         int& dinSelect)
{
  memset(projectName, 0, 128);
  // ProcessCommandLine() has already established the current global value.
  // Start the second argv pass from that value so an overlong later token
  // cannot erase an otherwise valid project selection.
  CopyCapped(projectName, 128, ProjectName);
  timeOfDay = OptDayNight;
  dinSelect = 0;

  for (std::size_t a = EngineSession::Active() ? 1 : 0; a < Platform::Arguments().size(); ++a)
  {
    const char* value = nullptr;
    const char* argument = Platform::Arguments()[a].c_str();

    if (CommandLineOptionValue(argument, "prj=", &value))
    {
      CopyProjectName(projectName, value);
      continue;
    }

    if (CommandLineOptionValue(argument, "dtm=", &value))
    {
      int parsed = 0;
      if (ParseCommandLineInt(value, parsed) && parsed >= 0 && parsed <= 2)
        timeOfDay = parsed;
      continue;
    }

    if (CommandLineOptionValue(argument, "din=", &value))
    {
      int parsed = 0;
      if (ParseCommandLineInt(value, parsed) && parsed >= 0 && parsed <= 1023)
        dinSelect = parsed * 1024;
    }
  }
}

// One recovery path for every scalar field. Ok returns immediately; any
// other status records a diagnostic and, in lenient mode, returns the
// memory-safe recovered value (fallback or clamp). Strict mode halts with
// the same reason the log would have carried.
static int RecoverScriptIntResult(const ScriptIntResult& parsed,
                                  const char* field, const char* line)
{
  if (parsed.status == ScriptScalarStatus::Ok)
    return parsed.value;

  const char* reason = ScriptScalarStatusReason(parsed.status);
  ReportScriptRecovery(field, reason, line);
  if (LoadDiagnostics::Instance().Strict())
    ScriptFieldFail(field, line, reason);
  return parsed.value;
}

static float RecoverScriptFloatResult(const ScriptFloatResult& parsed,
                                      const char* field, const char* line)
{
  if (parsed.status == ScriptScalarStatus::Ok)
    return parsed.value;

  const char* reason = ScriptScalarStatusReason(parsed.status);
  ReportScriptRecovery(field, reason, line);
  if (LoadDiagnostics::Instance().Strict())
    ScriptFieldFail(field, line, reason);
  return parsed.value;
}

static int ReadScriptIntField(const char* value, const char* line,
                              const char* field)
{
  return RecoverScriptIntResult(ParseScriptIntStatus(value, 0), field, line);
}

static int ReadScriptLegacyIntField(const char* value, const char* line,
                                    const char* field)
{
  return RecoverScriptIntResult(ParseScriptLegacyIntStatus(value, 0), field,
                                line);
}

static float ReadScriptFloatField(const char* value, const char* line,
                                  const char* field)
{
  return RecoverScriptFloatResult(ParseScriptFloatStatus(value, 0.0f), field,
                                  line);
}

static int ReadScriptInt(const char* value)
{
  return ReadScriptIntField(value, value, "script integer");
}

static void RequireScriptSlot(int index, int capacity, const char* what)
{
  if (!IsValidIndex(index, capacity))
  {
    char sz[192];
    snprintf(sz, sizeof(sz),
              "Script loading error: %s capacity exceeded (index=%d, max=%d).",
              what, index, capacity - 1);
    DoHalt(sz);
  }
}

static int RecoverScriptIndex(int index, int capacity, const char* what,
                              const char* line);

static int ReadScriptIndexField(const char* value, const char* line,
                                const char* field, int capacity)
{
  const int parsed = ReadScriptIntField(value, line, field);
  return RecoverScriptIndex(parsed, capacity, field, line);
}

// Value-derived indices recover by clamping into the fixed array in lenient
// mode (0 or capacity-1), so a bad mod value can neither abort the hunt nor
// touch memory out of bounds. Count guards (RequireScriptSlot at a capacity)
// stay fatal in both modes: there is no value to clamp, the section itself
// does not fit.
static int RecoverScriptIndex(int index, int capacity, const char* what,
                              const char* line)
{
  if (IsValidIndex(index, capacity))
    return index;

  char reason[96];
  snprintf(reason, sizeof(reason), "index out of range [0..%d]", capacity - 1);
  ReportScriptRecovery(what, reason, line);
  if (LoadDiagnostics::Instance().Strict())
  {
    char bad[192];
    bad[0] = 0;
    if (line && !CopyCapped(bad, sizeof(bad), line))
      bad[0] = 0;
    size_t len = strlen(bad);
    while (len > 0 && (bad[len - 1] == '\n' || bad[len - 1] == '\r'))
      bad[--len] = 0;

    char sz[320];
    if (bad[0])
      snprintf(sz, sizeof(sz),
                "Script loading error: %s index out of range (index=%d, max=%d).\n"
                "Line: %s",
                what, index, capacity - 1, bad);
    else
      snprintf(sz, sizeof(sz),
                "Script loading error: %s index out of range (index=%d, max=%d).",
                what, index, capacity - 1);
    DoHalt(sz);
  }
  return index < 0 ? 0 : capacity - 1;
}

// Clamp a value into an inclusive range. Used by the survival defaults and
// other settings whose out-of-range handling used to halt a hunt (v1.1.9).
static int RecoverScriptRange(int value, int minimum, int maximum,
                              const char* what, const char* line)
{
  if (value >= minimum && value <= maximum)
    return value;

  char reason[96];
  snprintf(reason, sizeof(reason), "expected %d..%d, got %d", minimum,
            maximum, value);
  ReportScriptRecovery(what, reason, line);
  if (LoadDiagnostics::Instance().Strict())
    ScriptFieldFail(what, line, reason);
  return value < minimum ? minimum : maximum;
}

// A configured minimum/maximum pair must fit its destination capacity and
// retain the ordering expected by the spawning loops. Lenient mode clamps
// both ends into [0, capacity] and orders the pair; strict mode halts.
static void RecoverOrderedScriptRange(int& minimum, int& maximum, int capacity,
                                      const char* what, const char* line)
{
  if (IsValidOrderedRange(minimum, maximum, capacity))
    return;

  char reason[128];
  snprintf(reason, sizeof(reason),
            "invalid range (min=%d, max=%d, capacity=%d)", minimum, maximum,
            capacity);
  ReportScriptRecovery(what, reason, line);
  if (LoadDiagnostics::Instance().Strict())
    ScriptFieldFail(what, line, reason);

  if (minimum < 0) minimum = 0;
  if (minimum > capacity) minimum = capacity;
  if (maximum < 0) maximum = 0;
  if (maximum > capacity) maximum = capacity;
  if (minimum > maximum)
  {
    const int swap = minimum;
    minimum = maximum;
    maximum = swap;
  }
}

static int ScriptIndex(const char* value, int capacity, const char* what)
{
  const int index = value ? ReadScriptInt(value) : -1;
  return RecoverScriptIndex(index, capacity, what, value);
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
  {
    ReportScriptRecovery("idle particle animation",
                         "no preceding look animation", nullptr);
    if (LoadDiagnostics::Instance().Strict())
      DoHalt("Script loading error: idle particle data has no preceding look animation.");
    // Attach the particle data to animation slot 0 instead of aborting the
    // whole hunt; the diagnostic names the missing look animation.
    return 0;
  }
  const int index = DinoInfo[TotalC].lookAnim[DinoInfo[TotalC].lookCount - 1];
  RequireScriptSlot(index, 50, "idle particle animation");
  return index;
}

void readBool(char *value, std::int32_t &out) {
	if (strstr(value, "true") || strstr(value, "TRUE")) out = true;
	if (strstr(value, "false") || strstr(value, "FALSE")) out = false;
}

void readBool(char *value, bool &out) {
	if (strstr(value, "true") || strstr(value, "TRUE")) out = true;
	if (strstr(value, "false") || strstr(value, "FALSE")) out = false;
}

void SkipSector(FILE *stream)
{
  if (!ConsumeScriptBlockBody(stream))
    DoHalt("Script loading error: unterminated block.");
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


		if (strstr(line, "tropPos")) trophyType[trophyTypeCount].trophyPos = ReadScriptIntField(value, line, "trophy tropPos");
		if (strstr(line, "alpha")) trophyType[trophyTypeCount].alpha = ReadScriptIntField(value, line, "trophy alpha");
		if (strstr(line, "beta")) trophyType[trophyTypeCount].beta = ReadScriptIntField(value, line, "trophy beta");
		if (strstr(line, "gamma")) trophyType[trophyTypeCount].gamma = ReadScriptIntField(value, line, "trophy gamma");
		if (strstr(line, "xoffset")) trophyType[trophyTypeCount].xoffset = ReadScriptIntField(value, line, "trophy xoffset");
		if (strstr(line, "yoffset")) trophyType[trophyTypeCount].yoffset = ReadScriptIntField(value, line, "trophy yoffset");
		if (strstr(line, "zoffset")) trophyType[trophyTypeCount].zoffset = ReadScriptIntField(value, line, "trophy zoffset");
		if (strstr(line, "xscale")) trophyType[trophyTypeCount].xoffsetScale = ReadScriptIntField(value, line, "trophy xscale");
		if (strstr(line, "yscale")) trophyType[trophyTypeCount].yoffsetScale = ReadScriptIntField(value, line, "trophy yscale");
		if (strstr(line, "zscale")) trophyType[trophyTypeCount].zoffsetScale = ReadScriptIntField(value, line, "trophy zscale");
		if (strstr(line, "tropAnim")) trophyType[trophyTypeCount].anim = ReadScriptIntField(value, line, "trophy tropAnim");
		if (strstr(line, "xdata")) trophyType[trophyTypeCount].xdata = ReadScriptIntField(value, line, "trophy xdata");
		if (strstr(line, "ydata")) trophyType[trophyTypeCount].ydata = ReadScriptIntField(value, line, "trophy ydata");
		if (strstr(line, "zdata")) trophyType[trophyTypeCount].zdata = ReadScriptIntField(value, line, "trophy zdata");
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
			snprintf(errorBuff, sizeof(errorBuff), "Script loading error: SpawnInfo: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "spawnratio")) DinoInfo[TotalC].SpawnInfo[DinoInfo[TotalC].SpawnInfoCh].spawnRatio = ReadScriptFloatField(value, line, "spawn ratio");
		if (strstr(line, "spawngroup")) DinoInfo[TotalC].SpawnInfo[DinoInfo[TotalC].SpawnInfoCh].spawnGroup = ReadScriptIndexField(value, line, "spawn group", 256);
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

		if (strstr(line, "spawnratio")) packType[packTypeCount].SpawnInfo[packType[packTypeCount].SpawnInfoCh].spawnRatio = ReadScriptFloatField(value, line, "pack spawn ratio");
		if (strstr(line, "spawngroup")) packType[packTypeCount].SpawnInfo[packType[packTypeCount].SpawnInfoCh].spawnGroup = ReadScriptIndexField(value, line, "pack spawn group", 256);
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
			snprintf(errorBuff, sizeof(errorBuff), "Script loading error: PackInfo: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "group")) DinoInfo[TotalC].packMember2[DinoInfo[TotalC].packMember2Ch].packGroup = ReadScriptIndexField(value, line, "pack member group", 1024);
		if (strstr(line, "ratio")) DinoInfo[TotalC].packMember2[DinoInfo[TotalC].packMember2Ch].ratio = ReadScriptFloatField(value, line, "pack member ratio");

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

		if (strstr(line, "xmax")) spawnGroup[TotalSpawnGroup].avoidRegion[spawnGroup[TotalSpawnGroup].avoidRegionCh].XMax = ReadScriptIntField(value, line, "avoid xmax");
		if (strstr(line, "xmin")) spawnGroup[TotalSpawnGroup].avoidRegion[spawnGroup[TotalSpawnGroup].avoidRegionCh].XMin = ReadScriptIntField(value, line, "avoid xmin");
		if (strstr(line, "ymax")) spawnGroup[TotalSpawnGroup].avoidRegion[spawnGroup[TotalSpawnGroup].avoidRegionCh].YMax = ReadScriptIntField(value, line, "avoid ymax");
		if (strstr(line, "ymin")) spawnGroup[TotalSpawnGroup].avoidRegion[spawnGroup[TotalSpawnGroup].avoidRegionCh].YMin = ReadScriptIntField(value, line, "avoid ymin");

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

		if (strstr(line, "xmax")) spawnGroup[TotalSpawnGroup].spawnRegion[spawnGroup[TotalSpawnGroup].spawnRegionCh].XMax = ReadScriptIntField(value, line, "region xmax");
		if (strstr(line, "xmin")) spawnGroup[TotalSpawnGroup].spawnRegion[spawnGroup[TotalSpawnGroup].spawnRegionCh].XMin = ReadScriptIntField(value, line, "region xmin");
		if (strstr(line, "ymax")) spawnGroup[TotalSpawnGroup].spawnRegion[spawnGroup[TotalSpawnGroup].spawnRegionCh].YMax = ReadScriptIntField(value, line, "region ymax");
		if (strstr(line, "ymin")) spawnGroup[TotalSpawnGroup].spawnRegion[spawnGroup[TotalSpawnGroup].spawnRegionCh].YMin = ReadScriptIntField(value, line, "region ymin");

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



	if (strstr(line, "spawnrate")) spawnGroup[TotalSpawnGroup].SpawnRate = ReadScriptFloatField(value, line, "spawn rate");
	if (strstr(line, "spawnmax")) spawnGroup[TotalSpawnGroup].SpawnMax = ReadScriptIntField(value, line, "spawn max");
	if (strstr(line, "spawnmin")) spawnGroup[TotalSpawnGroup].SpawnMin = ReadScriptIntField(value, line, "spawn min");

	if (strstr(line, "densityMulti")) spawnGroup[TotalSpawnGroup].densityMulti = ReadScriptIntField(value, line, "spawn density multiplier");
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
  ReadScriptCommandLineOptions(tempProjectName, timeOfDay, dinSelect);

	//time
	for (const auto& argument : Platform::Arguments())
	{
		const char* s = argument.c_str();
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
			RecoverOrderedScriptRange(spawnGroup[TotalSpawnGroup].SpawnMin,
			                          spawnGroup[TotalSpawnGroup].SpawnMax,
			                          256, "spawn group limits", line);
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

	// Older _RES.TXT files may put region blocks directly in a pack override
	// instead of wrapping them in spawngroup. They are not represented in the
	// pack table, but their contents still need to be consumed as one sector.
	if (IsScriptBlockLine(line, "region")) {
		SkipSector(stream);
		return;
	}

	if (strstr(line, "packMax")) packType[packTypeCount].packMax = ReadScriptIntField(value, line, "pack max");
	if (strstr(line, "packMin")) packType[packTypeCount].packMin = ReadScriptIntField(value, line, "pack min");
	if (strstr(line, "packDensity")) packType[packTypeCount].packDensity = ReadScriptFloatField(value, line, "pack density");
	
}

//mode 0 = packtable
//mode 1 = character
void ReadPackGroup(FILE *stream, char line[256], int mode) {
	RequireScriptSlot(packTypeCount, 1024, "pack types");

	//area
  char tempProjectName[128];
  int timeOfDay, dinSelect;
  ReadScriptCommandLineOptions(tempProjectName, timeOfDay, dinSelect);

	//time
	for (const auto& argument : Platform::Arguments())
	{
		const char* s = argument.c_str();
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
			RecoverOrderedScriptRange(packType[packTypeCount].packMin,
			                          packType[packTypeCount].packMax,
			                          256, "pack group limits", line);
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

		if (strstr(line, "vSpd"))  SnowInfo[SnowCh].snow_vSpd = ReadScriptIntField(value, line, "snow vertical speed");
		if (strstr(line, "hSpd"))  SnowInfo[SnowCh].snow_hSpd = ReadScriptIntField(value, line, "snow horizontal speed");
		if (strstr(line, "dens")) {
			SnowInfo[SnowCh].snow_dens = RecoverScriptRange(
			    ReadScriptIntField(value, line, "snow density"), 0, (1 << 20),
			    "snow density", line);
		}

		if (strstr(line, "red"))  SnowInfo[SnowCh].snow_r = ReadScriptIntField(value, line, "snow red");
		if (strstr(line, "gre"))  SnowInfo[SnowCh].snow_g = ReadScriptIntField(value, line, "snow green");
		if (strstr(line, "blu"))  SnowInfo[SnowCh].snow_b = ReadScriptIntField(value, line, "snow blue");
		if (strstr(line, "alp"))  SnowInfo[SnowCh].snow_a = ReadScriptIntField(value, line, "snow alpha");
		if (strstr(line, "rad"))  SnowInfo[SnowCh].snow_rad = ReadScriptFloatField(value, line, "snow radius");

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

					if (strstr(line, "survivalPlayerX")) SurvivalSpawnX = ReadScriptIntField(value, line, "survival player X");
					if (strstr(line, "survivalPlayerY")) SurvivalSpawnZ = ReadScriptIntField(value, line, "survival player Y");
					if (strstr(line, "survivalPlayerA")) SurvivalSpawnA = ReadScriptFloatField(value, line, "survival player angle");
					if (strstr(line, "survivalDinoXMax")) SurvivalDinoSpawn.XMax = ReadScriptIntField(value, line, "survival dino X max");
					if (strstr(line, "survivalDinoYMax")) SurvivalDinoSpawn.YMax = ReadScriptIntField(value, line, "survival dino Y max");
					if (strstr(line, "survivalDinoXMin")) SurvivalDinoSpawn.XMin = ReadScriptIntField(value, line, "survival dino X min");
					if (strstr(line, "survivalDinoYMin")) SurvivalDinoSpawn.YMin = ReadScriptIntField(value, line, "survival dino Y min");

				} else {
					if (strstr(line, "snow")) SkipSector(stream);
				}

			}

	}

}


void ReadWeaponLine(FILE *stream, char *_value, char line[256]) {
	RequireScriptSlot(TotalW, 10, "weapons");
	char *value = _value;

	// Text fields are handled before the numeric/flag dispatch: their values
	// are free-form, and a name or path can contain a numeric field key as a
	// substring (a lowercase 'crossbow', a path like models/separated/...).
	// Once such a line is recognized here it cannot reach a numeric reader.
	if (ScriptKeyIs(line, "name"))
	{
		CopyScriptField(WeapInfo[TotalW].Name, sizeof(WeapInfo[TotalW].Name), value, "Weapons name", line);
		return;
	}

	if (ScriptKeyIs(line, "file"))
	{
		CopyScriptField(WeapInfo[TotalW].FName, sizeof(WeapInfo[TotalW].FName), value, "Weapons file", line);
		return;
	}

	if (ScriptKeyIs(line, "gunshot"))
	{
		CopyScriptField(WeapInfo[TotalW].SFXName, sizeof(WeapInfo[TotalW].SFXName), value, "Weapons gunshot", line);
		WeapInfo[TotalW].MGSSound = true;
		return;
	}

	if (ScriptKeyIs(line, "pic1"))
	{
		CopyScriptField(WeapInfo[TotalW].BFName, sizeof(WeapInfo[TotalW].BFName), value, "Weapons pic", line);
		return;
	}

	if (ScriptKeyIs(line, "picc"))
	{
		CopyScriptField(WeapInfo[TotalW].CFName, sizeof(WeapInfo[TotalW].CFName), value, "Chamber pic", line);
		WeapInfo[TotalW].picch = true;
		return;
	}

	if (ScriptKeyIs(line, "bModel"))
	{
		CopyScriptField(WeapInfo[TotalW].BLName, sizeof(WeapInfo[TotalW].BLName), value, "Weapons bullet", line);
		WeapInfo[TotalW].bullet = true;
		return;
	}

	if (strstr(line, "getAnim"))  WeapInfo[TotalW].getAnim = ReadScriptIntField(value, line, "weapon getAnim");
	if (strstr(line, "putAnim"))  WeapInfo[TotalW].putAnim = ReadScriptIntField(value, line, "weapon putAnim");
	if (strstr(line, "shtAnim"))  WeapInfo[TotalW].shtAnim = ReadScriptIntField(value, line, "weapon shtAnim");
	if (strstr(line, "rldAnimFull"))  WeapInfo[TotalW].rldAnim = ReadScriptIntField(value, line, "weapon rldAnimFull");
	if (strstr(line, "rldAnimPart"))  WeapInfo[TotalW].rldAnimPart = ReadScriptIntField(value, line, "weapon rldAnimPart");
	if (strstr(line, "rckAnim"))  WeapInfo[TotalW].pmpAnim = ReadScriptIntField(value, line, "weapon rckAnim");
	if (strstr(line, "modAnim"))  WeapInfo[TotalW].modAnim = ReadScriptIntField(value, line, "weapon modAnim");

	if (strstr(line, "emptyAnim")) WeapInfo[TotalW].emptyAnim = ReadScriptIntField(value, line, "weapon emptyAnim");
	if (strstr(line, "getEmpAnim")) WeapInfo[TotalW].getEmpAnim = ReadScriptIntField(value, line, "weapon getEmpAnim");
	if (strstr(line, "putEmpAnim")) WeapInfo[TotalW].putEmpAnim = ReadScriptIntField(value, line, "weapon putEmpAnim");

	if (strstr(line, "getAqSnd"))  WeapInfo[TotalW].getAqSnd = ReadScriptIntField(value, line, "weapon getAqSnd");
	if (strstr(line, "putAqSnd"))  WeapInfo[TotalW].putAqSnd = ReadScriptIntField(value, line, "weapon putAqSnd");
	if (strstr(line, "shtAqSnd"))  WeapInfo[TotalW].shtAqSnd = ReadScriptIntField(value, line, "weapon shtAqSnd");
	if (strstr(line, "rldAqSndFull"))  WeapInfo[TotalW].rldAqSnd = ReadScriptIntField(value, line, "weapon rldAqSndFull");
	if (strstr(line, "rldAqSndPart"))  WeapInfo[TotalW].rldAqSndPart = ReadScriptIntField(value, line, "weapon rldAqSndPart");
	if (strstr(line, "rckAqSnd"))  WeapInfo[TotalW].pmpAqSnd = ReadScriptIntField(value, line, "weapon rckAqSnd");
	if (strstr(line, "modAqSnd"))  WeapInfo[TotalW].modAqSnd = ReadScriptIntField(value, line, "weapon modAqSnd");

	if (strstr(line, "mustRack")) readBool(value, WeapInfo[TotalW].mustPump);
	if (strstr(line, "autoRack")) readBool(value, WeapInfo[TotalW].autoPump);
	if (strstr(line, "autoReload")) readBool(value, WeapInfo[TotalW].autoReload);

	if (strstr(line, "canRun")) readBool(value, WeapInfo[TotalW].canRun);
	if (strstr(line, "cannotMortal")) readBool(value, WeapInfo[TotalW].cannotMortal);


	if (strstr(line, "semiAuto")) readBool(value, WeapInfo[TotalW].semiauto);
	if (strstr(line, "fullAuto")) readBool(value, WeapInfo[TotalW].fullauto);

	if (strstr(line, "land_power"))  WeapInfo[TotalW].Power = ReadScriptFloatField(value, line, "weapon land_power");
	if (strstr(line, "land_veloc"))  WeapInfo[TotalW].Veloc = ReadScriptFloatField(value, line, "weapon land_veloc");
	if (strstr(line, "land_prec"))   WeapInfo[TotalW].Prec = ReadScriptFloatField(value, line, "weapon land_prec");
	if (strstr(line, "land_fall"))   WeapInfo[TotalW].Fall = ReadScriptFloatField(value, line, "weapon land_fall");

	if (strstr(line, "aqua_power"))  WeapInfo[TotalW].PowerAq = ReadScriptFloatField(value, line, "weapon aqua_power");
	if (strstr(line, "aqua_veloc"))  WeapInfo[TotalW].VelocAq = ReadScriptFloatField(value, line, "weapon aqua_veloc");
	if (strstr(line, "aqua_prec"))   WeapInfo[TotalW].PrecAq = ReadScriptFloatField(value, line, "weapon aqua_prec");
	if (strstr(line, "aqua_fall"))   WeapInfo[TotalW].FallAq = ReadScriptFloatField(value, line, "weapon aqua_fall");

	if (strstr(line, "loud"))   WeapInfo[TotalW].Loud = ReadScriptFloatField(value, line, "weapon loud");
	if (strstr(line, "rate"))   WeapInfo[TotalW].Rate = ReadScriptFloatField(value, line, "weapon rate");
	if (strstr(line, "shots"))  WeapInfo[TotalW].Shots = ReadScriptIntField(value, line, "weapon shots");
	if (strstr(line, "reload")) WeapInfo[TotalW].Reload = ReadScriptIntField(value, line, "weapon reload");
	if (strstr(line, "trace"))  WeapInfo[TotalW].TraceC = ReadScriptIntField(value, line, "weapon trace") - 1;
	if (strstr(line, "optic"))  WeapInfo[TotalW].Optic = ReadScriptFloatField(value, line, "weapon optic");
	//if (strstr(line, "price")) WeapInfo[TotalW].Price =        atoi(value);

	if (strstr(line, "unzoom")) readBool(value, WeapInfo[TotalW].unzoom);
	if (strstr(line, "breathaim")) readBool(value, WeapInfo[TotalW].breathaim);

	if (strstr(line, "harpoon")) readBool(value, WeapInfo[TotalW].harpoon);

	if (strstr(line, "cross")) readBool(value, WeapInfo[TotalW].cross);
	if (strstr(line, "croR")) WeapInfo[TotalW].crossRed = ReadScriptIntField(value, line, "weapon crosshair red");
	if (strstr(line, "croG")) WeapInfo[TotalW].crossGreen = ReadScriptIntField(value, line, "weapon crosshair green");
	if (strstr(line, "croB")) WeapInfo[TotalW].crossBlue = ReadScriptIntField(value, line, "weapon crosshair blue");

	if (strstr(line, "shake"))   WeapInfo[TotalW].shake = ReadScriptFloatField(value, line, "weapon shake");

	if (strstr(line, "radar")) readBool(value, WeapInfo[TotalW].onRadar);
	if (strstr(line, "radR")) WeapInfo[TotalW].radarRed = ReadScriptIntField(value, line, "weapon radar red");
	if (strstr(line, "radG")) WeapInfo[TotalW].radarGreen = ReadScriptIntField(value, line, "weapon radar green");
	if (strstr(line, "radB")) WeapInfo[TotalW].radarBlue = ReadScriptIntField(value, line, "weapon radar blue");
	if (strstr(line, "radTime"))  WeapInfo[TotalW].radarTime = ReadScriptIntField(value, line, "weapon radar time");

	if (strstr(line, "muzzflash")) readBool(value, WeapInfo[TotalW].MuzzFlash);
	if (strstr(line, "chamflash")) readBool(value, WeapInfo[TotalW].ChamFlash);

	if (strstr(line, "recoil"))  WeapInfo[TotalW].recoil = ReadScriptFloatField(value, line, "weapon recoil");

	if (strstr(line, "retrieve")) readBool(value, WeapInfo[TotalW].retrieve);

}


void ReadWeapons(FILE *stream)
{

	//area
  char tempProjectName[128];
  int timeOfDay, dinSelect;
  ReadScriptCommandLineOptions(tempProjectName, timeOfDay, dinSelect);

	//time
	for (const auto& argument : Platform::Arguments())
	{
		const char* s = argument.c_str();
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
			snprintf(errorBuff, sizeof(errorBuff), "Script loading error: Weapons: %s", WeapInfo[TotalW].Name);
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
						snprintf(errorBuff, sizeof(errorBuff), "Script loading error: Weapons: %s", WeapInfo[TotalW].Name);
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

/*
// Disabled: no live definition. The only call site is the block below,
// which is commented out too.
void WipeAvoidances() {

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
			snprintf(errorBuff, sizeof(errorBuff), "Script loading error: IdleGroup: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "startChance")) DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].start = ReadScriptFloatField(value, line, "idle start chance");
		if (strstr(line, "endChance")) DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].end = ReadScriptFloatField(value, line, "idle end chance");
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
			DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].anim[DinoInfo[TotalC].idleGroup[DinoInfo[TotalC].idleGroupCount].count] = ReadScriptIntField(value, line, "idle-group animation");
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
			snprintf(errorBuff, sizeof(errorBuff), "Script loading error: IdleGroup2: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "startChance")) DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].start = ReadScriptFloatField(value, line, "secondary idle start chance");
		if (strstr(line, "endChance")) DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].end = ReadScriptFloatField(value, line, "secondary idle end chance");
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
			DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].anim[DinoInfo[TotalC].idle2Group[DinoInfo[TotalC].idle2GroupCount].count] = ReadScriptIntField(value, line, "secondary idle-group animation");
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
			snprintf(errorBuff, sizeof(errorBuff), "Script loading error: DeathType: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "dieAnim")) DinoInfo[TotalC].deathType[DinoInfo[TotalC].deathTypeCount].die = ReadScriptIntField(value, line, "death die animation");
		if (strstr(line, "sleepAnim")) DinoInfo[TotalC].deathType[DinoInfo[TotalC].deathTypeCount].sleep = ReadScriptIntField(value, line, "death sleep animation");
		if (strstr(line, "fallAnim")) DinoInfo[TotalC].deathType[DinoInfo[TotalC].deathTypeCount].fall = ReadScriptIntField(value, line, "death fall animation");
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
			snprintf(errorBuff, sizeof(errorBuff), "Script loading error: KillInfo: %s", DinoInfo[TotalC].Name);
			DoHalt(errorBuff);
		}
		value++;

		if (strstr(line, "hunterAnim")) DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].hunteranim = ReadScriptIntField(value, line, "kill hunter animation");
		if (strstr(line, "hunterCarryAnim")) DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].hunterswimanim = ReadScriptIntField(value, line, "kill hunter-carry animation");
		if (strstr(line, "hunterOffset")) DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].offset = ReadScriptIntField(value, line, "kill hunter offset");
		if (strstr(line, "eatAnim")) DinoInfo[TotalC].killType[DinoInfo[TotalC].killTypeCount].anim = ReadScriptIntField(value, line, "kill eat animation");
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

	// Text fields are handled before the numeric/flag dispatch and before the
	// block openers: their values are free-form, and a name or path can contain
	// a field key or a block name as a substring (a path like
	// models/massive/... or models/spawninfo/...). Once such a line is
	// recognized here it cannot select a numeric field or open a block.
	if (ScriptKeyIs(line, "name"))
	{
		CopyScriptField(DinoInfo[TotalC].Name, sizeof(DinoInfo[TotalC].Name), value, "Characters name", line);
		return;
	}

	if (ScriptKeyIs(line, "file"))
	{
		CopyScriptField(DinoInfo[TotalC].FName, sizeof(DinoInfo[TotalC].FName), value, "Characters file", line);
		return;
	}

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
		int gr = ReadScriptIntField(value, line, "trophy group");
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

	if (strstr(line, "callNo")) DinoInfo[TotalC].menuDino = ReadScriptIntField(value, line, "call number");

	if (strstr(line, "mass")) DinoInfo[TotalC].Mass = ReadScriptFloatField(value, line, "character mass");
	if (strstr(line, "length")) DinoInfo[TotalC].Length = ReadScriptFloatField(value, line, "character length");
	if (strstr(line, "radius")) DinoInfo[TotalC].Radius = ReadScriptFloatField(value, line, "character radius");
	if (strstr(line, "health")) DinoInfo[TotalC].Health0 = ReadScriptLegacyIntField(value, line, "character health");
	if (strstr(line, "basescore")) DinoInfo[TotalC].BaseScore = ReadScriptFloatField(value, line, "character base score");

	if (ScriptKeyIs(line, "ai")) DinoInfo[TotalC].Clone = ReadScriptIntField(value, line, "character AI");

	if (strstr(line, "smellK")) DinoInfo[TotalC].SmellK = ReadScriptFloatField(value, line, "character smell factor");
	if (strstr(line, "hearK")) DinoInfo[TotalC].HearK = ReadScriptFloatField(value, line, "character hearing factor");
	if (strstr(line, "lookK")) DinoInfo[TotalC].LookK = ReadScriptFloatField(value, line, "character sight factor");
	if (strstr(line, "shipdelta")) DinoInfo[TotalC].ShDelta = ReadScriptFloatField(value, line, "character ship delta");
	if (strstr(line, "scale0")) DinoInfo[TotalC].Scale0 = ReadScriptIntField(value, line, "character scale0");
	if (strstr(line, "scaleA")) DinoInfo[TotalC].ScaleA = ReadScriptIntField(value, line, "character scaleA");
	if (strstr(line, "fearCall")) DinoInfo[TotalC].fearCall[ScriptIndex(value, 64, "fear-call table")] = true;
	if (strstr(line, "dontFear")) DinoInfo[TotalC].fearCall[ScriptIndex(value, 64, "fear-call table")] = false;
	if (strstr(line, "maxdepth")) DinoInfo[TotalC].maxDepth = ReadScriptIntField(value, line, "character max depth");
	if (strstr(line, "maxalt")) DinoInfo[TotalC].maxDepth = ReadScriptIntField(value, line, "character max altitude");
	if (strstr(line, "mindepth")) DinoInfo[TotalC].minDepth = ReadScriptIntField(value, line, "character min depth");
	if (strstr(line, "minalt")) DinoInfo[TotalC].minDepth = ReadScriptIntField(value, line, "character min altitude");
	if (strstr(line, "spcdepth")) DinoInfo[TotalC].spacingDepth = ReadScriptIntField(value, line, "character depth spacing");
	if (strstr(line, "runspd")) DinoInfo[TotalC].runspd = ReadScriptFloatField(value, line, "character run speed");
	if (strstr(line, "jmpspd")) DinoInfo[TotalC].jmpspd = ReadScriptFloatField(value, line, "character jump speed");
	if (strstr(line, "wlkspd")) DinoInfo[TotalC].wlkspd = ReadScriptFloatField(value, line, "character walk speed");
	if (strstr(line, "swmspd")) DinoInfo[TotalC].swmspd = ReadScriptFloatField(value, line, "character swim speed");
	if (strstr(line, "flyspd")) DinoInfo[TotalC].flyspd = ReadScriptFloatField(value, line, "character fly speed");
	if (strstr(line, "gldspd")) DinoInfo[TotalC].gldspd = ReadScriptFloatField(value, line, "character glide speed");
	if (strstr(line, "tkfspd")) DinoInfo[TotalC].tkfspd = ReadScriptFloatField(value, line, "character takeoff speed");
	if (strstr(line, "lndspd")) DinoInfo[TotalC].lndspd = ReadScriptFloatField(value, line, "character land speed");
	if (strstr(line, "divspd")) DinoInfo[TotalC].divspd = ReadScriptFloatField(value, line, "character dive speed");
	if (strstr(line, "aggress")) DinoInfo[TotalC].aggress = ReadScriptIntField(value, line, "character aggression");
	// Per-species override of the AI-family aggression multiplier; absent or
	// non-positive keeps the clone table default (AIInfo[].agressMulti).
	if (strstr(line, "agressMulti"))
		DinoAggressMulti[TotalC] = ReadScriptIntField(value, line, "character aggression multiplier");
	if (strstr(line, "flydist")) DinoInfo[TotalC].flyDist = ReadScriptIntField(value, line, "character fly distance");
	if (strstr(line, "killdist")) DinoInfo[TotalC].killDist = ReadScriptLegacyIntField(value, line, "character kill distance");
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

	if (strstr(line, "weaveRange")) DinoInfo[TotalC].weaveRange = ReadScriptFloatField(value, line, "character weave range");
	if (strstr(line, "dontWeave")) readBool(value, DinoInfo[TotalC].dontWeave);

	//if (strstr(line, "noMoveNoRotate")) readBool(value, DinoInfo[TotalC].noMoveNoRot);

	if (strstr(line, "radR")) DinoInfo[TotalC].radarRed = ReadScriptIntField(value, line, "character radar red");
	if (strstr(line, "radG")) DinoInfo[TotalC].radarGreen = ReadScriptIntField(value, line, "character radar green");
	if (strstr(line, "radB")) DinoInfo[TotalC].radarBlue = ReadScriptIntField(value, line, "character radar blue");
	
	if (strstr(line, "bloR")) DinoInfo[TotalC].bloodRed = ReadScriptIntField(value, line, "character blood red");
	if (strstr(line, "bloG")) DinoInfo[TotalC].bloodGreen = ReadScriptIntField(value, line, "character blood green");
	if (strstr(line, "bloB")) DinoInfo[TotalC].bloodBlue = ReadScriptIntField(value, line, "character blood blue");

	if (strstr(line, "collisiondist")) DinoInfo[TotalC].maxGrad = ReadScriptIntField(value, line, "character collision distance");
	if (strstr(line, "runrotatespeed")) DinoInfo[TotalC].rotspdmulti = ReadScriptFloatField(value, line, "character rotation speed");

	if (strstr(line, "waterLevel")) DinoInfo[TotalC].waterLevel = ReadScriptIntField(value, line, "character water level");


	if (strstr(line, "CamYLand")) DinoInfo[TotalC].camDemoPoint = ReadScriptFloatField(value, line, "character land camera height");
	if (strstr(line, "CamYWater")) DinoInfo[TotalC].camDemoPointWater = ReadScriptFloatField(value, line, "character water camera height");
	if (strstr(line, "CamBaseLand")) DinoInfo[TotalC].camBase = ReadScriptFloatField(value, line, "character land camera base");
	if (strstr(line, "CamBaseWater")) DinoInfo[TotalC].camBaseWater = ReadScriptFloatField(value, line, "character water camera base");


	if (strstr(line, "dogSmell")) readBool(value, DinoInfo[TotalC].dogSmell);

	if (strstr(line, "climbDist")) DinoInfo[TotalC].climbDist = ReadScriptFloatField(value, line, "character climb distance");

	if (strstr(line, "canswim")) readBool(value, DinoInfo[TotalC].canSwim); //check animate subroutines for what this includes. LandBrach needs this attribute, but maybe rename to wade? (and default to off for landbrach ai? maybe?)

	if (strstr(line, "jumpPartFrame1")) DinoInfo[TotalC].partFrame1[CurrentJumpPartIndex()] = 1000 * ReadScriptIntField(value, line, "jump particle frame 1"); // x1000
	if (strstr(line, "jumpPartFrame2")) DinoInfo[TotalC].partFrame2[CurrentJumpPartIndex()] = 1000 * ReadScriptIntField(value, line, "jump particle frame 2"); // x1000
	if (strstr(line, "jumpPartDist")) DinoInfo[TotalC].partDist[CurrentJumpPartIndex()] = ReadScriptIntField(value, line, "jump particle distance");
	if (strstr(line, "jumpPartCnt")) DinoInfo[TotalC].partCnt[CurrentJumpPartIndex()] = ReadScriptIntField(value, line, "jump particle count");
	if (strstr(line, "jumpPartMag")) DinoInfo[TotalC].partMag[CurrentJumpPartIndex()] = ReadScriptIntField(value, line, "jump particle magnitude");
	if (strstr(line, "jumpPartOffset")) DinoInfo[TotalC].partOffset[CurrentJumpPartIndex()] = ReadScriptIntField(value, line, "jump particle offset");
	if (strstr(line, "jumpPartAngled")) readBool(value, DinoInfo[TotalC].partAngled[CurrentJumpPartIndex()]);
	if (strstr(line, "jumpPartCircle")) readBool(value, DinoInfo[TotalC].partCircle[CurrentJumpPartIndex()]);

	if (strstr(line, "idlePartFrame1")) DinoInfo[TotalC].partFrame1[CurrentIdlePartIndex()] = 1000 * ReadScriptIntField(value, line, "idle particle frame 1"); // x1000
	if (strstr(line, "idlePartFrame2")) DinoInfo[TotalC].partFrame2[CurrentIdlePartIndex()] = 1000 * ReadScriptIntField(value, line, "idle particle frame 2"); // x1000
	if (strstr(line, "idlePartDist")) DinoInfo[TotalC].partDist[CurrentIdlePartIndex()] = ReadScriptIntField(value, line, "idle particle distance");
	if (strstr(line, "idlePartCnt")) DinoInfo[TotalC].partCnt[CurrentIdlePartIndex()] = ReadScriptIntField(value, line, "idle particle count");
	if (strstr(line, "idlePartMag")) DinoInfo[TotalC].partMag[CurrentIdlePartIndex()] = ReadScriptIntField(value, line, "idle particle magnitude");
	if (strstr(line, "idlePartOffset")) DinoInfo[TotalC].partOffset[CurrentIdlePartIndex()] = ReadScriptIntField(value, line, "idle particle offset");
	if (strstr(line, "idlePartAngled")) readBool(value, DinoInfo[TotalC].partAngled[CurrentIdlePartIndex()]);
	if (strstr(line, "idlePartCircle")) readBool(value, DinoInfo[TotalC].partCircle[CurrentIdlePartIndex()]);

	if (strstr(line, "DangerFish")) readBool(value, DinoInfo[TotalC].DangerFish);

	if (strstr(line, "TRexObjCollide")) readBool(value, DinoInfo[TotalC].TRexObjCollide);

	if (strstr(line, "Mystery")) readBool(value, DinoInfo[TotalC].Mystery);
	if (strstr(line, "HideBinoc")) readBool(value, DinoInfo[TotalC].HideBinoc);

	if (strstr(line, "JumpRange")) DinoInfo[TotalC].jumpRange = ReadScriptIntField(value, line, "character jump range");

	if (strstr(line, "Weapon")) {
		DinoInfo[TotalC].Weapon = ScriptIndex(value, TotalW, "character weapon");
		DinoInfo[TotalC].Reload = WeapInfo[DinoInfo[TotalC].Weapon].Shots;
		if (WeapInfo[DinoInfo[TotalC].Weapon].Reload)
			DinoInfo[TotalC].Reload = WeapInfo[DinoInfo[TotalC].Weapon].Reload;
	}

	if (strstr(line, "runAnim")) DinoInfo[TotalC].runAnim = ReadScriptIntField(value, line, "run animation");
	if (strstr(line, "jumpAnim")) DinoInfo[TotalC].jumpAnim = ReadScriptIntField(value, line, "jump animation");
	if (strstr(line, "walkAnim")) DinoInfo[TotalC].walkAnim = ReadScriptIntField(value, line, "walk animation");
	if (strstr(line, "swimAnim")) DinoInfo[TotalC].swimAnim = ReadScriptIntField(value, line, "swim animation");
	if (strstr(line, "flyAnim")) DinoInfo[TotalC].flyAnim = ReadScriptIntField(value, line, "fly animation");
	if (strstr(line, "diveAnim")) DinoInfo[TotalC].diveAnim = ReadScriptIntField(value, line, "dive animation");
	if (strstr(line, "glideAnim")) DinoInfo[TotalC].glideAnim = ReadScriptIntField(value, line, "glide animation");
	if (strstr(line, "takeoffAnim")) DinoInfo[TotalC].takeoffAnim = ReadScriptIntField(value, line, "takeoff animation");
	if (strstr(line, "landAnim")) DinoInfo[TotalC].landAnim = ReadScriptIntField(value, line, "land animation");
	if (strstr(line, "slideAnim")) DinoInfo[TotalC].slideAnim = ReadScriptIntField(value, line, "slide animation");
	if (strstr(line, "shakeLAnim")) DinoInfo[TotalC].shakeLandAnim = ReadScriptIntField(value, line, "shake-land animation");
	if (strstr(line, "shakeWAnim")) DinoInfo[TotalC].shakeWaterAnim = ReadScriptIntField(value, line, "shake-water animation");
	if (strstr(line, "climbAnim")) DinoInfo[TotalC].climbAnim = ReadScriptIntField(value, line, "climb animation");
	if (strstr(line, "fireAnim")) DinoInfo[TotalC].fireAnim = ReadScriptIntField(value, line, "fire animation");
	if (strstr(line, "reloadAnim")) DinoInfo[TotalC].reloadAnim = ReadScriptIntField(value, line, "reload animation");

	
	if (strstr(line, "lookAnim") || strstr(line, "fishIdleAnim")) {
		if (idleOverwrite) {
			DinoInfo[TotalC].lookCount = 0;
			idleOverwrite = false;
		}
		RequireScriptSlot(DinoInfo[TotalC].lookCount, 32, "look animations");
		DinoInfo[TotalC].lookAnim[DinoInfo[TotalC].lookCount] = ReadScriptIntField(value, line, "look animation");
		DinoInfo[TotalC].lookCount++;
	}

	if (strstr(line, "smellAnim")) {
		if (idle2Overwrite) {
			DinoInfo[TotalC].smellCount = 0;
			idle2Overwrite = false;
		}
		RequireScriptSlot(DinoInfo[TotalC].smellCount, 32, "smell animations");
		DinoInfo[TotalC].smellAnim[DinoInfo[TotalC].smellCount] = ReadScriptIntField(value, line, "smell animation");
		DinoInfo[TotalC].smellCount++;
	}
	

	if (strstr(line, "roarAnim")) {
		if (roarOverwrite) {
			DinoInfo[TotalC].roarCount = 0;
			roarOverwrite = false;
		}
		RequireScriptSlot(DinoInfo[TotalC].roarCount, 32, "roar animations");
		DinoInfo[TotalC].roarAnim[DinoInfo[TotalC].roarCount] = ReadScriptIntField(value, line, "roar animation");
		DinoInfo[TotalC].roarCount++;
	}

	if (strstr(line, "waterDAnim")) {
		if (waterDieOverwrite) {
			DinoInfo[TotalC].waterDieCount = 0;
			waterDieOverwrite = false;
		}
		RequireScriptSlot(DinoInfo[TotalC].waterDieCount, 32, "water-death animations");
		DinoInfo[TotalC].waterDieAnim[DinoInfo[TotalC].waterDieCount] = ReadScriptIntField(value, line, "water-death animation");
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
	ReadScriptCommandLineOptions(tempProjectName, timeOfDay, dinSelect);

	//time
	for (const auto& argument : Platform::Arguments())
	{
		const char* s = argument.c_str();
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

  stream = Platform::OpenTextFile("HUNTDAT\\_res.txt", "r");
  if (!stream) DoHalt("Can't open resources file _res.txt");

  TotalC = 0;
  TotalMA = 0;

  char tempProjectName[128];
  int timeOfDay, dinSelect;
  if (HasCommandLineOption("-survival"))
    g_GameMode = GameMode::SurvivalMode;

  ScriptCommonOptions commonOptions;
  rewind(stream);
  ReadCommonOptionsFromScript(stream, commonOptions);
  rewind(stream);
  ApplySurvivalCommonOptions(commonOptions);

  ReadScriptCommandLineOptions(tempProjectName, timeOfDay, dinSelect);
  

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

  FlushScriptDiagnostics();
}
