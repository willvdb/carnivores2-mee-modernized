// menu_resource_seams.cpp -- narrow test doubles for the production menu
// resource readers.
//
// Menu/Resources.cpp references exactly these globals and ShowErrorMessage();
// defining them here lets the real translation unit link into a test without
// creating a menu window, a profile, or any Win32 state. Keep this list to the
// symbols the readers actually touch: growing it means the test is drifting
// away from a narrow seam.

#include "Hunt.h"

std::vector<DinoInfo> g_DinoInfo;
std::vector<WeapInfo> g_WeapInfo;
std::vector<AreaInfo> g_AreaInfo;
std::vector<UtilInfo> g_UtilInfo;
std::map<std::string, float> g_AccessoryScoreMods;
std::vector<int32_t> g_AccessoryPrices;
UtilInfo g_ObserverInfo;
Options g_Options;
std::string g_SavedHuntArea;
unsigned long long g_SavedHuntDinos = 0;
unsigned long long g_SavedHuntWeapons = 0;
unsigned long long g_SavedHuntUtils = 0;
int32_t g_SavedHuntTime = 0;
bool g_HasSavedHunt = false;
uint32_t g_ProfileIndex = 0;
uint32_t g_StartCredits = 0;
int g_ResCount = 0;
TRes g_ResolutionList[128];
SoundFX g_MenuSound_Go;
SoundFX g_MenuSound_Ambient;
SoundFX g_MenuSound_Move;
SoundFX g_MenuSound_Type;
SoundFX g_MenuSound_TypeGo;

void ShowErrorMessage(const std::string&)
{
}
