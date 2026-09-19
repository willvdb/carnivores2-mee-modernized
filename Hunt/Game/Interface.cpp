#define INITGUID
#include "Hunt.h"
#include "Platform/Platform.h"
#include "Game/DisplaySelection.h"
#include "Game/DisplayRecovery.h"
#include "Renderer/CPUText.h"
#ifdef _gl
#include "Renderer/GLRenderer.h"
#endif
#include "stdio.h"


struct TMenuSet
{
  int x0, y0;
  int Count;
  char Item[32][32];
};


TMenuSet Options[3];
char HMLtxt[3][12];
char CKtxt[2][16];
char Restxt[8][24];
char Textxt[3][12];
char Ontxt[2][12];
char Systxt[2][12];
int CurPlayer = -1;
int MaxDino, AreaMax, LoadCount;

#define REGLISTX 320
#define REGLISTY 370

std::int32_t NEWPLAYER = false;

#ifdef _WIN32
int  MapVKKey(int k)
{
  if (k==LegacyKey::LBUTTON) return 124;
  if (k==LegacyKey::RBUTTON) return 125;
  return MapVirtualKey(k, 0);
}




POINT p;
int OptMode = 0;
int OptLine = 0;


void wait_mouse_release()
{
  while (GetAsyncKeyState(LegacyKey::RBUTTON) & 0x80000000);
  while (GetAsyncKeyState(LegacyKey::LBUTTON) & 0x80000000);

}


#endif

#ifdef _WIN32
int GetTextW(HDC hdc, const char* s)
{
  SIZE sz;
  GetTextExtentPoint(hdc, s, strlen(s), &sz);
  return sz.cx;
}

#endif
void PrintText(const char* s, int x, int y, int rgb)
{
  if (auto* canvas = CPUText::GameCanvas()) {
    canvas->Draw(x+1, y+1, s, 0, CPUText::MiddleFont());
    canvas->Draw(x, y, s, rgb, CPUText::MiddleFont());
  }
}

void DoHalt(const char* Mess)
{

	LOG_ERROR("ABNORMAL_HALT: %s", Mess ? Mess : "");
	if (strlen(Mess))
	{
		PrintLog("ABNORMAL_HALT: ");
		PrintLog(Mess);
		PrintLog("\n");
		Platform::ShowMessage("Carnivores Termination", Mess);
	}

	if (Multiplayer) {
		if (Host) {
			ShutDownServer();
		}
		else {
			ShutDownClient();
		}
	}

  AudioStop();
  Audio_Shutdown();

  ShutDown3DHardware();
#ifdef _WIN32
  EnableWindow(hwndMain, false);
#endif
  Platform::ShutdownApplication();

  CloseLog();
  LogClose();
#ifdef _WIN32
  TerminateProcess(GetCurrentProcess(), 0);
#else
  std::_Exit(0);
#endif
}

//For stopping the program before audio/3d hardware startup
void DoHalt2(const char* Mess)
{
	LOG_ERROR("ABNORMAL_HALT: %s", Mess ? Mess : "");
//	AudioStop();
//	Audio_Shutdown();

//	ShutDown3DHardware();
#ifdef _WIN32
  EnableWindow(hwndMain, false);
#endif
	Platform::ShutdownApplication();
	if (strlen(Mess))
	{
		PrintLog("ABNORMAL_HALT: ");
		PrintLog(Mess);
		PrintLog("\n");
		Platform::ShowMessage("Carnivores Termination", Mess);
	}

	CloseLog();
	LogClose();
#ifdef _WIN32
  TerminateProcess(GetCurrentProcess(), 0);
#else
  std::_Exit(0);
#endif
}


void WaitRetrace()
{
#ifdef _soft
  std::int32_t bv = false;
  if (DirectActive)
    while (!bv)  lpDD->GetVerticalBlankStatus(&bv);
#endif
}


void SetFullScreen()
{
#ifdef _soft
  HRESULT res = DD_OK;
#endif

  if (!DirectActive) return;
#ifndef _gl
  if (HARD3D) return;
#endif
  if (!_GameState) return;

  GameDisplay::ToggleWindowMode(DisplayConfiguration);
  SyncLegacyDisplayState();

  // Leaving exclusive fullscreen restores the desktop display mode.
  // (ChangeDisplaySettings is per-process, so exit also restores it, but
  // toggling back to windowed in-game needs the explicit restore.)
  if (!FULLSCREEN) {
    Platform::RestoreDesktopMode();
  }

#ifndef _gl
  if (lpDD) {
    if (FULLSCREEN)
      res = lpDD->SetDisplayMode(WinW, WinH, 16);
    else
      res = lpDD->RestoreDisplayMode();

    if (res != DD_OK) {
      snprintf(logt, sizeof(logt), "DDRAW: Error set video mode %dx%d\n", WinW, WinH);
      PrintLog(logt);
    }

    lpVideoRAM = 0;
  }
#endif

#ifdef _WIN32
  SetVideoMode(WinW, WinH);
#else
  SetVideoMode(DisplayConfiguration.size.width, DisplayConfiguration.size.height);
#endif

#ifdef _gl
  if (g_GLRenderer)
    g_GLRenderer->SetVideoMode(WinW, WinH);
#endif
#ifdef _soft
  if (g_SoftRenderer)
    g_SoftRenderer->SetVideoMode(WinW, WinH);
#endif

  ResetMousePos();
}



void Wait(int time)
{
  unsigned int t = Platform::Milliseconds() + time;
  while (t>Platform::Milliseconds()) ;
}


//#define loadww 210
//#define loadwh 106
char loadtxt[128];
TPicture LoadWall;

void UpdateLoadingWindow()
{

#ifdef _WIN32
  HBITMAP hbmpOld = reinterpret_cast<HBITMAP>(SelectObject(hdcCMain, hbmpVideoBuf));
  HFONT   hfntOld = reinterpret_cast<HFONT>(SelectObject(hdcCMain, fnt_Small));

#endif

  for (int y=0; y<LoadWall.H/2; y++)
    memcpy( static_cast<std::uint16_t*>(lpVideoBuf) + y*VideoPitch,
            LoadWall.lpImage.get()  + y*LoadWall.W,
            LoadWall.W*2);

  if (LoadCount)
    for (int y=0; y<LoadWall.H/2; y++)
      memcpy( static_cast<std::uint16_t*>(lpVideoBuf) + y*VideoPitch,
              LoadWall.lpImage.get()  + (y+LoadWall.H/2)*LoadWall.W,
              (LoadWall.W*LoadCount/8)*2);

  //FillMemory(lpVideoBuf, 1024*loadwh*2, 1);
  /*
      SetBkMode(hdcCMain, TRANSPARENT);
  	SetTextColor(hdcCMain, 0x000000);
      TextOut(hdcCMain, 19, LoadWall.H/2-22, loadtxt, strlen(loadtxt) );
  	SetTextColor(hdcCMain, 0xB0B070);
      TextOut(hdcCMain, 18, LoadWall.H/2-23, loadtxt, strlen(loadtxt) );
  */
#ifdef _WIN32
  BitBlt(hdcMain,0,0,LoadWall.W,LoadWall.H/2, hdcCMain,0,0, SRCCOPY);

  SelectObject(hdcCMain,hfntOld);
  SelectObject(hdcCMain,hbmpOld);
#else
  if (g_GLRenderer) g_GLRenderer->PresentLoading(static_cast<std::uint16_t*>(lpVideoBuf), LoadWall.W, LoadWall.H/2, VideoPitch);
#endif

}





void StartLoading()
{
  LoadPictureTGA(LoadWall,   "HUNTDAT\\MENU\\loading.tga");
#ifndef _WIN32
  // Loading art may exceed a low-resolution game buffer. Keep game dimensions
  // intact; its next SetVideoMode call restores the gameplay allocation.
  CreateVideoDIB((std::max)(WinW, LoadWall.W), (std::max)(WinH, LoadWall.H / 2));
#endif
  Platform::ShowLoadingWindow({LoadWall.W, LoadWall.H / 2});
}

void EndLoading()
{
#ifdef _WIN32
  memset(lpVideoBuf, 0, VideoPitchB*768);
#else
  memset(lpVideoBuf, 0, static_cast<size_t>(VideoPitchB)*WinH);
#endif
  LoadWall.lpImage.reset();
}


void PrintLoad(char *t)
{
  strcpy(loadtxt, t);
  LoadCount++;
  UpdateLoadingWindow();

}


void SyncLegacyDisplayState()
{
  const auto legacy = GameDisplay::ProjectLegacyPresentation(DisplayConfiguration);
  WinW = legacy.size.width;
  WinH = legacy.size.height;
  FULLSCREEN = legacy.fullscreen;
  BORDERLESS = legacy.borderless;
}

static void UpdateDrawableGeometry()
{
  VideoPitch  = WinW;        // std::uint16_t index (pixels) for 16-bit video buffer
  VideoPitchB = WinW * 2;    // std::uint8_t index for 16-bit video buffer

  WinEX = WinW - 1;
  WinEY = WinH - 1;
  VideoCX = WinW / 2;
  VideoCY = WinH / 2;

  // Vertical FOV is the fundamental projection parameter. CameraH is
  // derived from VideoCY and the user-selected FOV (OptFov, in degrees).
  // CameraW is set equal to CameraH to keep square pixels: a square in
  // world-space appears square on screen regardless of aspect ratio.
  // The horizontal FOV then widens automatically as WinW/WinH grows
  // (e.g. ~80 deg at 4:3, ~120 deg at 16:9 with OptFov=62). This is
  // the same approach C1 uses and it avoids the horizontal stretching
  // that the old 4:3-hardcoded formula produced on 16:9 displays.
  CameraH = static_cast<float>(VideoCY) * FovScaleFromDegrees(OptFov);
  CameraW = CameraH;

  LoDetailSky =(WinW>400);
#if defined(_gl) && !defined(_WIN32)
  // All Linux publication paths, including topology fallback, update viewport
  // together with CPU pitch/centers. No later size comparison can bypass this.
  if (g_GLRenderer) g_GLRenderer->SetVideoMode(WinW, WinH);
#endif
}

#ifndef _WIN32
static GameDisplay::DisplayRecovery displayRecovery;
static bool displayConfigured = false;
static bool drawableSuspended = false;

void ObserveDisplayEvent(const Platform::Event& event)
{
  displayRecovery.Observe(event, Platform::Milliseconds());
}

// No native reconfiguration here: resize/scale events update CPU storage,
// viewport, readback and camera dimensions together before any frame renders.
static bool SynchronizeDrawable(const Platform::WindowState& window)
{
  if (!GameDisplay::UsableWindow(window)) return false;
  if (window.pixels.width != WinW || window.pixels.height != WinH) {
    LOG_INFO("Drawable changed: logical=%dx%d pixels=%dx%d requested=%dx%d",
             window.logical.width, window.logical.height, window.pixels.width, window.pixels.height,
             DisplayConfiguration.size.width, DisplayConfiguration.size.height);
    WinW = window.pixels.width;
    WinH = window.pixels.height;
    CreateVideoDIB(WinW, WinH);
    UpdateDrawableGeometry();
    ResetMousePos();
  }
  return true;
}

bool ServiceDisplayChanges()
{
  if (!displayConfigured) return true; // Loading has its own dimensions.
  auto window = Platform::QueryWindowState();
  if (window.minimized) {
    if (!drawableSuspended) CaptureMouse(false);
    drawableSuspended = true;
    return false;
  }
  if (displayRecovery.Ready(Platform::Milliseconds())) {
    const auto catalog = Platform::QueryDisplayCatalog();
    if (displayRecovery.Resolve(DisplayConfiguration, catalog, window) && !catalog.displays.empty()) {
      LOG_INFO("Display topology recovery: reapplying retained request with fresh selection");
      CaptureMouse(false);
      SetVideoMode(DisplayConfiguration.size.width, DisplayConfiguration.size.height);
      window = Platform::QueryWindowState();
      // Self-generated mode/layout events must not cause an endless retry if
      // the window manager rejects recovery. Retry only a distinct topology
      // or the user's next explicit mode request.
      displayRecovery.Recovered(Platform::QueryDisplayCatalog());
      CaptureMouse(blActive && _GameState && !IsPaused());
    }
  }
  if (!GameDisplay::UsableWindow(window)) {
    if (!drawableSuspended) {
      LOG_WARN("Drawable suspended: minimized=%d pixels=%dx%d", window.minimized, window.pixels.width, window.pixels.height);
      CaptureMouse(false);
      drawableSuspended = true;
    }
    return false;
  }
  if (!window.reachable) {
    if (!drawableSuspended) CaptureMouse(false);
    drawableSuspended = true;
    return false;
  }
  const bool usable = SynchronizeDrawable(window);
  if (usable && drawableSuspended) {
    CaptureMouse(blActive && _GameState && !IsPaused());
    drawableSuspended = false;
  }
  return usable;
}

// Focus/context restoration retains the user's current placement and dimensions.
// Explicit Alt+Enter and topology recovery still apply the requested mode.
void RestoreGameDisplay()
{
  if (!displayConfigured)
    SetVideoMode(DisplayConfiguration.size.width, DisplayConfiguration.size.height);
  else
    SynchronizeDrawable(Platform::QueryWindowState());
}
#endif

void SetVideoMode(int W, int H)
{
#ifndef _WIN32
  const Platform::Size priorPixels{WinW, WinH};
#endif
  DisplayConfiguration.size = {W, H};
  SyncLegacyDisplayState();

  // Reallocate the back-buffer DIB to match the new WinW/WinH so the
  // GDI pitch matches the runtime VideoPitchB. Required for widescreen
  // support; before this the DIB was always 1024x768 and the renderer
  // assumed a 2048-byte stride. No-op for the very first call (when
  // hwndMain is not yet created) — that path uses the old hardcoded
  // 1024x768 DIB which is fine because the initial render is the
  // loading screen only.
#ifdef _WIN32
  if (Platform::HasGameWindow()) CreateVideoDIB(WinW, WinH);
#endif

  const auto mode = DisplayConfiguration.mode;
  std::optional<Platform::DisplayMode> exclusiveMode;
  std::optional<Platform::DisplayTarget> target;
  GameDisplay::DisplaySelection appliedSelection;
  const bool wantsRefresh = mode == Platform::WindowMode::Exclusive && Platform::HasRefresh(DisplayConfiguration.refresh);
  const bool wantsMonitor = DisplayConfiguration.monitor.kind != GameDisplay::MonitorPreferenceKind::Primary;
  if ((wantsMonitor || wantsRefresh) && Platform::HasGameWindow()) {
    // One snapshot for presentation AND refresh selection. ResolutionList and
    // OptRes keep their primary/default compatibility projection unchanged.
    const auto catalog = Platform::QueryDisplayCatalog();
    const auto selected = GameDisplay::SelectMonitor(catalog, DisplayConfiguration.monitor, {W, H},
        wantsRefresh ? DisplayConfiguration.refresh : Platform::RefreshRate{});
    appliedSelection = selected;
    target = selected.target;
    exclusiveMode = selected.exclusiveMode;
    if (wantsMonitor) {
      if (selected.fallback != GameDisplay::DisplayFallback::None)
        LOG_WARN("Display preference: %s; using primary/default display%s",
                 GameDisplay::DisplayFallbackReason(selected.fallback),
                 DisplayConfiguration.monitor.kind == GameDisplay::MonitorPreferenceKind::Identity ||
                 selected.fallback == GameDisplay::DisplayFallback::AmbiguousBounds ? " and automatic refresh" : "");
      if (selected.index) {
        LOG_INFO("Display preference resolved to %s catalog index %zu",
                 selected.index == catalog.primaryDisplay ? "primary" : "secondary", *selected.index);
        const auto& bounds = catalog.displays[*selected.index].bounds;
        if (bounds) LOG_INFO("Runtime display bounds: (%d,%d) %dx%d", bounds->origin.x, bounds->origin.y,
                             bounds->size.width, bounds->size.height);
      }
    }
    if (wantsRefresh && !exclusiveMode && selected.fallback != GameDisplay::DisplayFallback::AmbiguousBounds)
      LOG_WARN("Exclusive %dx%d refresh %u/%u unavailable on selected display; using automatic refresh",
               W, H, DisplayConfiguration.refresh.numerator, DisplayConfiguration.refresh.denominator);
  }
  Platform::ConfigureGameWindow(mode, {W, H}, {VideoCX, VideoCY}, exclusiveMode, target);

  // Sync WinW/WinH and all derived values to the ACTUAL client area the OS
  // gave us. AdjustWindowRect predicts the frame chrome, but the real chrome
  // (title bar height, border width, DPI scale) can differ slightly, and at
  // resolutions that match or exceed the desktop the window can also be
  // clamped by Windows. If we kept WinW/WinH at the requested size while the
  // client area is smaller, the DIB and the OpenGL viewport would be larger
  // than the visible area and the top/bottom of the HUD would be clipped
  // (e.g. ammo counter hidden behind the title bar at 2560x1440 windowed on
  // a 2560x1440 desktop).
#ifdef _WIN32
  if (Platform::HasGameWindow()) {
    const auto client = Platform::ClientSize();
    int actualW = client.width;
    int actualH = client.height;
    if (actualW > 0 && actualH > 0 &&
        (actualW != WinW || actualH != WinH)) {
      char logt[128];
      snprintf(logt, sizeof(logt), "Client area adjusted: requested %dx%d, actual %dx%d\n",
               WinW, WinH, actualW, actualH);
      PrintLog(logt);
      GameDisplay::ApplyClientSize(DisplayConfiguration, {actualW, actualH});
      SyncLegacyDisplayState();
      // DIB was allocated at the old WinW/WinH; reallocate to match the
      // actual client area so GDI TextOut/DrawPicture don't write past the
      // visible region.
      CreateVideoDIB(WinW, WinH);
    }
  }

#else
  if (Platform::HasGameWindow()) {
    const auto client = Platform::ClientSize();
    const auto safe = GameDisplay::UsableDrawable(client) ? client :
        GameDisplay::UsableDrawable(priorPixels) ? priorPixels : Platform::Size{800, 600};
    WinW = safe.width;
    WinH = safe.height;
  }
#endif

#ifndef _WIN32
  if (Platform::HasGameWindow() && GameDisplay::UsableDrawable({WinW, WinH})) CreateVideoDIB(WinW, WinH);
#endif
  UpdateDrawableGeometry();
#ifdef _WIN32
  LoDetailSky = (W > 400);
#else
  displayRecovery.Applied(DisplayConfiguration, appliedSelection);
  displayConfigured = true;
#endif
  Platform::HideArrowCursor();
}


