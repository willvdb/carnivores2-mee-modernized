// ==========================================================================
// CommandLine.cpp
// ==========================================================================

#include "Hunt.h"
#include "Core/CommandLineParse.h"
#include "Core/ScoreMod.h"
#include "Platform/System.h"
#include "Session/Session.h"
#include "Game/ResolutionSelection.h"
#include "Game/RefreshPreference.h"
#include "Game/DisplayPreference.h"
#ifdef _WIN32
#include "Network/NetworkManager.h"
#endif

// The engine's destination for each wire slot. Kept here rather than in
// ScoreMod.h because these globals belong to the engine, not to the format.
static float* ScoreModTarget(ScoreModSlot slot)
{
  switch (slot) {
  case ScoreModSlot::Camo:     return &ScoreMod_Camo;
  case ScoreModSlot::Radar:    return &ScoreMod_Radar;
  case ScoreModSlot::Scent:    return &ScoreMod_Scent;
  case ScoreModSlot::Double:   return &ScoreMod_Double;
  case ScoreModSlot::Tranq:    return &ScoreMod_Tranq;
  case ScoreModSlot::Observer: return &ScoreMod_Observer;
  case ScoreModSlot::Count:    break;
  }
  return nullptr;
}

static bool equals_nocase(const char* lhs, const char* rhs)
{
  return LegacyText::Compare(lhs, rhs) == 0;
}

static bool starts_with_nocase(const char* text, const char* prefix)
{
  return LegacyText::Compare(text, prefix, strlen(prefix)) == 0;
}

static void LogInvalidCommandLineOption(const char* option)
{
  char message[128];
  snprintf(message, sizeof(message),
            "Command line: ignoring invalid %s option value.\n", option);
  PrintLog(message);
}

void ProcessCommandLine()
{
  int requestedWidth = DisplayConfiguration.size.width;
  int requestedHeight = DisplayConfiguration.size.height;
  auto requestedMode = DisplayConfiguration.mode;
  bool hasRequestedResolution = false;
  bool hasRequestedMode = false;

  const auto& arguments = Platform::Arguments();
  for (std::size_t i = EngineSession::Active() ? 1 : 0; i < arguments.size(); ++i)
  {
    const char* s = arguments[i].c_str();
    const char* value = nullptr;
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

    if (CommandLineOptionValue(s, "/res=", &value) ||
        CommandLineOptionValue(s, "-res=", &value)) {
      int width, height;
      if (ParseCommandLineResolution(value, width, height)) {
        requestedWidth = width;
        requestedHeight = height;
        hasRequestedResolution = true;
      } else {
        LogInvalidCommandLineOption("res=");
      }
      continue;
    }

    if (CommandLineOptionValue(s, "x=", &value))
    {
      float coordinate = 0.0f;
      if (ParseCommandLineFloat(value, coordinate)) {
        const float scaled = coordinate * 256.f;
        if (!std::isfinite(scaled)) {
          LogInvalidCommandLineOption("x=");
          continue;
        }
        PlayerX = scaled;
        LockLanding = true;
      } else {
        LogInvalidCommandLineOption("x=");
      }
      continue;
    }

    if (CommandLineOptionValue(s, "y=", &value))
    {
      float coordinate = 0.0f;
      if (ParseCommandLineFloat(value, coordinate)) {
        const float scaled = coordinate * 256.f;
        if (!std::isfinite(scaled)) {
          LogInvalidCommandLineOption("y=");
          continue;
        }
        PlayerZ = scaled;
        LockLanding = true;
      } else {
        LogInvalidCommandLineOption("y=");
      }
      continue;
    }

    if (CommandLineOptionValue(s, "reg=", &value))
    {
      int registration = 0;
      if (ParseCommandLineInt(value, registration))
        TrophyRoom.RegNumber = registration;
      else
        LogInvalidCommandLineOption("reg=");
      continue;
    }

    if (CommandLineOptionValue(s, "prj=", &value))
    {
      const CommandLineCopyResult result =
          CopyCommandLineOption(ProjectName, sizeof(ProjectName), s, "prj=");
      if (result == CommandLineCopyResult::Invalid)
        LogInvalidCommandLineOption("prj=");
      continue;
    }

    if (CommandLineOptionValue(s, "din=", &value))
    {
      int dinoFlags = 0;
      if (ParseCommandLineInt(value, dinoFlags) && dinoFlags >= 0 && dinoFlags <= 1023)
        TargetDino = dinoFlags * 1024;
      else
        LogInvalidCommandLineOption("din=");
      continue;
    }

    if (CommandLineOptionValue(s, "wep=", &value))
    {
      int weaponFlags = 0;
      if (ParseCommandLineInt(value, weaponFlags) && weaponFlags >= 0 && weaponFlags <= 1023)
        WeaponPres = weaponFlags;
      else
        LogInvalidCommandLineOption("wep=");
      continue;
    }

    if (CommandLineOptionValue(s, "dtm=", &value))
    {
      int dayNight = 0;
      if (ParseCommandLineInt(value, dayNight) && dayNight >= 0 && dayNight <= 2)
        OptDayNight = dayNight;
      else
        LogInvalidCommandLineOption("dtm=");
      continue;
    }

#ifdef _WIN32
    if (CommandLineOptionValue(s, "server=", &value))
    {
      const CommandLineCopyResult result =
          CopyCommandLineOption(g_Network.m_serverAddress,
                                sizeof(g_Network.m_serverAddress), s, "server=");
      if (result == CommandLineCopyResult::Invalid)
        LogInvalidCommandLineOption("server=");
      continue;
    }

#endif

    if (equals_nocase(s, "-debug"))   DEBUG = true;
    if (equals_nocase(s, "-double"))  DoubleAmmo = true;
    if (equals_nocase(s, "-huntdog"))  g_GameMode = GameMode::DogMode;
    if (equals_nocase(s, "-nightvision")) { NightVisionMode = true; g_GameMode = GameMode::NightVision; }
    if (equals_nocase(s, "-radar"))   RadarMode = true;
    if (equals_nocase(s, "-survival"))  g_GameMode = GameMode::SurvivalMode;
    if (equals_nocase(s, "-sonar"))   { SonarMode = true; g_GameMode = GameMode::SonarMode; }
    if (equals_nocase(s, "-scanner"))   { ScannerMode = true; g_GameMode = GameMode::ScannerMode; }
    if (equals_nocase(s, "-scent"))   ScentMode = true;
    if (equals_nocase(s, "-camo"))   CamoMode = true;
    if (equals_nocase(s, "-multiplayer"))   Multiplayer = true;
    if (equals_nocase(s, "-host"))   Host = true;
    if (equals_nocase(s, "-cisk"))   CiskMode = true;
    if (equals_nocase(s, "-tranq")) Tranq = true;
    if (equals_nocase(s, "-observ")) ObservMode = true;

    // smod=camo,radar,scent,double,tranq,observer. The order lives in
    // Hunt/Core/ScoreMod.h and is shared with the Menu's assembly, so the two
    // cannot drift apart. Modders can override these via the 'accessories {}'
    // block in _RES.TXT (parsed by Menu/Resources.cpp ReadAccessories()).
    if (CommandLineOptionValue(s, "smod=", &value)) {
      float mods[kScoreModSlotCount] = {0};
      const int got = ParseScoreModPayload(value, mods);
      for (int i = 0; i < got; ++i) {
        const ScoreModSlot slot = kScoreModWireOrder[i];
        float* target = ScoreModTarget(slot);
        if (target) *target = mods[static_cast<int>(slot)];
      }
    }

  }

  if (hasRequestedMode) DisplayConfiguration.mode = requestedMode;
  if (hasRequestedResolution)
    GameDisplay::ApplyCommandLineResolution(DisplayConfiguration, {requestedWidth, requestedHeight},
                                            ResolutionList, ResCount, OptRes, CurRes);
  SyncLegacyDisplayState();
}
