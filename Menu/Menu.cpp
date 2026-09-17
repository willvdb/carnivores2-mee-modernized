/***************************************************
* AtmosFear 2.1
* Menu.cpp
*
* Main Menu Code
*
*/

#include "Hunt.h"
#include "LegacyAssetPath.h"
#include "../Shared/LegacyProfile.h"
#include "SliderMath.h"
#include <cassert>
#include <algorithm>
#include <cmath>

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>


class MenuSet {
public:
	int32_t		x0, y0;
	uint32_t	Count;
	uint32_t	Offset;
	std::vector<std::pair<std::string, bool>> Item;
	int32_t		Selected;
	int32_t		Hilite;
	RECT		Rect;
	int32_t		Padding;

	MenuSet() :
		x0(0), y0(0),
		Count(0),
		Offset(0),
		Item(),
		Selected(-1),
		Hilite(-1),
		Rect({ 0,0,0,0 }),
		Padding(0)
	{}

	void AddItem(const std::string& txt);
};


enum MenuSetEnum {
	OPT_GAME = 0,
	OPT_KEYBINDINGS = 1,
	OPT_VIDEO = 2,
	OPT_MAX
};

// ======================================================================= //
// Layout System
// ======================================================================= //
//
// Modern layout system that auto-calculates spacing and supports proper
// text alignment. Replaces hardcoded pixel coordinates for menu sections.
//
namespace Layout {

	// Text alignment options (matches DTA_* values from wingdi.h)
	enum Align {
		LEFT   = 0,
		RIGHT  = 2,
		CENTER = 6
	};

	// A vertical column of items with consistent spacing and alignment
	struct Column {
		int x0;            // X position (left for LEFT, right for RIGHT, center for CENTER)
		int yStart;        // Y position of first item
		int yEnd;          // Y position of last item (bottom of column)
		int itemCount;     // Number of items to display
		Align align;       // Text alignment
		int padding;       // Extra padding from edges

		Column() : x0(0), yStart(0), yEnd(0), itemCount(0), align(LEFT), padding(0) {}

		// Calculate Y position for item i, auto-spacing to fill panel height
		int GetItemY(int i) const {
			if (itemCount <= 0) return yStart;
			int availableHeight = yEnd - yStart;
			int spacing = availableHeight / itemCount;
			return yStart + (spacing * i) + (spacing / 2) - 7; // Center vertically in slot
		}

		// Get the actual spacing being used
		int GetSpacing() const {
			if (itemCount <= 0) return 20;
			return (yEnd - yStart) / itemCount;
		}
	};

	// A rectangular region in the background artwork
	struct Panel {
		int x, y, w, h;

		Panel() : x(0), y(0), w(0), h(0) {}
		Panel(int x, int y, int w, int h) : x(x), y(y), w(w), h(h) {}

		int GetRight() const { return x + w; }
		int GetBottom() const { return y + h; }
	};

	// Helper: Get X position based on alignment within a panel
	int GetAlignedX(int panelLeft, int panelRight, Align align, int padding = 15) {
		switch (align) {
			case LEFT:   return panelLeft + padding;
			case RIGHT:  return panelRight - padding;
			case CENTER: return (panelLeft + panelRight) / 2;
			default:     return panelLeft + padding;
		}
	}

} // namespace Layout

// ======================================================================= //
// Options Screen Layout Definition
// ======================================================================= //
// These values follow the original StartLegacy option screen spacing:
// labels are right-aligned, values sit just to the right of the label column,
// and sliders use the same track positions as the legacy artwork.
//
namespace OptionsLayout {

	const int ROW_HEIGHT = 20;
	const int ROW_HEIGHT_VIDEO = 21; // Fits the extra C2ME video rows inside the legacy panel.

	// Panel regions from the original options artwork.
	const Layout::Panel PANEL_GAME     (40,  75,  340, 175);  // Top-left game settings
	const Layout::Panel PANEL_CONTROLS (422, 75,  338, 425);  // Right side key bindings
	const Layout::Panel PANEL_VIDEO    (40, 285, 340, 255);  // Bottom-left video/settings

	// Legacy-aligned option columns. yStart is biased so Column::GetItemY()
	// returns the exact baseline used by the original menu (100, 353, 71).
	const int GAME_LABEL_RIGHT_X   = 190;
	const int OPTION_VALUE_X       = 205;
	const int OPTION_SLIDER_X      = OPTION_VALUE_X;
	const int OPTION_SLIDER_W      = 123;

	const int CONTROLS_LABEL_RIGHT_X = 600;
	const int CONTROLS_VALUE_X       = 617;
	const int CONTROLS_SLIDER_X      = CONTROLS_VALUE_X + 1;
	const int CONTROLS_SLIDER_W      = 123;

	const int GAME_FIRST_Y   = 80;
	const int VIDEO_FIRST_Y  = 353;
	const int CONTROLS_FIRST_Y = 71;

	Layout::Column MakeColumn(int x0, int firstY, int count, Layout::Align align, int rowHeight = ROW_HEIGHT)
	{
		Layout::Column col;
		col.x0 = x0;
		col.yStart = firstY - 4;
		col.yEnd = col.yStart + (count * rowHeight);
		col.itemCount = count;
		col.align = align;
		return col;
	}

	// Labels are right-aligned in their panel, values are left-aligned.
	Layout::Column MakeGameLabels(int count)
	{
		return MakeColumn(GAME_LABEL_RIGHT_X, GAME_FIRST_Y, count, Layout::RIGHT);
	}

	Layout::Column MakeGameValues(int count)
	{
		return MakeColumn(OPTION_VALUE_X, GAME_FIRST_Y, count, Layout::LEFT);
	}

	Layout::Column MakeControlsLabels(int count)
	{
		return MakeColumn(CONTROLS_LABEL_RIGHT_X, CONTROLS_FIRST_Y, count, Layout::RIGHT);
	}

	Layout::Column MakeControlsValues(int count)
	{
		return MakeColumn(CONTROLS_VALUE_X, CONTROLS_FIRST_Y, count, Layout::LEFT);
	}

	Layout::Column MakeVideoLabels(int count)
	{
		return MakeColumn(GAME_LABEL_RIGHT_X, VIDEO_FIRST_Y, count, Layout::RIGHT, ROW_HEIGHT_VIDEO);
	}

	Layout::Column MakeVideoValues(int count)
	{
		return MakeColumn(OPTION_VALUE_X, VIDEO_FIRST_Y, count, Layout::LEFT, ROW_HEIGHT_VIDEO);
	}

} // namespace OptionsLayout


enum DrawTextAlignEnum {
	// Uses the same values as wingdi.h TA_LEFT and so on.
	// TODO: Write a conversion function for Linux if we port it to Linux
	DTA_LEFT = 0,
	DTA_RIGHT = 2,
	DTA_CENTER = 6,
	DTA_BOTTOM = 8
};


std::pair<unsigned, unsigned> g_HuntInfo;
Picture g_TrackBar[2];
MenuSet MenuOptions[3];
MenuSet MenuRegistry;
MenuSet MenuHunt[4];

void* lpVideoBuf;
HDC hdcCMain;
HBITMAP bmpMain;
HBITMAP hbmpOld;
HFONT hfntOld;

POINT g_CursorPos;
int g_WaitKey = -1;
bool g_KeyboardUsed = false;
static int g_LastHoverId = -1;

// Registry name-list double-click tracking (double-click a name to enter).
static DWORD g_LastListClickTime = 0;
static int g_LastListClickIndex = -1;


// String table
const char g_GitHubURL[] = "https://github.com/carnivores-cpe/Carn2-Menu";
const char st_BoolText[2][4] = { "Off", "On" };
const char st_UnitText[2][10] = { "Metric", "Imperial" };
const char st_HMLText[4][8] = { "Low", "Medium", "High", "Ultra" };
const char st_TextureText[3][5] = { "Low", "High", "Auto" };
const char st_AlphaKeyText[2][14] = { "Color Key", "Alpha Channel" };
// The new menu exposes only Software and OpenGL. Legacy Direct3D values are
// normalized back to OpenGL so old profiles/configs never launch v_d3d.ren.
const char st_RenText[kRenderAPI_Count][12] = { "Software", "OpenGL" };
const char g_RendererFile[kRenderAPI_Count][8] = { "v_soft", "v_gl" };
const char st_AudText[2][16] = { "DirectSound", "OpenAL Soft" };
const char st_FpsText[kFpsLimitCount][12] = { "Unlimited", "60", "120", "240" };

// Display mode video option. Matches the engine's display_mode config key:
// 0=windowed, 1=exclusive fullscreen, 2=borderless fullscreen.
const int kDisplayModeCount = 3;
const char st_DisplayModeText[kDisplayModeCount][16] = { "Windowed", "Fullscreen", "Borderless" };

// Accessory indices. MenuHunt[3].Item and g_UtilInfo stay parallel and fully
// populated (see UtilInfo note in Hunt.h), so launch/smod code can index
// both arrays with these. NOTE: _iceage inserts "Supply drop" at slot 4
// and shifts NV/tranq — these constants are C2-only.
constexpr int kAccCamo = 0;
constexpr int kAccRadar = 1;
constexpr int kAccScent = 2;
constexpr int kAccDouble = 3;
constexpr int kAccNV = 4;
constexpr int kAccTranq = 5;
constexpr int kAccCount = 6;

static void NormalizeRendererOption()
{
	const int32_t normalized = NormalizeMenuRenderAPI(g_Options.RenderAPI);
	if (normalized != g_Options.RenderAPI) {
		g_Options.RenderAPI = normalized;
		SaveConfig();
	}
}

// Append the display-mode launch flag matching the menu's Display Mode
// option (0=windowed, 1=exclusive fullscreen, 2=borderless fullscreen).
// The same key is written to config.cfg (display_mode), so the engine
// applies it even when launched without these flags; the flag here just
// makes the intent explicit and covers any config-less launch.
// Borderless is the GL default because DWM-composed fullscreen keeps
// Windows 11 color management active on wide-gamut displays. The
// software renderer has no borderless presentation, so its "borderless"
// pick maps to exclusive fullscreen (its classic behaviour).
static void AppendDisplayModeLaunchFlag(std::stringstream& params)
{
	switch (g_Options.DisplayMode) {
	case 0: params << " -windowed"; break;
	case 1: params << " -fullscreen"; break;
	case 2:
	default:
		if (NormalizeMenuRenderAPI(g_Options.RenderAPI) == kRenderAPI_OpenGL)
			params << " -borderless";
		else
			params << " -fullscreen";
		break;
	}
}


// ======================================================================= //
// Global Layout Columns (initialized in InitInterface)
// ======================================================================= //
Layout::Column g_GameLabels, g_GameValues;
Layout::Column g_ControlsLabels, g_ControlsValues;
Layout::Column g_VideoLabels, g_VideoValues;


int MapVKKey(int k);


bool IsPointInRect(POINT& p, RECT& rc)
{
	return (p.x > rc.left && p.y > rc.top && p.x < rc.right&& p.y < rc.bottom);
}


void WaitForMouseRelease()
{
	while (GetAsyncKeyState(VK_RBUTTON) & 0x80000000);
	while (GetAsyncKeyState(VK_MBUTTON) & 0x80000000);
	while (GetAsyncKeyState(VK_LBUTTON) & 0x80000000);
}


void AcceptNewKey()
{
	uint8_t keystate[256];

	if (GetKeyboardState(keystate)) {

		if (keystate[VK_ESCAPE] & 128)
		{
			// Check if we're setting the NightVision key (the last keybinding item before Reverse mouse)
			const int nvIndex = static_cast<int>(MenuOptions[OPT_KEYBINDINGS].Item.size() - 3);
			if (g_WaitKey == nvIndex) {
				g_Options.NightVisionKey = 0;
			} else {
				*((uint32_t*)(&g_Options.KeyMap) + g_WaitKey) = 0;
			}
			g_WaitKey = -2;
			return;
		}
		else
		{
			for (int k = 0; k < 255; k++)
			{
				if (keystate[k] & 128)
				{
					// Check if this is the NightVision key slot
					const int nvIndex = static_cast<int>(MenuOptions[OPT_KEYBINDINGS].Item.size() - 3);
					if (g_WaitKey == nvIndex) {
						// Avoid reassigning an already-used key — check only other NV keys
						if (g_Options.NightVisionKey == k) return;
						g_Options.NightVisionKey = k;
					} else {
						// Unassign any existing binding that uses the same key
						const int keyFieldCount = static_cast<int>(sizeof(g_Options.KeyMap) / sizeof(int32_t));
						for (int t = 0; t < keyFieldCount; t++)
							if (*((uint32_t*)(&g_Options.KeyMap) + t) == k)
								*((uint32_t*)(&g_Options.KeyMap) + t) = 0;

						*(reinterpret_cast<int*>((&g_Options.KeyMap)) + g_WaitKey) = k;
					}

#ifdef _DEBUG
					std::cout << "AcceptNewKey() : " << (static_cast<int>(k)) << " MapVK: " << (MapVKKey(k)) << std::endl;
#endif //_DEBUG

					WaitForMouseRelease();
					g_WaitKey = -1;
					return;
				}
			}
		}
	}
}


void ChangeMenuState(int32_t ms)
{
	// NOTE: the ambient bed keeps playing across submenus (hunt, options,
	// briefing...). It is silenced only around external processes — hunts
	// and the trophy room — by LaunchProcess, which also resumes it.
	g_PrevMenuState = g_MenuState;
	g_MenuState = ms;
	g_LastHoverId = -1;
	LoadGameMenu(g_MenuState);
}


// Centralized menu-exit path. Saves the current profile (and options,
// since TrophySave writes the full 1660-byte trophy block) before
// requesting shutdown, so unsaved settings in the menu do not get lost
// on quit. Callers must use this instead of calling PostQuitMessage
// directly, otherwise the save is skipped.
void RequestMenuExit(int exitCode)
{
	TrophySave(g_UserProfile);
	PostQuitMessage(exitCode);
}


int GetTextW(HDC hdc, const std::string& s)
{
	SIZE sz;
	GetTextExtentPoint(hdc, s.c_str(), (int)s.length(), &sz);
	return sz.cx;
}


int GetTextH(HDC hdc, const std::string& s)
{
	SIZE sz;
	GetTextExtentPoint(hdc, s.c_str(), (int)s.length(), &sz);
	return sz.cy;
}


/*
Map the virtual-key-code to a scan-code
*/
int MapVKKey(int k)
{
	if (k == VK_LBUTTON) return 124;
	if (k == VK_RBUTTON) return 125;
	if (k == VK_MBUTTON) return 126;
	if (k == VK_XBUTTON1) return 128;
	if (k == VK_XBUTTON2) return 129;
	return MapVirtualKey(k, MAPVK_VK_TO_VSC);
}


void MenuSet::AddItem(const std::string& txt)
{
	this->Item.push_back(std::make_pair(txt, false));
	this->Count = this->Item.size();
}


/*
Determine the amount of points that the current license selection
in the hunt screen will cost.
*/
int32_t CalculateDebit()
{
	int32_t debit = 0;

	if (MenuHunt[0].Selected >= 0 && MenuHunt[0].Selected < (int)g_AreaInfo.size())
	{
		debit += g_AreaInfo[MenuHunt[0].Selected].m_Price;
	}

	for (unsigned i = 0; i < MenuHunt[1].Item.size() && i < g_DinoList.size(); i++)
	{
		if (MenuHunt[1].Item[i].second)
		{
			debit += g_DinoInfo[g_DinoList[i]].m_Price;
		}
	}

	for (unsigned i = 0; i < MenuHunt[2].Item.size(); i++)
	{
		if (MenuHunt[2].Item[i].second)
		{
			debit += g_WeapInfo[i].m_Price;
		}
	}

	// Accessories settle their *scoring* at hunt end (smod= multipliers),
	// but their license price debits upfront like everything else — and
	// the equipment toggle gates on affordability for exactly that reason.
	for (unsigned i = 0; i < MenuHunt[3].Item.size() && i < g_UtilInfo.size(); i++)
	{
		if (MenuHunt[3].Item[i].second)
		{
			debit += g_UtilInfo[i].m_Price;
		}
	}

	return debit;
}


/*
Currently unused
*/
void CALLBACK WaveOutProc(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{

}


/*
Currently unused
*/
void AudioSoftThread()
{
	HWAVEOUT hwo;
	WAVEOUTCAPS woc;
	WAVEFORMATEX wfx;
	unsigned device = WAVE_MAPPER;

	wfx.cbSize = 0;
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 1;
	wfx.nSamplesPerSec = 22050;
	wfx.nBlockAlign = 2;
	wfx.wBitsPerSample = 16;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	std::cout << "Audio: Checking " << waveInGetNumDevs() << " devices for wave out support..." << std::endl;
	for (unsigned i = 0; i < waveInGetNumDevs(); i++)
		if (waveOutOpen(&hwo, i, &wfx, 0, 0, CALLBACK_EVENT | WAVE_FORMAT_QUERY) == MMSYSERR_NOERROR)
		{
			device = i;

			std::cout << "WaveOut Device ID: " << device << std::endl;
			std::cout << std::setw(20) << std::setfill('=') << "=" << std::endl;

			waveOutGetDevCaps(device, &woc, sizeof(WAVEOUTCAPS));
			std::cout << "Manufacturer ID: " << woc.wMid << std::endl;
			std::cout << "Product ID: " << woc.wPid << std::endl;
			std::cout << "Driver Version: " << (HIWORD(woc.vDriverVersion)) << "." << (LOWORD(woc.vDriverVersion)) << std::endl;
			std::cout << "Product Name: " << woc.szPname << std::endl;
			std::cout << "Channels: " << woc.wChannels << std::endl;

			std::cout << "Supported formats:" << std::endl;
			if (woc.dwFormats & WAVE_FORMAT_1M08)
				std::cout << "\t22.05 kHz Mono 8-bit" << std::endl;
			if (woc.dwFormats & WAVE_FORMAT_2M16)
				std::cout << "\t22.05 kHz Mono 16-bit" << std::endl;
			if (woc.dwFormats & WAVE_FORMAT_2S08)
				std::cout << "\t22.05 kHz Stereo 8-bit" << std::endl;
			if (woc.dwFormats & WAVE_FORMAT_2S16)
				std::cout << "\t22.05 kHz Stereo 16-bit" << std::endl;
			if (woc.dwFormats & WAVE_FORMAT_4M08)
				std::cout << "\t44.1 kHz Mono 8-bit" << std::endl;
			if (woc.dwFormats & WAVE_FORMAT_4M16)
				std::cout << "\t44.1 kHz Mono 16-bit" << std::endl;
			if (woc.dwFormats & WAVE_FORMAT_4S08)
				std::cout << "\t44.1 kHz Stereo 8-bit" << std::endl;
			if (woc.dwFormats & WAVE_FORMAT_4S16)
				std::cout << "\t44.1 kHz Stereo 16-bit" << std::endl;

			std::cout << "Supported functionality:" << std::endl;
			if (woc.dwSupport & WAVECAPS_LRVOLUME)
				std::cout << "\tSeparate left and right volume control." << std::endl;
			if (woc.dwSupport & WAVECAPS_PITCH)
				std::cout << "\tPitch control" << std::endl;
			if (woc.dwSupport & WAVECAPS_PLAYBACKRATE)
				std::cout << "\tPlayback rate control" << std::endl;
			if (woc.dwSupport & WAVECAPS_SYNC)
				std::cout << "\tDriver is synchronous" << std::endl;
			if (woc.dwSupport & WAVECAPS_VOLUME)
				std::cout << "\tVolume control" << std::endl;
			if (woc.dwSupport & WAVECAPS_SAMPLEACCURATE)
				std::cout << "\tSample-accurate position" << std::endl;

			break;
		}

	if (waveOutOpen(&hwo, device, &wfx, 0, reinterpret_cast<DWORD_PTR>(&WaveOutProc), CALLBACK_FUNCTION | WAVE_FORMAT_QUERY) != MMSYSERR_NOERROR)
	{
		std::cout << "Audio: Failed to open WaveOut device!" << std::endl;
		return;
	}

	waveOutClose(hwo);
}


/*
Initialise all the required settings and interface elements
*/
void InitInterface()
{
	std::cout << "Interface: Creating GDI Font handles..." << std::endl;
	fnt_Big = CreateFont(
		23, 0, 0, 0,
		600, 0, 0, 0,
		ANSI_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, nullptr);

	g_FontOptions = CreateFont(
		21, 0, 0, 0,
		500, 0, 0, 0,
		ANSI_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, nullptr);

	fnt_Small = CreateFont(
		14, 0, 0, 0,
		100, 0, 0, 0,
		ANSI_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, nullptr);

	fnt_Midd = CreateFont(
		16, 0, 0, 0,
		550, 0, 0, 0,
		ANSI_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, nullptr);

	if (!fnt_Small)
		std::cout << "Interface : Failed to create Small Font" << std::endl;
	if (!fnt_Midd)
		std::cout << "Interface : Failed to create Medium Font" << std::endl;
	if (!g_FontOptions)
		std::cout << "Interface : Failed to create Options Font" << std::endl;
	if (!fnt_Big)
		std::cout << "Interface : Failed to create Large Font" << std::endl;

	hdcCMain = CreateCompatibleDC(hdcMain);

	if (hdcCMain == nullptr)
	{
		throw std::runtime_error("Interface: Failed to create CompatibleDC!");
		return;
	}

	BITMAPINFOHEADER bmih;
	bmih.biSize = sizeof(BITMAPINFOHEADER);
	bmih.biWidth = 800;
	bmih.biHeight = -600;
	bmih.biPlanes = 1;
	bmih.biBitCount = 16;
	bmih.biCompression = BI_RGB;
	bmih.biSizeImage = 0;
	bmih.biXPelsPerMeter = 400;
	bmih.biYPelsPerMeter = 400;
	bmih.biClrUsed = 0;
	bmih.biClrImportant = 0;

	BITMAPINFO binfo;
	binfo.bmiHeader = bmih;
	bmpMain = CreateDIBSection(hdcMain, &binfo, DIB_RGB_COLORS, &lpVideoBuf, nullptr, 0);

	if (!bmpMain) {
		throw std::runtime_error("Interface: Failed to create DIB Section!");
		return;
	}

	g_PrevMenuState = -1;
	g_MenuState = 0;
	g_TypingBuffer = "";
	LoadGameMenu(0);

	g_ProfileIndex = 0;
	g_HuntSelectPic = nullptr;

	/************************************************************
	* Register/Profile lists
	*/
	MenuRegistry.x0 = 360;
	MenuRegistry.y0 = 363;
	//MenuEventStart(MENU_REGISTER) contains the AddMenuItem() calls
	MenuRegistry.Rect = { 360, 363, 499, 491 }; // Enough room for 9.14 items with fnt_Small

	/************************************************************
	* Options lists
	*/
	int m = OPT_GAME;
	MenuOptions[m].x0 = OptionsLayout::PANEL_GAME.x;
	MenuOptions[m].y0 = OptionsLayout::PANEL_GAME.y;
	MenuOptions[m].Padding = 15;
	MenuOptions[m].Count = 0;
	MenuOptions[m].AddItem("Agressivity");
	MenuOptions[m].AddItem("Density");
	MenuOptions[m].AddItem("Sensitivity");
	MenuOptions[m].AddItem("View range");
	MenuOptions[m].AddItem("Object detail");
	MenuOptions[m].AddItem("Measurement");
	MenuOptions[m].AddItem("Sound API");
	MenuOptions[m].AddItem("FPS limit");
	MenuOptions[m].Rect = { OptionsLayout::PANEL_GAME.x, OptionsLayout::PANEL_GAME.y, OptionsLayout::PANEL_GAME.GetRight(), OptionsLayout::PANEL_GAME.GetBottom() };

	m = OPT_KEYBINDINGS;
	MenuOptions[m].x0 = OptionsLayout::PANEL_CONTROLS.x;
	MenuOptions[m].y0 = OptionsLayout::PANEL_CONTROLS.y;
	MenuOptions[m].Padding = 15;
	MenuOptions[m].Count = 0;
	MenuOptions[m].AddItem("Forward");
	MenuOptions[m].AddItem("Backward");
	MenuOptions[m].AddItem("Reload");
	MenuOptions[m].AddItem("Resupply");
	MenuOptions[m].AddItem("Hold Breath");
	MenuOptions[m].AddItem("Firing Mode");
	MenuOptions[m].AddItem("Fire");
	MenuOptions[m].AddItem("Get weapon");
	MenuOptions[m].AddItem("Step Left");
	MenuOptions[m].AddItem("Step Right");
	MenuOptions[m].AddItem("Rack");
	MenuOptions[m].AddItem("Jump");
	MenuOptions[m].AddItem("Run");
	MenuOptions[m].AddItem("Crouch");
	MenuOptions[m].AddItem("Lure Call");
	MenuOptions[m].AddItem("Change Call");
	MenuOptions[m].AddItem("Binoculars");
	MenuOptions[m].AddItem("Night vision");
#ifdef _iceage
	MenuOptions[m].AddItem("Call Resupply");
#endif //_iceage
	MenuOptions[m].AddItem("Reverse mouse");
	MenuOptions[m].AddItem("Mouse sensitivity");
	MenuOptions[m].Rect = { OptionsLayout::PANEL_CONTROLS.x, OptionsLayout::PANEL_CONTROLS.y, OptionsLayout::PANEL_CONTROLS.GetRight(), OptionsLayout::PANEL_CONTROLS.GetBottom() };

	m = OPT_VIDEO;
	MenuOptions[m].x0 = OptionsLayout::PANEL_VIDEO.x;
	MenuOptions[m].y0 = OptionsLayout::PANEL_VIDEO.y;
	MenuOptions[m].Padding = 15;
	MenuOptions[m].Count = 0;
	MenuOptions[m].AddItem("Video Driver");
	MenuOptions[m].AddItem("Resolution");
	MenuOptions[m].AddItem("Display Mode");
	MenuOptions[m].AddItem("3D Shadows");
	MenuOptions[m].AddItem("Fog");
	MenuOptions[m].AddItem("Textures");
	MenuOptions[m].AddItem("Alpha Source");
	MenuOptions[m].AddItem("Brightness");
	MenuOptions[m].AddItem("Field of View");
	MenuOptions[m].Rect = { OptionsLayout::PANEL_VIDEO.x, OptionsLayout::PANEL_VIDEO.y, OptionsLayout::PANEL_VIDEO.GetRight(), OptionsLayout::PANEL_VIDEO.GetBottom() };

	/************************************************************
	* Initialize Layout Columns
	* Fixed StartLegacy-style rows keep labels/values inside the artwork panels.
	*/
	g_GameLabels = OptionsLayout::MakeGameLabels(static_cast<int>(MenuOptions[OPT_GAME].Item.size()));
	g_GameValues = OptionsLayout::MakeGameValues(static_cast<int>(MenuOptions[OPT_GAME].Item.size()));

	g_ControlsLabels = OptionsLayout::MakeControlsLabels(static_cast<int>(MenuOptions[OPT_KEYBINDINGS].Item.size()));
	g_ControlsValues = OptionsLayout::MakeControlsValues(static_cast<int>(MenuOptions[OPT_KEYBINDINGS].Item.size()));

	g_VideoLabels = OptionsLayout::MakeVideoLabels(static_cast<int>(MenuOptions[OPT_VIDEO].Item.size()));
	g_VideoValues = OptionsLayout::MakeVideoValues(static_cast<int>(MenuOptions[OPT_VIDEO].Item.size()));

	/************************************************************
	* Hunt lists
	*/
	m = 0; // Areas
	MenuHunt[m].x0 = 10;
	MenuHunt[m].y0 = 382;
	MenuHunt[m].Padding = 4;
	MenuHunt[m].Count = 0;
	MenuHunt[m].Offset = 0;
	// ...
	MenuHunt[m].Rect = { 10, 382, 190, 542 };

	m = 1; // Dinosaurs
	MenuHunt[m].x0 = 10;
	MenuHunt[m].y0 = 382;
	MenuHunt[m].Padding = 4;
	MenuHunt[m].Count = 0;
	MenuHunt[m].Offset = 0;
	// ...
	MenuHunt[m].Rect = { 210, 382, 390, 542 };

	m = 2; // Weapons
	MenuHunt[m].x0 = 210;
	MenuHunt[m].y0 = 382;
	MenuHunt[m].Padding = 4;
	MenuHunt[m].Count = 0;
	MenuHunt[m].Offset = 0;
	// ...
	MenuHunt[m].Rect = { 410, 382, 590, 542 };

	m = 3; // Accessories / Utilities
	MenuHunt[m].x0 = 10;
	MenuHunt[m].y0 = 382;
	MenuHunt[m].Padding = 4;
	MenuHunt[m].Count = 0;
	MenuHunt[m].Offset = 0;
	MenuHunt[m].AddItem("Camouflage");
	MenuHunt[m].AddItem("Radar");
	MenuHunt[m].AddItem("Cover scent");
	MenuHunt[m].AddItem("Double ammo");
#ifdef _iceage
	MenuHunt[m].AddItem("Supply drop");
#endif //_iceage
	MenuHunt[m].AddItem("Night vision");
	MenuHunt[m].AddItem("Tranquilizers");
#ifndef _iceage
	// Keep parallel with g_UtilInfo (camo/radar/scent/double/NV/tranq).
	assert(MenuHunt[m].Item.size() == (size_t)kAccCount);
#endif
	MenuHunt[m].Rect = { 610, 382, 790, 542 };

	std::cout << "Interface: Initialisation Ok!" << std::endl;

#ifdef _DEBUG
	std::thread audio_thread(AudioSoftThread);
	audio_thread.join();
#endif // _DEBUG
}


void ShutdownInterface()
{
	if (bmpMain)
		DeleteObject(reinterpret_cast<HBITMAP>(bmpMain));
	if (hdcCMain)
		DeleteDC(hdcCMain);

	if (fnt_Small)
		DeleteObject(fnt_Small);
	if (fnt_Midd)
		DeleteObject(fnt_Midd);
	if (fnt_Big)
		DeleteObject(fnt_Big);
	if (g_FontOptions)
		DeleteObject(g_FontOptions);

	std::cout << "Interface: Shutdown Ok!" << std::endl;
}


void _Line(HDC hdc, int x1, int y1, int x2, int y2)
{
	MoveToEx(hdc, x1, y1, nullptr);
	LineTo(hdc, x2, y2);
}


void DrawProgressBar(int x, int y, float l)
{
	int W = 120; y += 13;

	HPEN wp = CreatePen(PS_SOLID, 0, 0x009F9F9F);
	HBRUSH wb = CreateSolidBrush(0x003FAF3F);

	HPEN oldpen = (HPEN)SelectObject(hdcCMain, GetStockObject(BLACK_PEN));
	HBRUSH oldbrs = (HBRUSH)SelectObject(hdcCMain, GetStockObject(BLACK_BRUSH));


	x += 1; y += 1;
	_Line(hdcCMain, x, y - 9, x + W + 1, y - 9);
	_Line(hdcCMain, x, y, x + W + 1, y);
	_Line(hdcCMain, x, y - 8, x, y);
	_Line(hdcCMain, x + W, y - 8, x + W, y);
	_Line(hdcCMain, x + W / 2, y - 8, x + W / 2, y);
	_Line(hdcCMain, x + W / 4, y - 8, x + W / 4, y);
	_Line(hdcCMain, x + W * 3 / 4, y - 8, x + W * 3 / 4, y);

	x -= 1; y -= 1;
	SelectObject(hdcCMain, wp);
	_Line(hdcCMain, x, y - 9, x + W + 1, y - 9);
	_Line(hdcCMain, x, y, x + W + 1, y);
	_Line(hdcCMain, x, y - 8, x, y);
	_Line(hdcCMain, x + W, y - 8, x + W, y);
	_Line(hdcCMain, x + W / 2, y - 8, x + W / 2, y);
	_Line(hdcCMain, x + W / 4, y - 8, x + W / 4, y);
	_Line(hdcCMain, x + W * 3 / 4, y - 8, x + W * 3 / 4, y);

	W -= 2;
	PatBlt(hdcCMain, x + 2, y - 5, static_cast<int>((W * l / 2.f)), 4, PATCOPY);

	SelectObject(hdcCMain, wb);
	PatBlt(hdcCMain, x + 1, y - 6, static_cast<int>((W * l / 2.f)), 4, PATCOPY);


	SelectObject(hdcCMain, oldpen);
	SelectObject(hdcCMain, oldbrs);
	DeleteObject(wp);
	DeleteObject(wb);
}


void DrawRectangle(int x, int y, int w, int h, Color16 c)
{
	Color16* back_buffer = static_cast<Color16*>(lpVideoBuf);
	for (int i = y; i < y + h; i++)
		for (int j = x; j < x + w; j++)
		{
			back_buffer[(j)+(i * 800)] = Color16(c, 1);
			//*((uint16_t*)lpVideoBuf + (j)+(i) * 800) = c | 0x8000;
		}
}


void DrawSliderBar(int x, int y, int w, float v, int slider_rgb = RGB(239, 228, 176))
{
	if (v < 0.0f)
		v = 0.0f;
	if (v > 1.0f)
		v = 1.0f;

	int xs = static_cast<int>((x + ((w - 2) * v)));

	//HPEN wp = CreatePen(PS_SOLID, 0, 0x009F9F9F);

	HBRUSH wb = CreateSolidBrush(slider_rgb);// RGB(239, 228, 176));

	HPEN oldpen = (HPEN)SelectObject(hdcCMain, GetStockObject(BLACK_PEN));
	HBRUSH oldbrs = (HBRUSH)SelectObject(hdcCMain, GetStockObject(BLACK_BRUSH));

	// Draw track
	_Line(hdcCMain, x, y - 4, x + w + 1, y - 4);
	_Line(hdcCMain, x, y - 3, x + w + 1, y - 3);
	_Line(hdcCMain, x, y - 2, x + w + 1, y - 2);
	_Line(hdcCMain, x, y - 1, x + w + 1, y - 1);
	_Line(hdcCMain, x, y + 0, x + w + 1, y + 0);
	_Line(hdcCMain, x, y + 1, x + w + 1, y + 1);
	_Line(hdcCMain, x, y + 2, x + w + 1, y + 2);
	_Line(hdcCMain, x, y + 3, x + w + 1, y + 3);

	SelectObject(hdcCMain, wb);
	PatBlt(hdcCMain, xs, y - 4, 6, 8, PATCOPY);

	SelectObject(hdcCMain, oldpen);
	SelectObject(hdcCMain, oldbrs);
	//DeleteObject(wp);
	DeleteObject(wb);
}


int MenuViewOptFromSlider(float v)
{
	int opt = kViewOptMin + static_cast<int>((v * static_cast<float>(kViewOptMax - kViewOptMin)));
	if (opt < kViewOptMin) opt = kViewOptMin;
	if (opt > kViewOptMax) opt = kViewOptMax;
	return opt;
}


int MenuObjectDetailFromSlider(float v)
{
	return DiscreteSliderValue(v, kObjectDetailMin, kObjectDetailMax, kObjectDetailStep);
}


void DrawPicture(int x, int y, Picture& pic)
{
	if (pic.m_Data == nullptr || pic.m_Width == 0 || pic.m_Height == 0)
		return;

	/*for (int y = 0; y < 600; y++) {
		memcpy((uint16_t*)lpVideoBuf + (y * 800), &menu.m_Image[(600 - y - 1) * 800], 800 * 2);
	}*/

	for (auto i = 0U; i < pic.m_Height; i++) {
		memcpy((uint16_t*)lpVideoBuf + x + (y + i) * 800U, pic.m_Data + (pic.m_Height - i - 1) * pic.m_Width, pic.m_Width * 2);
	}
}


void DrawPicture(int x, int y, int w, int h, uint16_t* lpImage)
{
	for (int i = 0; i < h; i++)
		memcpy((uint16_t*)lpVideoBuf + x + (y + i) * 800, lpImage + (h - i - 1) * w, w * 2);
}


/*
Draw the menu background and overlay the 'on' layer
*/
void DrawMenuBg(MenuItem& menu)
{
	POINT& p = g_CursorPos;
	uint8_t cursor_id = 0;

	cursor_id = menu.GetID((p.x / 2), (p.y / 2));

	if (g_LastHoverId == -1) {
		g_LastHoverId = cursor_id;
	}
	else if (cursor_id != g_LastHoverId) {
		if (cursor_id != 0) {
			MenuAudioPlayHover();
		}
		g_LastHoverId = cursor_id;
	}

	// OLD: Render background image
	/*for (int y = 0; y < 600; y++) {
		memcpy((uint16_t*)lpVideoBuf + (y * 800), &menu.m_Image[(600 - y - 1) * 800], 800 * 2);
	}*/

	// Render the base background from the on/off states
	for (int yy = 0; yy < 300; yy++) {
		for (int xx = 0; xx < 400; xx++)
		{
			int x = xx * 2;
			int y = yy * 2;
			int on = false;
			uint8_t id2 = menu.GetID((xx), (yy));

			if (cursor_id == id2) on |= true;

			if (g_MenuState == MENU_HUNT && (id2 >= 1 && id2 <= 6)) {
				on |= (int)g_MenuItem.GetIsElementSet(id2);
			}
			else if (g_MenuState == MENU_OPTIONS && (id2 >= 1 && id2 <= 3)) {
				on |= (int)g_MenuItem.GetIsElementSet(id2);
			}

			if (id2 == 0) on = false;

			if (on) {
				*((uint16_t*)lpVideoBuf + ((x + 0L) + (y + 0L) * 800L)) = menu.m_Image_On[(x + 0) + (600 - (y + 0) - 1) * 800];
				*((uint16_t*)lpVideoBuf + ((x + 1L) + (y + 0L) * 800L)) = menu.m_Image_On[(x + 1) + (600 - (y + 0) - 1) * 800];
				*((uint16_t*)lpVideoBuf + ((x + 0L) + (y + 1L) * 800L)) = menu.m_Image_On[(x + 0) + (600 - (y + 1) - 1) * 800];
				*((uint16_t*)lpVideoBuf + ((x + 1L) + (y + 1L) * 800L)) = menu.m_Image_On[(x + 1) + (600 - (y + 1) - 1) * 800];
			}
			else {
				*((uint16_t*)lpVideoBuf + ((x + 0L) + (y + 0L) * 800L)) = menu.m_Image[(x + 0) + (600 - (y + 0) - 1) * 800];
				*((uint16_t*)lpVideoBuf + ((x + 1L) + (y + 0L) * 800L)) = menu.m_Image[(x + 1) + (600 - (y + 0) - 1) * 800];
				*((uint16_t*)lpVideoBuf + ((x + 0L) + (y + 1L) * 800L)) = menu.m_Image[(x + 0) + (600 - (y + 1) - 1) * 800];
				*((uint16_t*)lpVideoBuf + ((x + 1L) + (y + 1L) * 800L)) = menu.m_Image[(x + 1) + (600 - (y + 1) - 1) * 800];
			}
		}
	}
}


void DrawTextColor(int x, int y, const std::string& text, uint32_t color, int align = DTA_LEFT)
{
	if (align == DTA_RIGHT)
	{
		x -= GetTextW(hdcCMain, text);
	}

	SetBkMode(hdcCMain, TRANSPARENT);
	SetTextColor(hdcCMain, color);
	TextOut(hdcCMain, x, y, text.c_str(), (int)text.size());
}


void DrawTextShadow(int x, int y, const std::string& text, uint32_t color, int align = DTA_LEFT)
{
	if (align == DTA_RIGHT)
	{
		x -= GetTextW(hdcCMain, text);
	}

	DrawTextColor(x + 1, y + 1, text, RGB(0, 0, 0));
	DrawTextColor(x, y, text, color);
}



void DrawURLShadow(int x, int y, const std::string& text, uint32_t color, int align = DTA_LEFT)
{
	int W = GetTextW(hdcCMain, text);
	int H = GetTextH(hdcCMain, text);

	if (align == DTA_RIGHT)
	{
		x -= W;
	}

	RECT rc = { x, y, x + W, y + H };

	if (IsPointInRect(g_CursorPos, rc))
	{
		color = RGB(244, 10, 10);
	}

	HPEN wp = CreatePen(PS_SOLID, 0, color);
	HPEN oldpen = (HPEN)SelectObject(hdcCMain, GetStockObject(BLACK_PEN));

	DrawTextColor(x + 1, y + 1, text, 0x000000);
	_Line(hdcCMain, x + 1, y + H + 1, x + W + 1, y + H + 1);

	SelectObject(hdcCMain, wp);

	DrawTextColor(x, y, text, color);
	_Line(hdcCMain, x, y + H, x + W, y + H);

	SelectObject(hdcCMain, oldpen);
}


void InterfaceSetFont(HFONT font)
{
	if (font == nullptr) {
		SelectObject(hdcCMain, hfntOld);
	}
	else {
		hfntOld = reinterpret_cast<HFONT>(SelectObject(hdcCMain, font));
	}
}


void InterfaceClear(WORD Color)
{
	memset(lpVideoBuf, 0, (800 * 2) * 600);
	hbmpOld = reinterpret_cast<HBITMAP>(SelectObject(hdcCMain, bmpMain));
	hfntOld = reinterpret_cast<HFONT>(SelectObject(hdcCMain, fnt_Small));
}


void InterfaceBlt()
{
	SetStretchBltMode(hdcMain, HALFTONE);
	StretchBlt(hdcMain, 0, 0, g_ClientWidth, g_ClientHeight,
	           hdcCMain, 0, 0, MENU_BASE_WIDTH, MENU_BASE_HEIGHT, SRCCOPY);
	SelectObject(hdcCMain, hfntOld);
	SelectObject(hdcCMain, hbmpOld);
}


/*
Menu screen initialization event
NOTE: This only gets called on the first tick that the menu exists, it can be used to
initialize variables and reset various things.
*/
// Capture the current hunt setup for config.cfg so the next visit to the
// hunt screen reselects it instead of resetting to defaults. Guarded: the
// MenuHunt lists only exist after the first MENU_HUNT entry.
static void SaveHuntSelections()
{
	if (MenuHunt[0].Item.empty()) return;
	g_SavedHuntArea.clear();
	if (MenuHunt[0].Selected >= 0 && MenuHunt[0].Selected < (int)g_AreaInfo.size())
		g_SavedHuntArea = g_AreaInfo[MenuHunt[0].Selected].m_ProjectName;
	auto mask = [](const MenuSet& set) {
		unsigned long long m = 0;
		for (size_t i = 0; i < set.Item.size() && i < 64; i++)
			if (set.Item[i].second) m |= 1ULL << i;
		return m;
	};
	g_SavedHuntDinos = mask(MenuHunt[1]);
	g_SavedHuntWeapons = mask(MenuHunt[2]);
	g_SavedHuntUtils = mask(MenuHunt[3]);
	g_SavedHuntTime = g_TimeOfDay;
	g_HasSavedHunt = true;
}
// Reapply the persisted setup after the entry defaults. Everything is
// validated against the current data, so mod list changes can only fall
// back to defaults, never select out of range. Saved at launch, so a
// restored setup always corresponds to a launchable one.
static void RestoreHuntSelections()
{
	if (!g_HasSavedHunt) return;
	if (!g_SavedHuntArea.empty()) {
		for (size_t i = 0; i < g_AreaInfo.size(); i++)
			if (g_AreaInfo[i].m_ProjectName == g_SavedHuntArea) { MenuHunt[0].Selected = (int)i; break; }
	}
	auto apply = [](MenuSet& set, unsigned long long m) {
		for (size_t i = 0; i < set.Item.size() && i < 64; i++)
			set.Item[i].second = ((m >> i) & 1ULL) != 0;
	};
	apply(MenuHunt[1], g_SavedHuntDinos);
	apply(MenuHunt[2], g_SavedHuntWeapons);
	apply(MenuHunt[3], g_SavedHuntUtils);
	if (g_SavedHuntTime >= HUNT_DAWN && g_SavedHuntTime <= HUNT_NIGHT && g_SavedHuntTime != g_TimeOfDay) {
		g_MenuItem.SetIsElementSet(g_TimeOfDay + 1, false);
		g_TimeOfDay = g_SavedHuntTime;
		g_MenuItem.SetIsElementSet(g_TimeOfDay + 1, true);
	}
	if (g_UserProfile.Score - CalculateDebit() < 0) {
		// Saved setup is unaffordable here (e.g. restored onto a poorer
		// profile, or hand-edited masks) — fall back to the entry defaults
		// above instead of displaying a negative account. Time-of-day is
		// free, so it stays as restored.
		MenuHunt[0].Selected = g_AreaInfo.empty() ? -1 : 0;
		for (auto m = 1U; m <= 3U; m++) {
			for (auto& it : MenuHunt[m].Item) it.second = false;
			if (m < 3U && !MenuHunt[m].Item.empty()) MenuHunt[m].Item[0].second = true;
		}
		// Modded prices or a low-credit profile can make even the defaults
		// unaffordable. Clear the loadout and keep only an affordable area.
		// Existing launch guards require an area and, outside observer mode,
		// at least one creature and weapon before starting a hunt.
		if (CalculateDebit() > g_UserProfile.Score) {
			for (auto m = 1U; m <= 3U; m++)
				for (auto& it : MenuHunt[m].Item) it.second = false;
			if (CalculateDebit() > g_UserProfile.Score)
				MenuHunt[0].Selected = -1;
		}
	}
}
void MenuEventStart(int32_t menu_state)
{
	g_MenuItem.ResetElementSet();

	switch (menu_state) {
	case MENU_REGISTER: {
		MenuRegistry.Offset = 0;
		MenuRegistry.Count = 0;
		MenuRegistry.AddItem("");
		g_LastListClickTime = 0;
		g_LastListClickIndex = -1;
#ifdef _iceage
		char tname[128];
#endif
		for (auto i = 0U; i < 8U; i++) {
			g_Profiles[i].m_Name = "";
			g_Profiles[i].m_RegNumber = i;
			g_Profiles[i].m_Rank = RANK_BEGINNER;
			g_Profiles[i].m_Score = 0U;

			std::stringstream sn;
			sn << "trophy" << std::setfill('0') << std::setw(2) << i << ".sav";

#ifndef _iceage
			std::ifstream fs(sn.str(), std::ios::binary);
			std::array<std::uint8_t, LegacyProfile::HeaderSize> bytes{};
			LegacyProfile::Header header;
			if (!fs.read(reinterpret_cast<char*>(bytes.data()), bytes.size()) ||
			    !LegacyProfile::DecodeHeader(bytes.data(), bytes.size(), header)) continue;
			g_Profiles[i].m_RegNumber = header.registration;
			g_Profiles[i].m_Score = header.score;
			g_Profiles[i].m_Rank = header.rank;
			const auto end = std::find(header.name.begin(), header.name.end(), 0);
			g_Profiles[i].m_Name.assign(header.name.begin(), end);
#else
			std::ifstream fs(sn.str());
			if (!fs.is_open()) { continue; }

			fs.read(tname, 128);
			fs.read(reinterpret_cast<char*>(&g_Profiles[i].m_RegNumber), 4);
			fs.read(reinterpret_cast<char*>(&g_Profiles[i].m_Score), 4);
			fs.read(reinterpret_cast<char*>(&g_Profiles[i].m_Rank), 4);

			g_Profiles[i].m_Name = tname;
#endif
		}
	} break;
	case MENU_OPTIONS: {
		g_WaitKey = -1;

		for (auto m = 0U; m < OPT_MAX; m++) {
			MenuOptions[m].Hilite = -1;
			MenuOptions[m].Selected = -1;
		}
	} break;
	case MENU_MAIN: {
		MenuAudioStartAmbient();
	} break;
		/****************************************************
		Hunt License Menu */
	case MENU_HUNT:
	{
		if (g_TimeOfDay < HUNT_DAWN || g_TimeOfDay > HUNT_NIGHT)
			g_TimeOfDay = HUNT_DAY;

		g_HuntInfo.first = -1;
		g_HuntInfo.second = 0;

		for (auto i = 1U; i <= 3U; i++)
			g_MenuItem.SetIsElementSet(i, false);

		g_MenuItem.SetIsElementSet(g_TimeOfDay + 1, true);

		g_MenuItem.SetIsElementSet(4, false);
		g_MenuItem.SetIsElementSet(5, g_Options.TranqMode);
		g_MenuItem.SetIsElementSet(6, g_ObserverMode);

		for (auto i = 0U; i < 4U; i++)
		{
			MenuHunt[i].Offset = 0;
		}

		MenuHunt[0].Item.clear();
		for (auto i = 0U; i < g_AreaInfo.size(); i++)
		{
			MenuHunt[0].Item.push_back(std::make_pair(g_AreaInfo[i].m_Name, false));
		}
		MenuHunt[0].Count = g_AreaInfo.size();

		g_DinoList.clear();
		MenuHunt[1].Item.clear();
		for (auto i = 0U; i < g_DinoInfo.size(); i++)
		{
			DinoInfo& dino = g_DinoInfo.at(i);

			if (dino.m_AI >= 10)//&& dino.m_Rank <= g_UserProfile.Rank)
			{
				// Add to a list
				g_DinoList.push_back(i);
				MenuHunt[1].Item.push_back(std::make_pair(dino.m_Name, false));

				std::stringstream spp;

				// Load appropriate text

				spp << "huntdat/menu/txt/dino" << (dino.m_AI - 9);
				if (g_Options.OptSys)
					spp << ".txu";
				else
					spp << ".txm";
				g_DinoInfo[i].m_Description.clear();
				LoadText(g_DinoInfo[i].m_Description, spp.str());
			}
		}
		MenuHunt[1].Count = g_DinoList.size();

		MenuHunt[2].Item.clear();
		for (auto i = 0U; i < g_WeapInfo.size(); i++)
		{
			MenuHunt[2].Item.push_back(std::make_pair(g_WeapInfo[i].m_Name, false));
			// Load appropriate text
			std::stringstream spp;
			spp << "huntdat/menu/txt/weapon" << (i + 1) << ".txt";
			g_WeapInfo[i].m_Description.clear();
			LoadText(g_WeapInfo[i].m_Description, spp.str());
		}
		MenuHunt[2].Count = g_WeapInfo.size();

		/* TODO: In future versions of the menu, support new types of accessories/utilities
		In the _RES they can have a string that defines the command line toggle to send to
		the .REN executable, such as '-camo' for camoflauge, or whatever custom accessory
		the modded .REN allows for. */

		// Reset the states
		for (auto m = 0U; m < 4U; m++)
		{
			MenuHunt[m].Selected = -1;
			for (auto i = MenuHunt[m].Item.begin(); i != MenuHunt[m].Item.end(); i++)
			{
				i->second = false;
			}
		}

		if (!g_AreaInfo.empty())
		{
			MenuHunt[0].Selected = 0;
		}

		for (auto i = 0U; i < 3U; i++)
		{
			if (!MenuHunt[i].Item.empty())
			{
				MenuHunt[i].Item[0].second = true;
			}
		}

		RestoreHuntSelections();
		g_ScoreDebit = CalculateDebit();
	} break;
	}
}


void LoadGameMenu(int32_t menu)
{
	std::string	mf_off = "";
	std::string	mf_on = "";
	std::string	mf_map = "";

	switch (menu) {
	case MENU_REGISTER:
	{
		mf_off = "HUNTDAT/MENU/MENUR.TGA";
		mf_on = "HUNTDAT/MENU/MENUR_ON.TGA";
		mf_map = "HUNTDAT/MENU/MR_MAP.RAW";
	} break;
	case MENU_REGISTRY_DELETE:
	{
		mf_off = "HUNTDAT/MENU/MENUD.TGA";
		mf_on = "HUNTDAT/MENU/MENUD_ON.TGA";
		mf_map = "HUNTDAT/MENU/MD_MAP.RAW";
	} break;
	case MENU_REGISTRY_WAIVER:
	{
		mf_off = "HUNTDAT/MENU/MENUL.TGA";
		mf_on = "HUNTDAT/MENU/MENUL_ON.TGA";
		mf_map = "HUNTDAT/MENU/ML_MAP.RAW";
	} break;
	case MENU_MAIN:
	{
		mf_off = "HUNTDAT/MENU/MENUM.TGA";
		mf_on = "HUNTDAT/MENU/MENUM_ON.TGA";
		mf_map = "HUNTDAT/MENU/MAIN_MAP.RAW";
	} break;
	case MENU_HUNT:
	{
		mf_off = "HUNTDAT/MENU/MENU2.TGA";
		mf_on = "HUNTDAT/MENU/MENU2_ON.TGA";
		mf_map = "HUNTDAT/MENU/M2_MAP.RAW";
	} break;
	case MENU_OPTIONS:
	{
		mf_off = "HUNTDAT/MENU/OPT_OFF.TGA";
		mf_on = "HUNTDAT//MENU/OPT_ON.TGA";
		mf_map = "HUNTDAT/MENU/OPT_MAP.RAW";
	} break;
	case MENU_CREDITS:
	{
		mf_off = "HUNTDAT/MENU/CREDITS.TGA";
		mf_on = "";
		mf_map = "";
	} break;
	case MENU_STATISTICS:
	{
		mf_off = "HUNTDAT/MENU/MENUS.TGA";
		mf_on = "";
		mf_map = "";
	} break;
	case MENU_QUIT:
	{
		mf_off = "HUNTDAT/MENU/MENUQ.TGA";
		mf_on = "HUNTDAT/MENU/MENUQ_ON.TGA";
		mf_map = "HUNTDAT/MENU/MQ_MAP.RAW";
	} break;
	default:
	{
		throw std::invalid_argument("Invalid argument: unsigned int menu.\nExpected a member of MenuStateEnum!");
	} break;
	}

	if (!LoadMenuBackground(g_MenuItem.m_Image, mf_off)) {
		throw std::runtime_error("Failed to open the file...");
		return;
	}

	if (!mf_on.empty()) {
		if (!LoadMenuBackground(g_MenuItem.m_Image_On, mf_on)) {
			throw std::runtime_error("Failed to open the file...");
			return;
		}
	}

	if (!mf_map.empty()) {
		std::ifstream fs(ResolveMenuAssetReadPath(mf_map), std::ios::binary);
		if (fs.is_open())
			fs.read(reinterpret_cast<char*>(g_MenuItem.m_Image_Map), 400 * 300);
	}
	else {
		memset(g_MenuItem.m_Image_Map, 0, 400 * 300);
	}
}


/*
Draw the profile details that appear at the top of the main menu,
such as Profile Name, Score, Rank.
*/
void DrawMenuProfile()
{
	std::stringstream ss;

	int c = RGB(239, 228, 176);

	InterfaceSetFont(fnt_Big);
	DrawTextShadow(90, 9, g_UserProfile.Name, c);

	ss << g_UserProfile.Score;
	DrawTextShadow(592, 9, ss.str().c_str(), c, DTA_RIGHT);

#ifdef _carnivores1
	switch (g_UserProfile.Rank)
	{
	case 0: DrawTextShadow(344, 9, "Novice", c); break;
	case 1: DrawTextShadow(344, 9, "Advanced", c); break;
	case 2: DrawTextShadow(344, 9, "Expert", c); break;
	}
#endif

	InterfaceSetFont(nullptr);
}


/*
Draw this profile's last hunt statistics and total hunt statistics, also calls DrawProfileMenu()
to draw the profile name and score/rank as an overlay.
*/
void DrawMenuStatistics()
{
	// X 602 - 792
	RECT rc = { 602, 70, 792, 300 };
	std::stringstream ss;
	int c = RGB(239, 228, 176);

	InterfaceSetFont(fnt_Midd);
	int  ttm = static_cast<int>(g_UserProfile.Total.time);
	int  ltm = static_cast<int>(g_UserProfile.Last.time);

	DrawTextShadow(rc.left + 4, 78, "Path travelled  ", c);

	if (g_Options.OptSys)
		ss << std::setprecision(4) << (g_UserProfile.Last.path / 0.3f) << " ft.";
	else
		ss << std::setprecision(4) << (g_UserProfile.Last.path) << " m.";

	DrawTextShadow(rc.right - 4, 78, ss.str(), c, DTA_RIGHT);
	ss.str(""); ss.clear();

	DrawTextShadow(rc.left + 4, 98, "Time hunted  ", c);
	ss << std::dec << (ltm / 3600) << ":"; // Hours
	ss << std::setfill('0') << std::setw(2) << ((ltm % 3600) / 60) << ":"; // Minutes
	ss << std::setfill('0') << std::setw(2) << (ltm % 60); // Seconds
	DrawTextShadow(rc.right - 4, 98, ss.str(), c, DTA_RIGHT);
	ss.str(""); ss.clear();

	DrawTextShadow(rc.left + 4, 118, "Shots made  ", c);
	ss << g_UserProfile.Last.smade;
	DrawTextShadow(rc.right - 4, 118, ss.str(), c, DTA_RIGHT);
	ss.str(""); ss.clear();

	int accuracy = 0;
	if (g_UserProfile.Last.success > 0)
		accuracy = ((g_UserProfile.Last.success / g_UserProfile.Last.smade) * 100);

	DrawTextShadow(rc.left + 4, 138, "Accuracy  ", c);
	ss << accuracy << "%";
	DrawTextShadow(rc.right - 4, 138, ss.str(), c, DTA_RIGHT);
	ss.str(""); ss.clear();

	/************************** TOTAL STATS **************************/

	DrawTextShadow(rc.left + 4, 208, "Path travelled  ", c);

	if (g_UserProfile.Total.path < 1000)
	{
		if (g_Options.OptSys) ss << std::setprecision(4) << (g_UserProfile.Total.path / 0.3f) << " ft.";
		else                  ss << std::setprecision(4) << (g_UserProfile.Total.path) << " m.";
	}
	else
	{
		if (g_Options.OptSys) ss << std::setprecision(4) << (g_UserProfile.Total.path / 1667.f) << " miles.";
		else                  ss << std::setprecision(4) << (g_UserProfile.Total.path / 1000.f) << " km.";
	}

	DrawTextShadow(rc.right - 4, 208, ss.str(), c, DTA_RIGHT);
	ss.str(""); ss.clear();

	DrawTextShadow(rc.left + 4, 228, "Time hunted  ", c);
	ss << std::dec << (ttm / 3600) << ":"; // Hours
	ss << std::setfill('0') << std::setw(2) << ((ttm % 3600) / 60) << ":"; // Minutes
	ss << std::setfill('0') << std::setw(2) << (ttm % 60); // Seconds
	DrawTextShadow(rc.right - 4, 228, ss.str(), c, DTA_RIGHT);
	ss.str(""); ss.clear();

	DrawTextShadow(rc.left + 4, 248, "Shots made  ", c);
	ss << g_UserProfile.Total.smade;
	DrawTextShadow(rc.right - 4, 248, ss.str(), c, DTA_RIGHT);
	ss.str(""); ss.clear();

	accuracy = 0;
	if (g_UserProfile.Total.success > 0 && g_UserProfile.Total.smade > 0)
		accuracy = static_cast<int>(static_cast<float>((static_cast<float>(g_UserProfile.Total.success) / static_cast<float>(g_UserProfile.Total.smade))) * 100.f);

	DrawTextShadow(rc.left + 4, 268, "Accuracy  ", c);
	ss << accuracy << "%";
	DrawTextShadow(rc.right - 4, 268, ss.str(), c, DTA_RIGHT);
	ss.str(""); ss.clear();

	DrawTextShadow(rc.left + 4, 288, "Rank:", c);

	switch (g_UserProfile.Rank)
	{
	case 0: DrawTextShadow(rc.right - 4, 288, "Novice", c, DTA_RIGHT); break;
	case 1: DrawTextShadow(rc.right - 4, 288, "Advanced", c, DTA_RIGHT); break;
	case 2: DrawTextShadow(rc.right - 4, 288, "Expert", c, DTA_RIGHT); break;
	default: DrawTextShadow(rc.right - 4, 288, "Eldritch", c, DTA_RIGHT); break; // Easter egg/invalid rank
	}

	DrawMenuProfile();
}


/*
*/
void DrawMenuCredits()
{
	uint32_t color = RGB(239, 228, 176);
	std::vector<std::string> contributor_list = {
		/* Please do not remove this name */ "Rexhunter99"
	};

	InterfaceSetFont(fnt_Small);

	DrawURLShadow(550, 42, g_GitHubURL, RGB(126, 178, 239));

	DrawTextShadow(550, 60, "Launcher Code:", color);
	
	auto i = 0U;
	for (auto contributor : contributor_list)
	{
		DrawTextShadow(650, 60 + ((i++) * 15), contributor, color);
	}
}


void DrawMenuHunt()
{
	uint32_t c = RGB(239, 228, 176);
	std::stringstream ss;

	if (g_HuntSelectPic != nullptr && g_HuntSelectPic->IsValid())
	{
		DrawPicture(38, 73, *g_HuntSelectPic);
	}

	InterfaceSetFont(fnt_Big);

	// Draw the profile credits within range of 0 - 9999
	ss << (std::min(9999, std::max(0, g_UserProfile.Score)));
	DrawTextShadow(328 + 8, 38, ss.str(), c, DTA_LEFT);
	ss.str(""); ss.clear();

	// Draw the debited credits for the current hunt options, in range of -999 - 9999
	ss << (std::min(9999, std::max(-999, (g_UserProfile.Score - g_ScoreDebit))));
	DrawTextShadow(472 - 8, 38, ss.str(), c, DTA_RIGHT);
	ss.str(""); ss.clear();

	InterfaceSetFont(fnt_Midd);

	if (g_HuntInfo.first == 0 && !g_AreaInfo.empty()) // Areas
	{
		for (auto i = 1U; i < g_AreaInfo[g_HuntInfo.second].m_Description.size(); i++)
		{
			DrawTextShadow(424, 96 + ((i-1) * 16), g_AreaInfo[g_HuntInfo.second].m_Description[i], c);
		}
	}
	else if (g_HuntInfo.first == 1 && !g_DinoInfo.empty()) // Dinosaurs
	{
		unsigned d = g_DinoList[g_HuntInfo.second];
		if (!(g_DinoInfo[d].m_Price >= 1000 && g_UserProfile.Score < 1000))
		{
			for (auto i = 0U; i < g_DinoInfo[d].m_Description.size(); i++)
			{
				DrawTextShadow(424, 96 + ((i) * 16), g_DinoInfo[d].m_Description[i], c);
			}

			DrawTextShadow(424, 210 + (0 * 16), "Sight:", c);
			DrawTextShadow(424, 210 + (1 * 16), "Hearing:", c);
			DrawTextShadow(424, 210 + (2 * 16), "Scents:", c);

			DrawProgressBar(424 + 80, 210 + (0 * 16), std::min(1.0f, std::max(0.0f, g_DinoInfo[d].m_LookK)) * 2.f);
			DrawProgressBar(424 + 80, 210 + (1 * 16), std::min(1.0f, std::max(0.0f, g_DinoInfo[d].m_HearK)) * 2.f);
			DrawProgressBar(424 + 80, 210 + (2 * 16), std::min(1.0f, std::max(0.0f, g_DinoInfo[d].m_SmellK)) * 2.f);
		}
	}
	else if (g_HuntInfo.first == 2 && !g_WeapInfo.empty()) // Weapons
	{
		for (auto i = 0U; i < g_WeapInfo[g_HuntInfo.second].m_Description.size(); i++)
		{
			DrawTextShadow(424, 96 + ((i) * 16), g_WeapInfo[g_HuntInfo.second].m_Description[i], c);
		}

		DrawTextShadow(424, 210 + (0 * 16), "Power:", c);
		DrawTextShadow(424, 210 + (1 * 16), "Accuracy:", c);
		DrawTextShadow(424, 210 + (2 * 16), "Volume:", c);

		DrawProgressBar(424 + 80, 210 + (0 * 16), std::min(2.0f, std::max(0.0f, g_WeapInfo[g_HuntInfo.second].m_Power)));
		DrawProgressBar(424 + 80, 210 + (1 * 16), std::min(2.0f, std::max(0.0f, g_WeapInfo[g_HuntInfo.second].m_Prec)));
		DrawProgressBar(424 + 80, 210 + (2 * 16), std::min(2.0f, std::max(0.0f, g_WeapInfo[g_HuntInfo.second].m_Loud)));
	}
	else if (g_HuntInfo.first == 3 && !g_UtilInfo.empty()) // Accessories
	{
		if (g_HuntInfo.second < 9000) {
			for (auto i = 0U; i < g_UtilInfo[g_HuntInfo.second].m_Description.size(); i++)
			{
				DrawTextShadow(424, 96 + ((i) * 16), g_UtilInfo[g_HuntInfo.second].m_Description[i], c);
			}
		}
		else {
			// TODO: Don't hardcode this...
			if (g_HuntInfo.second == 9998)
				for (auto i = 0U; i < g_TranqInfo.m_Description.size(); i++)
				{
					DrawTextShadow(424, 96 + ((i) * 16), g_TranqInfo.m_Description[i], c);
				}
			else if (g_HuntInfo.second == 9999)
				for (auto i = 0U; i < g_ObserverInfo.m_Description.size(); i++)
				{
					DrawTextShadow(424, 96 + ((i) * 16), g_ObserverInfo.m_Description[i], c);
				}
		}
	}

	InterfaceSetFont(fnt_Small);

	int32_t score = g_UserProfile.Score - g_ScoreDebit;

	unsigned list_max = std::min(g_AreaInfo.size(), static_cast<size_t>(10));

	for (unsigned ii = MenuHunt[0].Offset; ii < MenuHunt[0].Offset + list_max; ii++) {
		int i = ii - MenuHunt[0].Offset;
		c = 0xB0B070;

		std::stringstream sc;
		sc << g_AreaInfo[ii].m_Price;

		if (score < g_AreaInfo[ii].m_Price)
			c = 0x707070;

		if (MenuHunt[0].Selected == ii)
		{
			c = RGB(255, 255, 10);
		}

		DrawTextShadow(MenuHunt[0].Rect.left + 4, MenuHunt[0].Rect.top + (16 * i), g_AreaInfo[ii].m_Name, c);
		DrawTextShadow(MenuHunt[0].Rect.right - 4, MenuHunt[0].Rect.top + (16 * i), sc.str(), c, DTA_RIGHT);
	}

	for (unsigned ii = MenuHunt[1].Offset; ii < MenuHunt[1].Offset + MenuHunt[1].Item.size(); ii++)
	{
		int i = ii - MenuHunt[1].Offset;
		uint32_t c = 0xB0B070;
		try {
			DinoInfo& di = g_DinoInfo.at(g_DinoList[i]);
			std::string s = di.m_Name;
			std::stringstream sc;

			sc << di.m_Price;

			// (IceAge)
			// If the dinosaur is worth more than 1000 score and the player has less than, we want to hide what it is
			if (di.m_Price >= 1000 && g_UserProfile.Score < 1000)
			{
				s = "???";
				c = 0x707070;
			}

			if (score < di.m_Price)
			{
				c = 0x707070;
			}

			if (MenuHunt[1].Item[i].second)
			{
				c = RGB(255, 255, 10);
			}

			DrawTextShadow(MenuHunt[1].Rect.left + 4, MenuHunt[1].Rect.top + (16 * i), s, c);
			DrawTextShadow(MenuHunt[1].Rect.right - 4, MenuHunt[1].Rect.top + (16 * i), sc.str(), c, DTA_RIGHT);
		}
		catch (std::out_of_range& e) {
			throw std::runtime_error(e.what());
		}
	}

	for (unsigned ii = MenuHunt[2].Offset; ii < MenuHunt[2].Offset + MenuHunt[2].Item.size(); ii++)
	{
		uint32_t c = 0xB0B070;
		int i = ii - MenuHunt[2].Offset;
		WeapInfo& wi = g_WeapInfo[ii];
		std::stringstream sc;
		sc << wi.m_Price;
		
		if (score < wi.m_Price)
		{
			c = 0x707070;
		}

		if (MenuHunt[2].Item[ii].second)
		{
			c = RGB(255, 255, 10);
		}

		DrawTextShadow(MenuHunt[2].Rect.left + 4, MenuHunt[2].Rect.top + (16 * i), g_WeapInfo[ii].m_Name, c);
		DrawTextShadow(MenuHunt[2].Rect.right - 4, MenuHunt[2].Rect.top + (16 * i), sc.str(), c, DTA_RIGHT);
	}

	for (unsigned ii = MenuHunt[3].Offset; ii < MenuHunt[3].Offset + MenuHunt[3].Item.size(); ii++)
	{
		if (ii >= g_UtilInfo.size())
			break;

		uint32_t c = 0xB0B070;
		int i = (int)(ii - MenuHunt[3].Offset);

		std::stringstream sc;
		sc << g_UtilInfo[ii].m_Price;

		// Same affordability grey-out as the other lists: accessories the
		// account cannot cover show dimmed, so their cost is readable even
		// before trying to select them.
		if (score < g_UtilInfo[ii].m_Price)
		{
			c = 0x707070;
		}

		if (MenuHunt[3].Item[ii].second)
		{
			c = RGB(255, 255, 10);
		}

		//DrawTextShadow(MenuHunt[3].Rect.left + 4, MenuHunt[3].Rect.top + (16 * i), MenuHunt[3].Item[ii].first, c);
		DrawTextShadow(MenuHunt[3].Rect.left + 4, MenuHunt[3].Rect.top + (16 * i), g_UtilInfo[ii].m_Name, c);
		DrawTextShadow(MenuHunt[3].Rect.right - 4, MenuHunt[3].Rect.top + (16 * i), sc.str(), c, DTA_RIGHT);
	}
}


/*
Draw the save file 'registry' menu
*/
void DrawMenuRegistry()
{
	POINT& p = g_CursorPos;
	std::stringstream ss;
	uint32_t color = RGB(239, 228, 176);

	if (g_MenuState == MENU_REGISTRY_DELETE)
	{
		InterfaceSetFont(fnt_Midd);
		DrawTextShadow(290, 370, "Do you want to delete player", 0x00B08030);
		ss << "\'" << g_Profiles[g_ProfileIndex].m_Name << "\' ?";
		DrawTextShadow(300, 394, ss.str(), 0x00B08030);
		InterfaceSetFont(0);
	}
	else {
		InterfaceSetFont(fnt_Small);

		if ((timeGetTime() % 800) > 300)
			ss << g_TypingBuffer << "_";
		else
			ss << g_TypingBuffer;

		DrawTextShadow(315, 326, ss.str(), color);

		g_HiliteProfileIndex = g_ProfileIndex;

		// 320, 370
		for (auto i = 0U; i < 7U; i++)
		{
			color = 0xB0B070; // Base colour

			if (i == g_HiliteProfileIndex)
				color = RGB(255, 170, 10);

			if (p.x >= 308 && p.y >= (368 + (16 * i)) && p.x <= 408 && p.y <= (368 + (16 * i) + 16)) {
				g_HiliteProfileIndex = i; //temporary, move to another function for user input
#ifdef _DEBUG
				color = RGB(42, 255, 42);
#endif
			}

			std::string tname = g_Profiles[i].m_Name;

			if (g_Profiles[i].m_Name.empty()) {
				tname = "...";
			}

			DrawTextShadow(320, 370 + (16 * i), tname, color);

			if (!g_Profiles[i].m_Name.empty())
			{
				std::stringstream score_ss;
				score_ss << g_Profiles[i].m_Score;
				DrawTextShadow(480, 370 + (16 * i), score_ss.str(), color, DTA_RIGHT);
			}
		}

		InterfaceSetFont(0);
	}
}


/*
Commit the current registry selection: load an existing profile or create
a new one from the typing buffer. Shared by the GO button, the Enter key
and the original double-click-a-name shortcut.
*/
static void ConfirmRegistrySelection()
{
	if (g_Profiles[g_ProfileIndex].m_Name.empty()) {
		if (!g_TypingBuffer.empty())
		{
			g_UserProfile.New(g_TypingBuffer);
			g_Options.Default();
			TrophySave(g_UserProfile);
			SaveConfig();
			ChangeMenuState(MENU_REGISTRY_WAIVER);
		}
		else
		{
			MessageBox(hwndMain, "You need to enter a name!", "Try again!", MB_OK | MB_ICONINFORMATION);
		}
	}
	else {
		TrophyLoad(g_UserProfile, g_ProfileIndex);
		LoadConfig();
		ChangeMenuState(MENU_MAIN);
	}
}

/*
Keyboard and Mouse handling for menus
NOTE: Could move these to individual functions if a state machine is confusing
*/
void MenuEventInput(int32_t menu)
{
	if (!g_KeyboardUsed) return;

	uint8_t id = g_MenuItem.GetID(g_CursorPos.x / 2, g_CursorPos.y / 2);

	if (g_KeyboardState[VK_LBUTTON] & 128) {
	}

	if (menu == MENU_CREDITS)
	{
		if (g_KeyboardState[VK_RETURN] & 128) {
			g_KeyboardState[VK_LBUTTON] |= 128;
		}

		if (g_KeyboardState[VK_SPACE] & 128) {
			g_KeyboardState[VK_LBUTTON] |= 128;
		}

		if (g_KeyboardState[VK_LBUTTON] & 128) {
			WaitForMouseRelease();
			MenuAudioPlayClick();
			RECT rc = { 550, 42, 600 + GetTextW(hdcCMain, g_GitHubURL), 56 };

			if (IsPointInRect(g_CursorPos, rc))
			{
				ShellExecute(0, 0, TEXT(g_GitHubURL), 0, 0, SW_SHOW);
			}
			else
			{
				ChangeMenuState(MENU_MAIN);
			}
		}
	}
	else if (menu == MENU_REGISTER)
	{
		if (g_KeyboardState[VK_RETURN] & 128) {
			g_KeyboardState[VK_LBUTTON] |= 128;
			id = 1;
			MenuAudioPlayTypeGo();
		}

		if (g_KeyboardState[VK_DELETE] & 128) {
			g_KeyboardState[VK_LBUTTON] |= 128;
			id = 2;
		}

		if (g_KeyboardState[VK_LBUTTON] & 128) {
			if (id == 1) {
				MenuAudioPlayClick();
				WaitForMouseRelease();
				ConfirmRegistrySelection();
			}
			else if (id == 2) {
				// Delete the selected 'save'
				WaitForMouseRelease();
				MenuAudioPlayClick();
				ChangeMenuState(MENU_REGISTRY_DELETE);
			}
			else {
				WaitForMouseRelease();

				// Original behaviour: double-clicking a name in the list enters
				// the menu directly (same as clicking GO); a single click only
				// selects the profile.
				DWORD now = timeGetTime();
				bool inList = false;
				for (auto i = 0U; i < 7U; i++) {
					if (g_CursorPos.x >= 308 && g_CursorPos.y >= (368 + (16 * i)) &&
						g_CursorPos.x <= 408 && g_CursorPos.y <= (368 + (16 * i) + 16)) {
						inList = true;
						break;
					}
				}

				bool doubleClick = inList &&
					g_LastListClickIndex == g_HiliteProfileIndex &&
					(now - g_LastListClickTime) < (DWORD)GetDoubleClickTime();
				g_LastListClickTime = now;
				g_LastListClickIndex = g_HiliteProfileIndex;

				if (doubleClick) {
					g_ProfileIndex = g_HiliteProfileIndex;
					MenuAudioPlayClick();
					ConfirmRegistrySelection();
				}
				else {
					MenuAudioPlayHover();
					g_ProfileIndex = g_HiliteProfileIndex;
					g_TypingBuffer = g_Profiles[g_ProfileIndex].m_Name;
				}
			}
		}
	}
	/*
	*/
	else if (menu == MENU_REGISTRY_DELETE)
	{
		if (g_KeyboardState[VK_LBUTTON] & 128) {
			if (id == 1)
			{
				WaitForMouseRelease();
				MenuAudioPlayClick();
				TrophyDelete(g_ProfileIndex); // Delete the last clicked profile
				ChangeMenuState(MENU_REGISTER);
			}
			else if (id == 2)
			{
				WaitForMouseRelease();
				MenuAudioPlayClick();
				ChangeMenuState(MENU_REGISTER);
			}
		}
	}
	/*
	*/
	else if (menu == MENU_REGISTRY_WAIVER)
	{
		if (g_KeyboardState[VK_LBUTTON] & 128) {
			if (id == 1)
			{
				WaitForMouseRelease();
				MenuAudioPlayClick();
				ChangeMenuState(MENU_MAIN);
			}
			else if (id == 2)
			{
				WaitForMouseRelease();
				MenuAudioPlayClick();
				TrophyDelete(g_ProfileIndex);
				ChangeMenuState(MENU_REGISTER);
			}
		}
	}
	/**************************************
	* Options Menu
	*/
	else if (menu == MENU_OPTIONS) {
		if (g_WaitKey >= 0)
		{
			AcceptNewKey();
		}
		else if (id == 4)
		{
			if (g_KeyboardState[VK_LBUTTON] & 128)
			{
				WaitForMouseRelease();
				MenuAudioPlayClick();
				TrophySave(g_UserProfile); // Save all the settings
				ChangeMenuState(MENU_MAIN);
			}
		}
		else
		{
			for (int m = 0; m < OPT_MAX; m++)
			{
				MenuSet& mo = MenuOptions[m];
				if (IsPointInRect(g_CursorPos, mo.Rect))
				{
					g_MenuItem.SetIsElementSet(m + 1, true);

					// Use layout system for hit detection
					Layout::Column* col = nullptr;
					if (m == OPT_GAME) col = &g_GameLabels;
					else if (m == OPT_KEYBINDINGS) col = &g_ControlsLabels;
					else if (m == OPT_VIDEO) col = &g_VideoLabels;

					if (col && col->itemCount > 0) {
						int spacing = col->GetSpacing();
						int yd = g_CursorPos.y - col->yStart;
						if (yd >= 0 && yd < (col->yEnd - col->yStart)) {
							mo.Hilite = yd / spacing;
							if (mo.Hilite >= (int)col->itemCount)
								mo.Hilite = col->itemCount - 1;
						}
						else mo.Hilite = -1;
					}
					else mo.Hilite = -1;

					if (g_KeyboardState[VK_LBUTTON] & 128) // Left Click
					{
						if (m == OPT_GAME)
						{
							int sliderX = OptionsLayout::OPTION_SLIDER_X;
							int tbw = OptionsLayout::OPTION_SLIDER_W;
							float v = static_cast<float>((g_CursorPos.x - sliderX)) / static_cast<float>(tbw);
							if (v < 0.0f) v = 0.0f;
							if (v > 1.0f) v = 1.0f;

							mo.Selected = mo.Hilite;

							if (mo.Hilite == 0)
							{
								if (g_CursorPos.x >= sliderX && g_CursorPos.x <= sliderX + tbw)
									g_Options.Aggression = static_cast<int>((v * 255.f));
							}
							else if (mo.Hilite == 1)
							{
								if (g_CursorPos.x >= sliderX && g_CursorPos.x <= sliderX + tbw)
									g_Options.Density = static_cast<int>((v * 255.f));
							}
							else if (mo.Hilite == 2)
							{
								if (g_CursorPos.x >= sliderX && g_CursorPos.x <= sliderX + tbw)
									g_Options.Sensitivity = static_cast<int>((v * 255.f));
							}
							else if (mo.Hilite == 3)
							{
								if (g_CursorPos.x >= sliderX && g_CursorPos.x <= sliderX + tbw)
									g_Options.ViewRange = MenuViewOptFromSlider(v);
							}
							else if (mo.Hilite == 4) // Object detail
							{
								if (g_CursorPos.x >= sliderX && g_CursorPos.x <= sliderX + tbw) {
									g_Options.ObjectDetail = MenuObjectDetailFromSlider(v);
									SaveConfig();
								}
							}
							else if (mo.Hilite == 5) // Metric or Imperial(US)
							{
								WaitForMouseRelease();
								g_Options.OptSys = !g_Options.OptSys;
							}
							else if (mo.Hilite == 6)
							{
								WaitForMouseRelease();
								g_Options.SoundAPI = (g_Options.SoundAPI + 1) % AUDIO_BACKEND_COUNT;
							}
							else if (mo.Hilite == 7)
							{
								WaitForMouseRelease();
								g_Options.OptFpsLimit = (g_Options.OptFpsLimit + 1) % kFpsLimitCount;
								SaveConfig();
							}
						}
						else if (m == OPT_KEYBINDINGS) { // Left Click
							int sliderX = OptionsLayout::CONTROLS_SLIDER_X;
							int tbw = OptionsLayout::CONTROLS_SLIDER_W;
							float v = static_cast<float>((g_CursorPos.x - sliderX)) / static_cast<float>(tbw);
							if (v < 0.0f) v = 0.0f;
							if (v > 1.0f) v = 1.0f;

							mo.Selected = mo.Hilite;

							if (static_cast<size_t>(mo.Hilite) < MenuOptions[OPT_KEYBINDINGS].Item.size() - 3)
							{
								WaitForMouseRelease();
								g_WaitKey = mo.Hilite;
							}
							else if (static_cast<int>(mo.Hilite) == MenuOptions[OPT_KEYBINDINGS].Item.size() - 3) // Night vision toggle key
							{
								WaitForMouseRelease();
								g_WaitKey = mo.Hilite;
							}
							else if (static_cast<int>(mo.Hilite) == MenuOptions[OPT_KEYBINDINGS].Item.size() - 2) // Mouse Y-Axis Inverted
							{
								WaitForMouseRelease();
								g_Options.MouseInvert = !g_Options.MouseInvert;
							}
							else if (static_cast<int>(mo.Hilite) == MenuOptions[OPT_KEYBINDINGS].Item.size() - 1) // Mouse Sensitivty Slider
							{
								if (g_CursorPos.x >= sliderX && g_CursorPos.x <= sliderX + tbw)
									g_Options.MouseSensitivity = static_cast<int>((v * 255.f));
							}
						}
						else if (m == OPT_VIDEO) { // Left Click
							int sliderX = OptionsLayout::OPTION_SLIDER_X;
							int tbw = OptionsLayout::OPTION_SLIDER_W;
							float v = static_cast<float>((g_CursorPos.x - sliderX)) / static_cast<float>(tbw);
							if (v < 0.0f) v = 0.0f;
							if (v > 1.0f) v = 1.0f;

							mo.Selected = mo.Hilite;
							if (mo.Hilite == 0)
							{
								WaitForMouseRelease();
								g_Options.RenderAPI++;
								if (g_Options.RenderAPI >= kRenderAPI_Count)
									g_Options.RenderAPI = kRenderAPI_Software;
								g_Options.RenderAPI = NormalizeMenuRenderAPI(g_Options.RenderAPI);
								SaveConfig();
							}
							if (mo.Hilite == 1) // Resolution
							{
								WaitForMouseRelease();
								// Cycle through the dynamic list. g_ResCount is
								// rebuilt by EnumerateResolutions() at menu startup.
								// Clamp at g_ResCount (g_ResolutionList[] length) instead
								// of the old fixed RES_MAX=8.
								g_Options.Resolution++;
								if (g_Options.Resolution >= g_ResCount)
									g_Options.Resolution = 0;
								// Persist immediately: config.cfg's WxH line
								// overrides the profile index at engine boot,
								// so without this the pick never reaches the hunt.
								SaveConfig();
							}
							else if (mo.Hilite == 2) // Display Mode
							{
								WaitForMouseRelease();
								g_Options.DisplayMode++;
								if (g_Options.DisplayMode >= kDisplayModeCount)
									g_Options.DisplayMode = 0;
								SaveConfig();
							}
							else if (mo.Hilite == 3) // Shadows
							{
								WaitForMouseRelease();
								g_Options.Shadows = !g_Options.Shadows;
							}
							else if (mo.Hilite == 4) // Fog
							{
								WaitForMouseRelease();
								g_Options.Fog = !g_Options.Fog;
							}
							else if (mo.Hilite == 5) // Textures
							{
								WaitForMouseRelease();
								g_Options.Textures++;
								if (g_Options.Textures == 3)
									g_Options.Textures = 0;
							}
							else if (mo.Hilite == 6) // Colorkey
							{
								WaitForMouseRelease();
								g_Options.AlphaColorKey = !g_Options.AlphaColorKey;
							}
							else if (mo.Hilite == 7) // Brightness
							{
								if (g_CursorPos.x >= sliderX && g_CursorPos.x <= sliderX + tbw)
									g_Options.Brightness = static_cast<int>((v * 255.f));
							}
							else if (mo.Hilite == 8) // Field of View
							{
								if (g_CursorPos.x >= sliderX && g_CursorPos.x <= sliderX + tbw)
								{
									int fov = kFovMin + static_cast<int>((v * static_cast<float>((kFovMax - kFovMin))));
									fov = kFovMin + ((fov - kFovMin) / kFovStep) * kFovStep;
									if (fov < kFovMin) fov = kFovMin;
									if (fov > kFovMax) fov = kFovMax;
									g_Options.FOV = fov;
									SaveConfig();
								}
							}
						}
					}
				}
				else
				{
					g_MenuItem.SetIsElementSet(m + 1, false);
					mo.Hilite = -1;
				}
			}
		}
	}
	else if (menu == MENU_HUNT) {
		if (g_KeyboardState[VK_ESCAPE] & 128)
		{
			ChangeMenuState(MENU_QUIT);
		}

		// Mouse hover
		if (IsPointInRect(g_CursorPos, MenuHunt[0].Rect))
		{
			WaitForMouseRelease();
			int32_t scorea = 0;

			if (MenuHunt[0].Selected != -1)
			{
				scorea = g_AreaInfo[MenuHunt[0].Selected].m_Price;
			}

			int32_t score = (g_UserProfile.Score - g_ScoreDebit) + scorea;
			int yd = g_CursorPos.y - MenuHunt[0].Rect.top;

			unsigned index = yd / 16;

			if (index < g_AreaInfo.size())
			{
				if (g_AreaInfo[index].m_Price >= 1000 && g_UserProfile.Score < 1000)
					g_HuntSelectPic = &g_AreaInfo[index].m_ThumbnailHidden;
				else
					g_HuntSelectPic = &g_AreaInfo[index].m_Thumbnail;
				g_HuntInfo.first = 0; // Areas
				g_HuntInfo.second = index;

				if ((g_KeyboardState[VK_LBUTTON] & 128) && score >= g_AreaInfo[index].m_Price)
				{
					WaitForMouseRelease();
					MenuAudioPlayClick();

					// Reset the states
					for (auto i = MenuHunt[0].Item.begin(); i != MenuHunt[0].Item.end(); i++)
					{
						i->second = false;
					}

					MenuHunt[0].Item[index].second = true;
					MenuHunt[0].Selected = index;

					g_ScoreDebit = CalculateDebit();
				}
			}
		}
		else if (IsPointInRect(g_CursorPos, MenuHunt[1].Rect))
		{
			int32_t score = g_UserProfile.Score - g_ScoreDebit;
			int yd = g_CursorPos.y - MenuHunt[1].Rect.top;

			unsigned index = yd / 16;
			unsigned dataIndex = index + MenuHunt[1].Offset; // Account for scroll offset

			if (dataIndex < g_DinoList.size())
			{
				if (g_DinoInfo[g_DinoList[dataIndex]].m_Price >= 1000 && g_UserProfile.Score < 1000)
					g_HuntSelectPic = &g_DinoInfo[g_DinoList[dataIndex]].m_ThumbnailHidden;
				else
					g_HuntSelectPic = &g_DinoInfo[g_DinoList[dataIndex]].m_Thumbnail;
				g_HuntInfo.first = 1; // Dinos
				g_HuntInfo.second = dataIndex;

				if ((g_KeyboardState[VK_LBUTTON] & 128))
				{
					WaitForMouseRelease();
					MenuAudioPlayClick();

					if (score >= g_DinoInfo[g_DinoList[dataIndex]].m_Price && !MenuHunt[1].Item[dataIndex].second)
					{
						MenuHunt[1].Item[dataIndex].second = true;
					}
					else
					{
						MenuHunt[1].Item[dataIndex].second = false;
					}

					g_ScoreDebit = CalculateDebit();
				}
			}
		}
		else if (IsPointInRect(g_CursorPos, MenuHunt[2].Rect))
		{
			int32_t score = g_UserProfile.Score - g_ScoreDebit;
			int yd = g_CursorPos.y - MenuHunt[2].Rect.top;

			unsigned index = yd / 16;

			if (index < MenuHunt[2].Item.size())
			{
				if (g_WeapInfo[index].m_Price >= 1000 && g_UserProfile.Score < 1000)
					g_HuntSelectPic = &g_WeapInfo[index].m_ThumbnailHidden;
				else
					g_HuntSelectPic = &g_WeapInfo[index].m_Thumbnail;
				g_HuntInfo.first = 2; // Weapons
				g_HuntInfo.second = index;

				if ((g_KeyboardState[VK_LBUTTON] & 128))
				{
					WaitForMouseRelease();
					MenuAudioPlayClick();

					if (score >= g_WeapInfo[index].m_Price && !MenuHunt[2].Item[index].second)
					{
						MenuHunt[2].Item[index].second = true;
					}
					else
					{
						MenuHunt[2].Item[index].second = false;
					}

					g_ScoreDebit = CalculateDebit();
				}
			}
		}
		else if (IsPointInRect(g_CursorPos, MenuHunt[3].Rect))
		{
			int32_t score = g_UserProfile.Score - g_ScoreDebit;
			int yd = g_CursorPos.y - MenuHunt[3].Rect.top;
			unsigned accIndex = (unsigned)(yd / 16) + MenuHunt[3].Offset;

			// MenuHunt[3].Item and g_UtilInfo are parallel and fully listed,
			// so the cursor row maps directly to the accessory index.
			if (accIndex < MenuHunt[3].Item.size() && accIndex < g_UtilInfo.size())
			{
				g_HuntSelectPic = &g_UtilInfo[accIndex].m_Thumbnail;
				g_HuntInfo.first = 3; // Accessories
				g_HuntInfo.second = accIndex;

				if ((g_KeyboardState[VK_LBUTTON] & 128))
				{
					WaitForMouseRelease();
					MenuAudioPlayClick();

					// Same affordability gate as dinos/weapons: selecting
					// requires covering the price, deselecting is always
					// free — so the account can never be driven negative.
					if (score >= g_UtilInfo[accIndex].m_Price && !MenuHunt[3].Item[accIndex].second)
					{
						MenuHunt[3].Item[accIndex].second = true;
					}
					else
					{
						MenuHunt[3].Item[accIndex].second = false;
					}

					g_ScoreDebit = CalculateDebit();
				}
			}
		}
		else if (id == 6) {
			// Observer info panel
			g_HuntSelectPic = &g_ObserverInfo.m_Thumbnail;
			g_HuntInfo.first = 3; // Accessories
			g_HuntInfo.second = 9999;
		}

		// Left Mouse Click
		if (g_KeyboardState[VK_LBUTTON] & 128)
		{
			if (id >= 1 && id <= 6)
			{
				WaitForMouseRelease();
				MenuAudioPlayClick();
				bool b = g_MenuItem.ToggleIsElementSet(id);

				if (id >= 1 && id <= 3)
				{
					g_TimeOfDay = id - 1;

					for (int i = 1; i <= 3; i++)
					{
						if (i != id)
							g_MenuItem.SetIsElementSet(i, false);
					}
				}

				if (id == 4)
					b = false; // Unused
				if (id == 6) {
					g_ObserverMode = b;
				}
			}
			else if (id == 7) // Back
			{
				WaitForMouseRelease();
				MenuAudioPlayClick();
				ChangeMenuState(MENU_MAIN);
			}
			else if (id == 8) // Hunt/Next
			{
				// Launch the game
				WaitForMouseRelease();
				MenuAudioPlayClick();
				
				if (MenuHunt[0].Selected == -1)
				{
					// -- Don't launch
					return;
				}

				int din = 0;
				int wep = 0;

				// Calculate the dinosaur flags
				for (unsigned i = 0; i < g_DinoList.size(); i++)
				{
					if (MenuHunt[1].Item[i].second)
						din |= 1 << i;
				}

				// Calculate the weapon flags
				for (unsigned i = 0; i < g_WeapInfo.size(); i++)
				{
					if (MenuHunt[2].Item[i].second)
						wep |= 1 << i;
				}

				// Initialise the command line parameters. For slot six, launch the
				// basename whose files actually exist (m_MapFile): vanilla resolves
				// external.map/.rsc, mods that ship area6.map/.rsc resolve there.
				// m_ProjectName keeps the logical slot name "area6" for saved-hunt
				// restore matching. The engine aliases external->area6 in its script
				// area filtering, so either basename loads correctly.
				std::stringstream params("");
				const AreaInfo& launchArea = g_AreaInfo[MenuHunt[0].Selected];
				const std::string& launchName =
					(launchArea.m_MapFile == "external") ? std::string("external") : launchArea.m_ProjectName;
				params << " reg=" << g_UserProfile.RegNumber;
				params << " prj=huntdat/areas/" << launchName;
				params << " din=" << din;
				params << " wep=" << wep;
				params << " dtm=" << g_TimeOfDay;

#ifdef _iceage
				// Ice Age resupply
				if (MenuHunt[3].Item[4].second)
					params << " " << g_UtilInfo[4].m_Command;
#endif //_iceage

				if (MenuHunt[3].Item[kAccDouble].second)
					params << " " << g_UtilInfo[kAccDouble].m_Command;

				// Night vision goggles
				{
					if (kAccNV < static_cast<int>(MenuHunt[3].Item.size()) && MenuHunt[3].Item[kAccNV].second)
						params << " " << g_UtilInfo[kAccNV].m_Command;
				}

				if (MenuHunt[3].Item[kAccCamo].second) {
					g_Options.CamoMode = true;
					params << " -camo";
				}
				else {
					g_Options.CamoMode = true;
				}

				if (MenuHunt[3].Item[kAccRadar].second) {
					g_Options.RadarMode = true;
					params << " -radar";
				}
				else {
					g_Options.RadarMode = true;
				}

				if (MenuHunt[3].Item[kAccScent].second) {
					g_Options.ScentMode = true;
					params << " -scent";
				}
				else {
					g_Options.ScentMode = true;
				}

				// Tranquilizers
				{
					if (kAccTranq < static_cast<int>(MenuHunt[3].Item.size()) && MenuHunt[3].Item[kAccTranq].second)
						params << " " << g_UtilInfo[kAccTranq].m_Command;
				}

				if (g_ObserverMode)
					params << " -observ";

				// Pass accessory score multipliers to the engine. Order must
				// match the engine's smod= parser in Hunt/Game.cpp
				// ProcessCommandLine(): camo, radar, scent, double, tranq, observer.
				// Values come from UtilInfo.m_ScoreMod (populated from _RES.TXT's
				// 'accessories {}' block, falling back to legacy defaults).
				params << " smod=" << g_UtilInfo[kAccCamo].m_ScoreMod  // camo
				       << ","  << g_UtilInfo[kAccRadar].m_ScoreMod    // radar
				       << ","  << g_UtilInfo[kAccScent].m_ScoreMod    // scent
				       << ","  << g_UtilInfo[kAccDouble].m_ScoreMod    // double
				       << ","  << g_UtilInfo[kAccTranq].m_ScoreMod    // tranq
				       << ","  << g_ObserverInfo.m_ScoreMod;  // observer

#ifdef _DEBUG
				params << " -debug";
#endif //_DEBUG

				std::stringstream renderer("");
				renderer << g_RendererFile[g_Options.RenderAPI] << ".ren";

				// Observer mode is an exploration session and deliberately permits an
				// empty creature/weapon loadout, matching the original launcher.
				if (g_ObserverMode || (wep && din))
				{
					TrophySave(g_UserProfile); // Save all the settings
					AppendDisplayModeLaunchFlag(params);
					std::cout << "Launching...  `> " << renderer.str() << " " << params.str() << "`" << std::endl;
					SaveHuntSelections();
					SaveConfig();
					LaunchProcess(renderer.str(), params.str());
					TrophyLoad(g_UserProfile, g_UserProfile.RegNumber); // Load the changes
				LoadConfig();
				}
				else
				{
					ShowErrorMessage("You need to select at least:\r\n 1x Creature\r\n 1x Weapon");
				}
			}
		}
	}
	else if (menu == MENU_MAIN)
	{
		if (g_KeyboardState[VK_ESCAPE] & 128) {
			ChangeMenuState(MENU_QUIT);
		}
		else if (g_KeyboardState[VK_LBUTTON] & 128) {
			WaitForMouseRelease();
			if (id >= 1 && id <= 6) {
				WaitForMouseRelease();
				MenuAudioPlayClick();
				if (id == 1) { ChangeMenuState(MENU_HUNT); }
				else if (id == 2) { ChangeMenuState(MENU_OPTIONS); }
				else if (id == 3) {
					std::stringstream params("");

					params << "reg=" << g_UserProfile.RegNumber;
					params << " prj=huntdat/areas/trophy";
					params << " dtm=" << 1;
#ifdef _DEBUG
					params << " -debug";
#endif //_DEBUG
					std::stringstream renderer("");
					renderer << g_RendererFile[g_Options.RenderAPI] << ".ren";

					AppendDisplayModeLaunchFlag(params);
					std::cout << "Execute: [" << renderer.str() << " " << params.str() << "]" << std::endl;
					TrophySave(g_UserProfile); // Save the changes
					LaunchProcess(renderer.str(), params.str());
					TrophyLoad(g_UserProfile, g_UserProfile.RegNumber); // Load the changes
				LoadConfig();
				}
				else if (id == 4) { ChangeMenuState(MENU_CREDITS); }
				else if (id == 5) { ChangeMenuState(MENU_QUIT); }
				else if (id == 6) { ChangeMenuState(MENU_STATISTICS); }
			}
		}
	}
	else if (menu == MENU_STATISTICS)
	{
		if (g_KeyboardState[VK_ESCAPE] & 128) {
			ChangeMenuState(MENU_QUIT);
		}

		if (g_KeyboardState[VK_LBUTTON] & 128) {
			WaitForMouseRelease();
			MenuAudioPlayClick();
			ChangeMenuState(MENU_MAIN);
		}
	}
	else if (menu == MENU_QUIT)
	{
		if (g_KeyboardState[VK_LBUTTON] & 128) {
			WaitForMouseRelease();
			MenuAudioPlayClick();

			if (id == 1)      RequestMenuExit(0);
			else if (id == 2) ChangeMenuState(MENU_MAIN);
		}
	}
}


void DrawMenuOptions()
{
	const int label_c = RGB(181, 134, 82);  // StartLegacy option label color
	const int value_c = RGB(165, 181, 181); // StartLegacy option value color
	const int on_c = RGB(255, 210, 80);

	InterfaceSetFont(g_FontOptions);

	// Game options
	for (auto i = 0U; i < MenuOptions[OPT_GAME].Item.size(); i++) {
		int y0 = g_GameLabels.GetItemY(i);
		int c = (MenuOptions[OPT_GAME].Hilite == i) ? on_c : label_c;

		DrawTextShadow(g_GameLabels.x0, y0, MenuOptions[OPT_GAME].Item[i].first, c, DTA_RIGHT);

		if (i == 0) DrawSliderBar(OptionsLayout::OPTION_SLIDER_X, y0 + 12, OptionsLayout::OPTION_SLIDER_W, static_cast<float>(g_Options.Aggression) / 255.0f, label_c);
		else if (i == 1) DrawSliderBar(OptionsLayout::OPTION_SLIDER_X, y0 + 12, OptionsLayout::OPTION_SLIDER_W, static_cast<float>(g_Options.Density) / 255.0f, label_c);
		else if (i == 2) DrawSliderBar(OptionsLayout::OPTION_SLIDER_X, y0 + 12, OptionsLayout::OPTION_SLIDER_W, static_cast<float>(g_Options.Sensitivity) / 255.0f, label_c);
		else if (i == 3) DrawSliderBar(OptionsLayout::OPTION_SLIDER_X, y0 + 12, OptionsLayout::OPTION_SLIDER_W, static_cast<float>((g_Options.ViewRange - kViewOptMin)) / static_cast<float>((kViewOptMax - kViewOptMin)), label_c);
		else if (i == 4) DrawSliderBar(OptionsLayout::OPTION_SLIDER_X, y0 + 12, OptionsLayout::OPTION_SLIDER_W, static_cast<float>((g_Options.ObjectDetail - kObjectDetailMin)) / static_cast<float>((kObjectDetailMax - kObjectDetailMin)), label_c);
		else DrawTextShadow(g_GameValues.x0, y0, i == 5 ? st_UnitText[g_Options.OptSys] : (i == 6 ? st_AudText[NormalizeAudioBackend(g_Options.SoundAPI)] : st_FpsText[g_Options.OptFpsLimit]), value_c);
	}

	// Control key bindings
	for (auto i = 0U; i < MenuOptions[OPT_KEYBINDINGS].Item.size(); i++) {
		const int nvIndex = static_cast<int>(MenuOptions[OPT_KEYBINDINGS].Item.size() - 3);

		// Key name string: use NightVisionKey for the NV slot, KeyMap for others
		std::stringstream ss;
		if (static_cast<int>(i) == nvIndex) {
			ss << g_KeyNames[MapVKKey(g_Options.NightVisionKey)];
		} else {
			ss << g_KeyNames[MapVKKey(*((int32_t*)&g_Options.KeyMap + i))];
		}

		int y0 = g_ControlsLabels.GetItemY(i);
		int c = (MenuOptions[OPT_KEYBINDINGS].Hilite == i) ? on_c : label_c;

		DrawTextShadow(g_ControlsLabels.x0, y0, MenuOptions[OPT_KEYBINDINGS].Item[i].first, c, DTA_RIGHT);

		if (static_cast<int>(i) < MenuOptions[OPT_KEYBINDINGS].Item.size() - 3)
		{
			// KeyMap-bound keys
			if (g_WaitKey == i)
				DrawTextShadow(g_ControlsValues.x0, y0, "<?>", value_c);
			else
				DrawTextShadow(g_ControlsValues.x0, y0, ss.str(), value_c);
		}
		else if (static_cast<int>(i) == nvIndex)
		{
			// Night vision toggle key (stored in NightVisionKey, not KeyMap)
			if (g_WaitKey == i)
				DrawTextShadow(g_ControlsValues.x0, y0, "<?>", value_c);
			else
				DrawTextShadow(g_ControlsValues.x0, y0, ss.str(), value_c);
		}
		else if (static_cast<int>(i) == MenuOptions[OPT_KEYBINDINGS].Item.size() - 2)
			DrawTextShadow(g_ControlsValues.x0, y0, st_BoolText[static_cast<int>(g_Options.MouseInvert)], value_c);
		else if (static_cast<int>(i) == MenuOptions[OPT_KEYBINDINGS].Item.size() - 1)
			DrawSliderBar(OptionsLayout::CONTROLS_SLIDER_X, y0 + 12, OptionsLayout::CONTROLS_SLIDER_W, static_cast<float>(g_Options.MouseSensitivity) / 255.0f, label_c);
	}

	// Video/Graphics options
	for (auto i = 0U; i < MenuOptions[OPT_VIDEO].Item.size(); i++) {
		int y0 = g_VideoLabels.GetItemY(i);
		int c = (MenuOptions[OPT_VIDEO].Hilite == i) ? on_c : label_c;

		DrawTextShadow(g_VideoLabels.x0, y0, MenuOptions[OPT_VIDEO].Item[i].first, c, DTA_RIGHT);

		if (i == 0) DrawTextShadow(g_VideoValues.x0, y0, st_RenText[g_Options.RenderAPI], value_c);
		else if (i == 1) {
			// Render the selected resolution from g_ResolutionList[]. The list
			// is built by EnumerateResolutions() at menu startup, so any mode
			// the display supports can be shown. Clamp the index defensively
			// in case the saved value is out of range (e.g., a profile from a
			// 1920x1080 display loaded on a 1366x768 one).
			int idx = g_Options.Resolution;
			if (idx < 0 || idx >= g_ResCount) idx = 0;
			static char resStr[32];
			sprintf(resStr, "%d x %d", g_ResolutionList[idx].w, g_ResolutionList[idx].h);
			DrawTextShadow(g_VideoValues.x0, y0, resStr, value_c);
		}
		else if (i == 2) {
			int dm = g_Options.DisplayMode;
			if (dm < 0 || dm >= kDisplayModeCount) dm = 0;
			DrawTextShadow(g_VideoValues.x0, y0, st_DisplayModeText[dm], value_c);
		}
		else if (i == 3) DrawTextShadow(g_VideoValues.x0, y0, st_BoolText[g_Options.Shadows], value_c);
		else if (i == 4) DrawTextShadow(g_VideoValues.x0, y0, st_BoolText[g_Options.Fog], value_c);
		else if (i == 5) DrawTextShadow(g_VideoValues.x0, y0, st_TextureText[g_Options.Textures], value_c);
		else if (i == 6) DrawTextShadow(g_VideoValues.x0, y0, st_AlphaKeyText[g_Options.AlphaColorKey], value_c);
		else if (i == 7) DrawSliderBar(OptionsLayout::OPTION_SLIDER_X, y0 + 12, OptionsLayout::OPTION_SLIDER_W, static_cast<float>(g_Options.Brightness) / 255.0f, label_c);
		else if (i == 8) {
			float t = static_cast<float>((g_Options.FOV - kFovMin)) / static_cast<float>((kFovMax - kFovMin));
			DrawSliderBar(OptionsLayout::OPTION_SLIDER_X, y0 + 12, OptionsLayout::OPTION_SLIDER_W, t, label_c);
		}
	}

	InterfaceSetFont(nullptr);
}


// TODO: Move these to Hunt.h
#define FPS_TARGET 60L
#define FRAME_TIME_DELTA (1000L / FPS_TARGET) // The time in milliseconds a frame takes to process to achieve FPS_TARGET

int64_t g_PrevFrameTime = 0;
#ifdef _DEBUG
int32_t g_Frames = 0;
int32_t g_FramesPerSecond = 0;
int64_t g_PrevFrameCountTime = 0;
#endif

/*
Perform per-frame/tick update of the menus
*/
void ProcessMenu()
{
	NormalizeRendererOption();

	GetCursorPos(&g_CursorPos);
	ScreenToClient(hwndMain, &g_CursorPos);

	// Scale mouse coordinates from scaled window to internal 800x600 space
	if (g_ScaleX > 0.0f)
		g_CursorPos.x = static_cast<int>(g_CursorPos.x / g_ScaleX);
	if (g_ScaleY > 0.0f)
		g_CursorPos.y = static_cast<int>(g_CursorPos.y / g_ScaleY);

	// Restrict the virtual cursor to the client area
	if (g_CursorPos.x < 0) g_CursorPos.x = 0;
	if (g_CursorPos.y < 0) g_CursorPos.y = 0;
	if (g_CursorPos.x >= MENU_BASE_WIDTH) g_CursorPos.x = MENU_BASE_WIDTH - 1;
	if (g_CursorPos.y >= MENU_BASE_HEIGHT) g_CursorPos.y = MENU_BASE_HEIGHT - 1;

	// Get the keyboard state
	if (GetActiveWindow() == hwndMain) {
		g_KeyboardUsed = GetKeyboardState(g_KeyboardState);
	}
	else {
		memset(g_KeyboardState, 0, 256);
		g_KeyboardUsed = false;
	}

	// Trigger the Start() event of the menu if applicable
	if (g_PrevMenuState != g_MenuState) {
		MenuEventStart(g_MenuState);
		g_PrevMenuState = g_MenuState;
	}

	// Handle input from mouse/keyboard
	MenuEventInput(g_MenuState);

	// Clear the video buffer and then draw the menu background
	InterfaceClear(HIRGB(0, 0, 10));
	DrawMenuBg(g_MenuItem);

	// Draw menus
	switch (g_MenuState)
	{
	case MENU_REGISTRY_DELETE: DrawMenuRegistry(); break;
	case MENU_REGISTER: DrawMenuRegistry(); break;
	case MENU_MAIN: DrawMenuProfile(); break;
	case MENU_STATISTICS: DrawMenuStatistics(); break;
	case MENU_QUIT: DrawMenuProfile(); break;
	case MENU_OPTIONS: DrawMenuOptions(); break;
	case MENU_CREDITS: DrawMenuCredits(); break;
	case MENU_HUNT: DrawMenuHunt(); break;
	}

#ifdef _DEBUG
	// Perform some framerate metric stuff, only for [Debug] builds though
	g_Frames++;

	InterfaceSetFont(fnt_Small);

	int64_t t = Timer::GetTime();
	int64_t t_diff = t - g_PrevFrameTime;

	std::stringstream ss;
	ss << "FPS: " << g_FramesPerSecond;
	DrawTextShadow(2, 2, ss.str(), RGB(255, 60, 60));
	ss.str(""); ss.clear();

	ss << "FT:  " << t_diff << "ms";
	DrawTextShadow(2, 2 + 14, ss.str(), RGB(255, 60, 60));
	ss.str(""); ss.clear();

	ss << "XY:  " << g_CursorPos.x << "x" << g_CursorPos.y;
	DrawTextShadow(2, 2 + 28, ss.str(), RGB(255, 60, 60));

	if ((t - g_PrevFrameCountTime) >= 1000) {
		g_FramesPerSecond = g_Frames;
		g_Frames = 0;
		g_PrevFrameCountTime = t;
	}

	g_PrevFrameTime = t;
#endif

	// Draw the GDI buffer to the window
	InterfaceBlt();
}


void MenuKeyCharEvent(uint16_t wParam)
{
	if (g_MenuState == MENU_REGISTER) {
		if (wParam == 8) {
			if (!g_TypingBuffer.empty()) {
				g_TypingBuffer.pop_back();
				MenuAudioPlayType();
			}
		}
		else {
			if (g_TypingBuffer.size() < 19) {
				if (wParam >= 32 && wParam <= 128) {
					g_TypingBuffer.push_back(static_cast<char>(wParam));
					MenuAudioPlayType();
				}
			}
		}
	}
}


void MenuMouseScrollEvent(int32_t menu, int32_t scroll)
{
	// TODO: Enable scrolling of text lists
	if (menu == MENU_REGISTER)
	{

	}
	else if (menu == MENU_HUNT)
	{
		for (int i = 0; i < 4; i++)
		{
			if (IsPointInRect(g_CursorPos, MenuHunt[i].Rect) && MenuHunt[i].Item.size() > 10)
			{
				MenuHunt[i].Offset -= scroll;

				if (MenuHunt[i].Offset < 0)
					MenuHunt[i].Offset = 0;
				else if (MenuHunt[i].Offset > MenuHunt[i].Item.size() - 10)
					MenuHunt[i].Offset = MenuHunt[i].Item.size() - 10;
			}
		}
	}
}

