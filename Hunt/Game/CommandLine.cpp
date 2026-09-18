// ==========================================================================
// CommandLine.cpp
// ==========================================================================

#include "Hunt.h"
#include "Platform/System.h"
#include "Game/ResolutionSelection.h"
#include "Game/RefreshPreference.h"
#include "Game/DisplayPreference.h"
#ifdef _WIN32
#include "Network/NetworkManager.h"
#endif

static bool equals_nocase(const char* lhs, const char* rhs)
{
  return LegacyText::Compare(lhs, rhs) == 0;
}

static bool starts_with_nocase(const char* text, const char* prefix)
{
  return LegacyText::Compare(text, prefix, strlen(prefix)) == 0;
}

void ProcessCommandLine()
{
  auto parse_resolution = [&](const char* value, int& width, int& height) -> bool {
    width = 0;
    height = 0;
    char sep = 0;
    // Single case-insensitive WxH parse (matches EngineInit LoadConfig).
    if (sscanf(value, "%d%c%d", &width, &sep, &height) != 3 ||
        (sep != 'x' && sep != 'X')) {
      return false;
    }
    return width > 0 && height > 0;
  };

  int requestedWidth = DisplayConfiguration.size.width;
  int requestedHeight = DisplayConfiguration.size.height;
  auto requestedMode = DisplayConfiguration.mode;
  bool hasRequestedResolution = false;
  bool hasRequestedMode = false;

  for (const auto& argument : Platform::Arguments())
  {
    const char* s = argument.c_str();
    const auto displayArgument = GameDisplay::ApplyMonitorArgument(s, DisplayConfiguration.monitor);
    if (displayArgument != GameDisplay::DisplayArgument::Unrelated) {
      if (displayArgument == GameDisplay::DisplayArgument::Invalid) {
        snprintf(logt, sizeof(logt), "Command line: invalid display argument '%.24s': expected display index/primary or versioned display-id; using primary display.\n", s);
        PrintLog(logt);
      }
      continue; // Also consume malformed values before legacy substring parsing.
    }
    const auto refreshArgument = GameDisplay::ApplyRefreshArgument(s, DisplayConfiguration.refresh);
    if (refreshArgument != GameDisplay::RefreshArgument::Unrelated) {
      if (refreshArgument == GameDisplay::RefreshArgument::Invalid) {
        PrintLog("Command line: refresh: ");
        PrintLog(GameDisplay::RefreshSyntax);
      }
      continue;
    }
#ifndef _WIN32
    if (equals_nocase(s,"-multiplayer") || equals_nocase(s,"-host") || starts_with_nocase(s,"server="))
      DoHalt2("Multiplayer is not available in the Linux build.");
#endif

    if (equals_nocase(s, "/nofullscreen") || equals_nocase(s, "-nofullscreen") ||
        equals_nocase(s, "/windowed") || equals_nocase(s, "-windowed")) {
      requestedMode = Platform::WindowMode::Windowed;
      hasRequestedMode = true;
      continue;
    }

    if (equals_nocase(s, "/fullscreen") || equals_nocase(s, "-fullscreen")) {
      requestedMode = Platform::WindowMode::Exclusive;
      hasRequestedMode = true;
      continue;
    }

    if (equals_nocase(s, "/borderless") || equals_nocase(s, "-borderless")) {
      requestedMode = Platform::WindowMode::Borderless;
      hasRequestedMode = true;
      continue;
    }

    if (equals_nocase(s, "/vmode1")) { requestedWidth = 320; requestedHeight = 240; hasRequestedResolution = true; continue; }
    if (equals_nocase(s, "/vmode2")) { requestedWidth = 400; requestedHeight = 300; hasRequestedResolution = true; continue; }
    if (equals_nocase(s, "/vmode3")) { requestedWidth = 512; requestedHeight = 384; hasRequestedResolution = true; continue; }
    if (equals_nocase(s, "/vmode4")) { requestedWidth = 640; requestedHeight = 480; hasRequestedResolution = true; continue; }
    if (equals_nocase(s, "/vmode5")) { requestedWidth = 800; requestedHeight = 600; hasRequestedResolution = true; continue; }

    if (starts_with_nocase(s, "/res=") || starts_with_nocase(s, "-res=")) {
      int width, height;
      if (parse_resolution(strchr(s, '=') + 1, width, height)) {
        requestedWidth = width;
        requestedHeight = height;
        hasRequestedResolution = true;
      }
      continue;
    }

    if (strstr(s,"x="))
    {
      PlayerX = static_cast<float>(atof(&s[2]))*256.f;
      LockLanding = true;
    }
    if (strstr(s,"y="))
    {
      PlayerZ = static_cast<float>(atof(&s[2]))*256.f;
      LockLanding = true;
    }

    if (strstr(s,"reg=")) TrophyRoom.RegNumber = atoi(&s[4]);
    if (strstr(s,"prj=")) strcpy(ProjectName, (s+4));
    if (strstr(s,"din=")) TargetDino = (atoi(&s[4])*1024);
	if (strstr(s, "wep=")) WeaponPres = atoi(&s[4]);
	if (strstr(s, "dtm=")) OptDayNight = atoi(&s[4]);
#ifdef _WIN32
    if (strstr(s, "server=")) strcpy(g_Network.m_serverAddress, (s + 7));
#endif

    if (strstr(s,"-debug"))   DEBUG = true;
    if (strstr(s,"-double"))  DoubleAmmo = true;
	if (strstr(s, "-huntdog"))  g_GameMode = GameMode::DogMode;
	if (strstr(s, "-nightvision")) { NightVisionMode = true; g_GameMode = GameMode::NightVision; }
    if (strstr(s,"-radar"))   RadarMode = true;
	if (strstr(s, "-survival"))  g_GameMode = GameMode::SurvivalMode;
	if (strstr(s, "-sonar"))   { SonarMode = true; g_GameMode = GameMode::SonarMode; }
	if (strstr(s, "-scanner"))   { ScannerMode = true; g_GameMode = GameMode::ScannerMode; }
	if (strstr(s, "-scent"))   ScentMode = true;
	if (strstr(s, "-camo"))   CamoMode = true;
	if (strstr(s, "-multiplayer"))   Multiplayer = true;
	if (strstr(s, "-host"))   Host = true;
	if (strstr(s, "-cisk"))   CiskMode = true;
    if (strstr(s,"-tranq")) Tranq = true;
    if (strstr(s,"-observ")) ObservMode = true;

	// smod=camo,radar,scent,double,tranq,observer
	// Order must match the Menu's assembly in Menu.cpp and the defaults
	// in InitEngine(). Modders can override these via the 'accessories {}'
	// block in _RES.TXT (parsed by Menu/Resources.cpp ReadAccessories()).
	if (strstr(s, "smod=")) {
		float mods[6] = {0};
		int got = sscanf(s + 5, "%f,%f,%f,%f,%f,%f",
			&mods[0], &mods[1], &mods[2], &mods[3], &mods[4], &mods[5]);
		if (got >= 1) ScoreMod_Camo     = mods[0];
		if (got >= 2) ScoreMod_Radar    = mods[1];
		if (got >= 3) ScoreMod_Scent    = mods[2];
		if (got >= 4) ScoreMod_Double   = mods[3];
		if (got >= 5) ScoreMod_Tranq    = mods[4];
		if (got >= 6) ScoreMod_Observer = mods[5];
	}

  }

  if (hasRequestedMode) DisplayConfiguration.mode = requestedMode;
  if (hasRequestedResolution)
    GameDisplay::ApplyCommandLineResolution(DisplayConfiguration, {requestedWidth, requestedHeight},
                                            ResolutionList, ResCount, OptRes, CurRes);
  SyncLegacyDisplayState();
}
