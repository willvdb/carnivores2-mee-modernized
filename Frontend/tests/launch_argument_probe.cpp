// Exact production CommandLine.cpp body with state/services doubled. No graphics.
#include "Game/DisplayConfiguration.h"
#include "Game/DisplayPreference.h"
#include "Game/RefreshPreference.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <stdexcept>
namespace Platform {
std::vector<std::string> arguments;
const std::vector<std::string>& Arguments() { return arguments; }
}
namespace EngineSession { bool Active() { return true; } }
GameDisplay::Configuration DisplayConfiguration;
Platform::Size ResolutionList[1] = {{800,600}};
int ResCount=1, OptRes=0, CurRes=0, TargetDino=0, WeaponPres=0, OptDayNight=0;
float PlayerX=0, PlayerZ=0;
struct { int RegNumber=0; } TrophyRoom;
struct { char m_serverAddress[128]{}; } g_Network;
char ProjectName[128]{}, logt[1024]{};
bool LockLanding=false, DEBUG=false, DoubleAmmo=false, NightVisionMode=false;
bool RadarMode=false, SonarMode=false, ScannerMode=false, ScentMode=false, CamoMode=false;
bool Multiplayer=false, Host=false, CiskMode=false, Tranq=false, ObservMode=false;
enum class GameMode { Hunt, DogMode, NightVision, SurvivalMode, SonarMode, ScannerMode };
GameMode g_GameMode=GameMode::Hunt;
float ScoreMod_Camo=0, ScoreMod_Radar=0, ScoreMod_Scent=0, ScoreMod_Double=0, ScoreMod_Tranq=0, ScoreMod_Observer=0;
void PrintLog(const char*) {}
void DoHalt2(const char* text) { throw std::runtime_error(text); }
void SyncLegacyDisplayState() {}
#include "launch_commandline.inc"
int main(int argc,char** argv) {
    Platform::arguments.assign(argv, argv+argc);
    ProcessCommandLine();
    std::printf("{\"din\":%d,\"wep\":%d,\"time\":%d,\"observer\":%s,\"equipment\":%s,"
                "\"modifiers\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]}\n",
        TargetDino, WeaponPres, OptDayNight, ObservMode?"true":"false",
        (DoubleAmmo||NightVisionMode||RadarMode||ScentMode||CamoMode||Tranq)?"true":"false",
        ScoreMod_Camo,ScoreMod_Radar,ScoreMod_Scent,ScoreMod_Double,ScoreMod_Tranq,ScoreMod_Observer);
}
