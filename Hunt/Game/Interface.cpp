#define INITGUID
#include "Hunt.h"
#include "Platform/Platform.h"
#ifdef _gl
#include "Renderer/GLRenderer.h"
#endif
#include "stdio.h"
#include "timeapi.h"

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

BOOL NEWPLAYER = false;

int  MapVKKey(int k)
{
  if (k==VK_LBUTTON) return 124;
  if (k==VK_RBUTTON) return 125;
  return MapVirtualKey(k, 0);
}




POINT p;
int OptMode = 0;
int OptLine = 0;


void wait_mouse_release()
{
  while (GetAsyncKeyState(VK_RBUTTON) & 0x80000000);
  while (GetAsyncKeyState(VK_LBUTTON) & 0x80000000);

}


int GetTextW(HDC hdc, LPSTR s)
{
  SIZE sz;
  GetTextExtentPoint(hdc, s, strlen(s), &sz);
  return sz.cx;
}

void PrintText(LPSTR s, int x, int y, int rgb)
{
  HBITMAP hbmpOld = reinterpret_cast<HBITMAP>(SelectObject(hdcCMain,hbmpVideoBuf));
  SetBkMode(hdcCMain, TRANSPARENT);

  SetTextColor(hdcCMain, 0x00000000);
  TextOut(hdcCMain, x+1, y+1, s, strlen(s));
  SetTextColor(hdcCMain, rgb);
  TextOut(hdcCMain, x, y, s, strlen(s));

  SelectObject(hdcCMain,hbmpOld);
}

void DoHalt(LPSTR Mess)
{

	LOG_ERROR("ABNORMAL_HALT: %s", Mess ? Mess : "");
	if (strlen(Mess))
	{
		PrintLog("ABNORMAL_HALT: ");
		PrintLog(Mess);
		PrintLog("\n");
		MessageBox(nullptr, Mess, "Carnivores Termination", IDOK | MB_SYSTEMMODAL | MB_ICONEXCLAMATION);
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
  EnableWindow(hwndMain, false);

  CloseLog();
  LogClose();
  TerminateProcess(GetCurrentProcess(), 0);
}

//For stopping the program before audio/3d hardware startup
void DoHalt2(LPSTR Mess)
{
	LOG_ERROR("ABNORMAL_HALT: %s", Mess ? Mess : "");
//	AudioStop();
//	Audio_Shutdown();

//	ShutDown3DHardware();
	EnableWindow(hwndMain, false);
	if (strlen(Mess))
	{
		PrintLog("ABNORMAL_HALT: ");
		PrintLog(Mess);
		PrintLog("\n");
		MessageBox(nullptr, Mess, "Carnivores Termination", IDOK | MB_SYSTEMMODAL | MB_ICONEXCLAMATION);
	}

	CloseLog();
	LogClose();
	TerminateProcess(GetCurrentProcess(), 0);
}


void WaitRetrace()
{
  BOOL bv = false;
  if (DirectActive)
    while (!bv)  lpDD->GetVerticalBlankStatus(&bv);
}


void SetFullScreen()
{
  HRESULT res = DD_OK;

  if (!DirectActive) return;
#ifndef _gl
  if (HARD3D) return;
#endif
  if (!_GameState) return;

  FULLSCREEN = !FULLSCREEN;
  if (BORDERLESS) {
    BORDERLESS = false;
    FULLSCREEN = true;
  }

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
      sprintf_s(logt, sizeof(logt), "DDRAW: Error set video mode %dx%d\n", WinW, WinH);
      PrintLog(logt);
    }

    lpVideoRAM = 0;
  }
#endif

  SetVideoMode(WinW, WinH);

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
  unsigned int t = timeGetTime() + time;
  while (t>timeGetTime()) ;
}


//#define loadww 210
//#define loadwh 106
char loadtxt[128];
TPicture LoadWall;

void UpdateLoadingWindow()
{

  HBITMAP hbmpOld = reinterpret_cast<HBITMAP>(SelectObject(hdcCMain, hbmpVideoBuf));
  HFONT   hfntOld = reinterpret_cast<HFONT>(SelectObject(hdcCMain, fnt_Small));


  for (int y=0; y<LoadWall.H/2; y++)
    memcpy( static_cast<WORD*>(lpVideoBuf) + y*VideoPitch,
            LoadWall.lpImage.get()  + y*LoadWall.W,
            LoadWall.W*2);

  if (LoadCount)
    for (int y=0; y<LoadWall.H/2; y++)
      memcpy( static_cast<WORD*>(lpVideoBuf) + y*VideoPitch,
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
  BitBlt(hdcMain,0,0,LoadWall.W,LoadWall.H/2, hdcCMain,0,0, SRCCOPY);

  SelectObject(hdcCMain,hfntOld);
  SelectObject(hdcCMain,hbmpOld);
}





void StartLoading()
{
  LoadPictureTGA(LoadWall,   "HUNTDAT\\MENU\\loading.tga");
  Platform::ShowLoadingWindow({LoadWall.W, LoadWall.H / 2});
}

void EndLoading()
{
  FillMemory(lpVideoBuf, VideoPitchB*768, 0);
  LoadWall.lpImage.reset();
}


void PrintLoad(char *t)
{
  strcpy(loadtxt, t);
  LoadCount++;
  UpdateLoadingWindow();

}


void SetVideoMode(int W, int H)
{
  WinW = W;
  WinH = H;

  // Reallocate the back-buffer DIB to match the new WinW/WinH so the
  // GDI pitch matches the runtime VideoPitchB. Required for widescreen
  // support; before this the DIB was always 1024x768 and the renderer
  // assumed a 2048-byte stride. No-op for the very first call (when
  // hwndMain is not yet created) — that path uses the old hardcoded
  // 1024x768 DIB which is fine because the initial render is the
  // loading screen only.
  if (Platform::HasGameWindow()) CreateVideoDIB(WinW, WinH);

  const auto mode = FULLSCREEN ? Platform::WindowMode::Exclusive :
                    BORDERLESS ? Platform::WindowMode::Borderless : Platform::WindowMode::Windowed;
  Platform::ConfigureGameWindow(mode, {W, H}, {VideoCX, VideoCY});

  // Sync WinW/WinH and all derived values to the ACTUAL client area the OS
  // gave us. AdjustWindowRect predicts the frame chrome, but the real chrome
  // (title bar height, border width, DPI scale) can differ slightly, and at
  // resolutions that match or exceed the desktop the window can also be
  // clamped by Windows. If we kept WinW/WinH at the requested size while the
  // client area is smaller, the DIB and the OpenGL viewport would be larger
  // than the visible area and the top/bottom of the HUD would be clipped
  // (e.g. ammo counter hidden behind the title bar at 2560x1440 windowed on
  // a 2560x1440 desktop).
  if (Platform::HasGameWindow()) {
    const auto client = Platform::ClientSize();
    int actualW = client.width;
    int actualH = client.height;
    if (actualW > 0 && actualH > 0 &&
        (actualW != WinW || actualH != WinH)) {
      char logt[128];
      sprintf_s(logt, sizeof(logt), "Client area adjusted: requested %dx%d, actual %dx%d\n",
               WinW, WinH, actualW, actualH);
      PrintLog(logt);
      WinW = actualW;
      WinH = actualH;
      // DIB was allocated at the old WinW/WinH; reallocate to match the
      // actual client area so GDI TextOut/DrawPicture don't write past the
      // visible region.
      CreateVideoDIB(WinW, WinH);
    }
  }

  VideoPitch  = WinW;        // WORD index (pixels) for 16-bit video buffer
  VideoPitchB = WinW * 2;    // byte index for 16-bit video buffer

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

  LoDetailSky =(W>400);
  Platform::HideArrowCursor();
}



