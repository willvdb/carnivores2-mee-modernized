// ==========================================================================
// SoftHUD.cpp — Software renderer HUD and 2D overlay rendering
//
// Split from the original monolithic RenderSoft.cpp.
// ==========================================================================

#include "Hunt.h"
#include "SoftInternal.h"
#include "Renderer/SoftRenderer.h"
#include "Renderer/UIText.h"

#ifdef _soft
void STTextOut(int x, int y, LPSTR t, int color)
{
  SetTextColor(hdcCMain, 0x00000000);
  TextOut(hdcCMain, x+1, y+1, t, strlen(t));
  SetTextColor(hdcCMain, color);
  TextOut(hdcCMain, x, y, t, strlen(t));
}

void ShowControlElements()
{

  HBITMAP hbmpOld = reinterpret_cast<HBITMAP>(SelectObject(hdcCMain, hbmpVideoBuf));

  char buf[128];

  SetBkMode(hdcCMain, TRANSPARENT);

  if (TIMER)
  {
    sprintf_s(buf, sizeof(buf),"msc: %d", TimeDt);
    STTextOut(WinEX-70, 10, buf, 0x0020A0A0);
    sprintf_s(buf, sizeof(buf),"polys: %d", dFacesCount);
    STTextOut(WinEX-90, 24, buf, 0x0020A0A0);
  }

  if (MessageList.timeleft)
  {
    if (RealTime>MessageList.timeleft) MessageList.timeleft = 0;
    STTextOut(10, 10, MessageList.mtext, 0x0020A0A0);
  }


  if (ExitTime)
  {
    int y = WinH / 3;
    sprintf_s(buf, sizeof(buf),"Preparing for evacuation...");
    STTextOut(VideoCX - GetTextW(hdcMain, buf)/2, y, buf, 0x0060C0D0);
    sprintf_s(buf, sizeof(buf),"%d seconds left.", 1 + ExitTime / 1000);
    STTextOut(VideoCX - GetTextW(hdcMain, buf)/2, y + 18, buf, 0x0060C0D0);
  }

  if (WaveNoteTime)
  {
	  int y = WinH / 3;
	  sprintf_s(buf, sizeof(buf), "Waves Survived: %i", SurvivalWave - 1);
	  STTextOut(VideoCX - GetTextW(hdcCMain, buf) / 2, y, buf, 0x0060C0D0);
  }

  SelectObject(hdcCMain, hbmpOld);

}



static void BlitPicture(int x, int y, int w, int h, const TPicture& pic)
{
  if (!lpVideoBuf || VideoPitch <= 0 || WinW <= 0 || WinH <= 0 ||
      !pic.lpImage || pic.W <= 0 || pic.H <= 0 || w <= 0 || h <= 0)
    return;

  WORD* dst = static_cast<WORD*>(lpVideoBuf);
  for (int yy = 0; yy < h; yy++)
  {
    const int dstY = y + yy;
    if (dstY < 0 || dstY >= WinH) continue;
    const int srcY = yy * pic.H / h;

    for (int xx = 0; xx < w; xx++)
    {
      const int dstX = x + xx;
      if (dstX < 0 || dstX >= WinW) continue;
      const int srcX = xx * pic.W / w;
      dst[dstY * VideoPitch + dstX] = pic.lpImage[srcY * pic.W + srcX];
    }
  }
}

void DrawPicture(int x, int y, TPicture &pic)
{
  BlitPicture(x, y, pic.W, pic.H, pic);
}

void DrawScaledPicture(int x, int y, int w, int h, TPicture &pic)
{
  BlitPicture(x, y, w, h, pic);
}

void DrawFlash(int x, int y, int w, int h, TPicture &pic)
{
  BlitPicture(x, y, w, h, pic);
}


int CircleCX, CircleCY;

void PutPixel(int x, int y)
{
  if (!lpVideoBuf || VideoPitch <= 0 || x < 0 || x >= WinW || y < 0 || y >= WinH) return;
  *(static_cast<WORD*>(lpVideoBuf) + (y*VideoPitch) + x) = 18<<5;
}

void Put8pix(int X,int Y)
{
  PutPixel(CircleCX + X, CircleCY + Y);
  PutPixel(CircleCX + X, CircleCY - Y);
  PutPixel(CircleCX - X, CircleCY + Y);
  PutPixel(CircleCX - X, CircleCY - Y);
  PutPixel(CircleCX + Y, CircleCY + X);
  PutPixel(CircleCX + Y, CircleCY - X);
  PutPixel(CircleCX - Y, CircleCY + X);
  PutPixel(CircleCX - Y, CircleCY - X);
}

void DrawCircle(int cx, int cy, int R)
{
  int d = 3 - (2 * R);
  int x = 0;
  int y = R;
  CircleCX=cx;
  CircleCY=cy;
  do
  {
    Put8pix(x,y);
    x++;
    if (d < 0) d = d + (x<<2) + 6;
    else
    {
      d = d + (x - y) * 4 + 10;
      y--;
    }
  }
  while (x<y);
  Put8pix(x,y);
}

void DrawBoxMystery(WORD *lfbPtr, int xx, int yy, WORD c)
{
	if (!lpVideoBuf || VideoPitch <= 0 || xx < 0 || xx + 3 >= WinW ||
		yy - 3 < 0 || yy + 3 >= WinH) return;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 1) = c;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 2) = c;
	yy++;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 1) = c;
	yy+=2;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 1) = c;
	yy -= 4;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 3) = c;
	yy --;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx) = c;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 3) = c;
	yy--;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 1) = c;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 2) = c;
}



void DrawBox(WORD *lfbPtr, int xx, int yy, WORD c)
{
	if (!lpVideoBuf || VideoPitch <= 0 || xx < 0 || xx + 1 >= WinW ||
		yy < 0 || yy + 1 >= WinH) return;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx) = c;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 1) = c;
	yy++;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx) = c;
	*(static_cast<WORD*>(lpVideoBuf) + yy *VideoPitch + xx + 1) = c;
}

void DrawHMap()
{


	if (g_GameMode == GameMode::SurvivalMode) return;

  //if (WinH < 280) return;
  DrawPicture(VideoCX-MapPic.W/2, VideoCY - MapPic.H/2, MapPic);
  int xx = VideoCX - 128 + (CCX>>2);
  int yy = VideoCY - 128 + (CCY>>2)+6;

  int px = xx;
  int py = yy;

  if (yy > 0 && yy < WinH && xx > 0 && xx < WinW)
  {
    DrawCircle(xx, yy, 17);
	DrawBox(static_cast<WORD*>(lpVideoBuf), xx, yy, 31 * VideoPitch);
  }

  float _sonarPos;
  if (g_GameMode == GameMode::SonarMode) {
	  _sonarPos = sonarPos;
	  sonarPos += TimeDt * 0.02 * cos((pi / 2)*(sonarPos / 41));
	  if (sonarPos > 38) sonarPos = 1;
	  DrawCircle(xx, yy, sonarPos);
  }
  
  for (int b = 0; b < bulletCh; b++) {
	  if (bullet[b].RTime) {
		  xx = VideoCX - 128 + static_cast<int>(bullet[b].a.x) / 1024;
		  yy = VideoCY - 128 + static_cast<int>(bullet[b].a.z) / 1024;
		  if (yy > 0 && yy < WinH && xx > 0 && xx < WinW)
			  DrawBox(static_cast<WORD*>(lpVideoBuf),xx, yy, WeapInfo[bullet[b].parent].radarColour555);
	  }
  }

    for (int c=0; c<ChCount; c++)
    {


			//if (! (TargetDino & (1<<Characters[c].AI)) ) continue;
			//if (Characters[c].AI > 0) continue;
		
		if (!DinoInfo[Characters[c].CType].onRadar && !Characters[c].RTime) continue;
		
		if (!Characters[c].Health && !Characters[c].RTime) continue;

		//if (!RadarMode && Characters[c].Clone != AI_HUNTDOG && !Characters[c].RTime) continue;
			xx = VideoCX - 128 + static_cast<int>(Characters[c].pos.x) / 1024;
			yy = VideoCY - 128 + static_cast<int>(Characters[c].pos.z) / 1024;
			if (yy <= 0 || yy >= WinH) continue;
			if (xx <= 0 || xx >= WinW) continue;

			if (Characters[c].Clone == AI_HUNTDOG) {
				DrawBox(static_cast<WORD*>(lpVideoBuf), xx, yy, DinoInfo[Characters[c].CType].radarColour555);//31*VideoPitch
			}
			else {
				if (RadarMode || Characters[c].RTime) {
					WORD *colour = &DinoInfo[Characters[c].CType].radarColour555;
					if (Characters[c].tracker >= 0) colour = &WeapInfo[Characters[c].tracker].radarColour555;

					if (DinoInfo[Characters[c].CType].Mystery) DrawBoxMystery(static_cast<WORD*>(lpVideoBuf), xx, yy, *colour);
					else DrawBox(static_cast<WORD*>(lpVideoBuf), xx, yy, *colour); //30<<5
				}

				if (g_GameMode == GameMode::SonarMode) {
					int dx, dz;
					dx = px - xx;
					dz = py - yy;
					int pd = static_cast<int>(sqrt(dx * dx + dz * dz));


					if (pd < 38) {
						if (pd >= _sonarPos && pd <= sonarPos) {
							Characters[c].showSonar = true;
							Characters[c].sonar.x = xx;
							Characters[c].sonar.y = yy;
							AddVoicev(fxBlip.length, fxBlip.lpData.data(), 256);
						}
					}
					else Characters[c].showSonar = false;

					if (Characters[c].showSonar && !Characters[c].RTime) {
						if (DinoInfo[Characters[c].CType].Mystery) DrawBoxMystery(static_cast<WORD*>(lpVideoBuf), Characters[c].sonar.x, Characters[c].sonar.y, DinoInfo[Characters[c].CType].radarColour555);
						else DrawBox(static_cast<WORD*>(lpVideoBuf), Characters[c].sonar.x, Characters[c].sonar.y, DinoInfo[Characters[c].CType].radarColour555);
					}
				}

			}

    }
}


void DrawSurvivalText(int x0, int y0)
{
    char tWaves[32], tHigh[32];
    sprintf_s(tWaves, sizeof(tWaves), "%i", SurvivalWave - 1);
    sprintf_s(tHigh,  sizeof(tHigh),  "%i", TrophyRoom2.survivalHighScore);

    const COLORREF kLabel = 0x00BFBFBF;
    const COLORREF kValue = 0x0000BFBF;

    const uitxt::Seg rowWaves[] = { { "Waves Survived: ", kLabel }, { tWaves, kValue } };
    const uitxt::Seg rowHigh[]  = { { "High Score: ",     kLabel }, { tHigh,  kValue } };

    const uitxt::Row rows[] = {
        { rowWaves, 2 },
        { rowHigh,  2 },
    };

    HBITMAP hbmpOld = reinterpret_cast<HBITMAP>(SelectObject(hdcCMain, hbmpVideoBuf));

    // exit_s.tga is 212x196; the two lines sit 26 art pixels apart.
    uitxt::DrawBox(CPUText::GameCanvas(), x0, y0,
                   /*padX*/ 40, /*padY*/ 98, /*step*/ 26,
                   /*maxW*/ 164, /*maxH*/ 88,
                   rows, 2);

    SelectObject(hdcCMain, hbmpOld);
}

void DrawScoreText(int x0, int y0) {
    char t[32];
    sprintf_s(t, sizeof(t), "%d", ScoreDisp);

    const COLORREF kLabel = 0x00BFBFBF;
    const COLORREF kValue = 0x0000BFBF;

    const uitxt::Seg segs[] = {
        { "Unclaimed Kill - Score Added: ", kLabel },
        { t, kValue },
    };
    const uitxt::Row rows[] = { { segs, 2 } };

    HBITMAP hbmpOld = reinterpret_cast<HBITMAP>(SelectObject(hdcCMain, hbmpVideoBuf));

    // score.tga is 210x42; its recessed panel is the strip around row 18.
    uitxt::DrawBox(CPUText::GameCanvas(), x0, y0,
                   /*padX*/ 14, /*padY*/ 18, /*step*/ 16,
                   /*maxW*/ 192, /*maxH*/ 16,
                   rows, 1);

    SelectObject(hdcCMain, hbmpOld);
}



void DrawTrophyText(int x0, int y0)
{
  const int   dtype = TrophyDisplayBody.ctype;
  const int   time  = TrophyDisplayBody.time;
  const int   date  = TrophyDisplayBody.date;
  const int   wep   = TrophyDisplayBody.weapon;
  const int   score = TrophyDisplayBody.score;
  const float scale = TrophyDisplayBody.scale;
  const float range = TrophyDisplayBody.range;

  char tWeight[32], tLength[32], tWeapon[64], tScore[32], tRange[32], tDate[32], tTime[32];

  if (OptSys) sprintf_s(tWeight, sizeof(tWeight), "%3.2ft", DinoInfo[dtype].Mass * scale * scale / 0.907f);
  else        sprintf_s(tWeight, sizeof(tWeight), "%3.2fT", DinoInfo[dtype].Mass * scale * scale);

  if (OptSys) sprintf_s(tLength, sizeof(tLength), "%3.2fft", DinoInfo[dtype].Length * scale / 0.3f);
  else        sprintf_s(tLength, sizeof(tLength), "%3.2fm", DinoInfo[dtype].Length * scale);

  sprintf_s(tWeapon, sizeof(tWeapon), "%s", WeapInfo[wep].Name);
  sprintf_s(tScore,  sizeof(tScore),  "%d", score);

  if (OptSys) sprintf_s(tRange, sizeof(tRange), "%3.1fft", range / 0.3f);
  else        sprintf_s(tRange, sizeof(tRange), "%3.1fm", range);

  if (OptSys) sprintf_s(tDate, sizeof(tDate), "%d.%d.%d", ((date >> 10) & 255), (date & 255), date >> 20);
  else        sprintf_s(tDate, sizeof(tDate), "%d.%d.%d", (date & 255), ((date >> 10) & 255), date >> 20);

  sprintf_s(tTime, sizeof(tTime), "%d:%02d", ((time >> 10) & 255), (time & 255));

  const COLORREF kLabel = 0x00BFBFBF;
  const COLORREF kValue = 0x0000BFBF;

  const uitxt::Seg rowName[]  = { { "Name: ",        kLabel }, { DinoInfo[dtype].Name, kValue } };
  const uitxt::Seg rowSize[]  = { { "Weight: ",      kLabel }, { tWeight,  kValue },
                                  { "Length: ",      kLabel }, { tLength,  kValue } };
  const uitxt::Seg rowGear[]  = { { "Weapon: ",      kLabel }, { tWeapon,  kValue },
                                  { "Score: ",       kLabel }, { tScore,   kValue } };
  const uitxt::Seg rowRange[] = { { "Range of kill: ", kLabel }, { tRange, kValue } };
  const uitxt::Seg rowWhen[]  = { { "Date: ",        kLabel }, { tDate,    kValue },
                                  { "Time: ",        kLabel }, { tTime,    kValue } };

  const uitxt::Row rows[] = {
      { rowName,  2 },
      { rowSize,  4 },
      { rowGear,  4 },
      { rowRange, 2 },
      { rowWhen,  4 },
  };

  HBITMAP hbmpOld = reinterpret_cast<HBITMAP>(SelectObject(hdcCMain, hbmpVideoBuf));

  // trophy.tga / collect.tga are 210x124; the recessed panel spans rows 22..98.
  uitxt::DrawBox(CPUText::GameCanvas(), x0, y0,
                 /*padX*/ 16, /*padY*/ 18, /*step*/ 16,
                 /*maxW*/ 190, /*maxH*/ 80,
                 rows, 5);

  SelectObject(hdcCMain, hbmpOld);
}



void Render_LifeInfo(int li)
{
  int x,y;

  HBITMAP hbmpOld = reinterpret_cast<HBITMAP>(SelectObject(hdcCMain, hbmpVideoBuf));
  HFONT oldfont = reinterpret_cast<HFONT>(SelectObject(hdcCMain, fnt_Small));

  int   ctype = Characters[li].CType;
  float  scale = Characters[li].scale;
  char t[32];

  x = VideoCX + WinW / 64;
  y = VideoCY + static_cast<int>((WinH / 6.8));

  STTextOut(x, y, DinoInfo[ctype].Name, 0x0000b000);

  if (OptSys) sprintf(t,"Weight: %3.2ft ", DinoInfo[ctype].Mass * scale * scale / 0.907);
  else        sprintf(t,"Weight: %3.2fT ", DinoInfo[ctype].Mass * scale * scale);
  STTextOut(x, y+16, t, 0x0000b000);

  int R  = static_cast<int>((VectorLength( SubVectors(Characters[li].pos, PlayerPos) )*3 / 64.f));
  if (OptSys) sprintf(t,"Distance: %dft ", R);
  else        sprintf(t,"Distance: %dm  ", R/3);

  STTextOut(x, y+32, t, 0x0000b000);

  SelectObject(hdcMain, oldfont);

  SelectObject(hdcCMain, oldfont);
  SelectObject(hdcCMain, hbmpOld);
}

void RenderHealthBar()
{
  if (MyHealth >= 100000) return;
  if (MyHealth == 000000) return;

  int L = WinW / 4;
  int x0 = WinW - (WinW / 20) - L;
  int y0 = WinH / 40;
  int G = (std::min)((MyHealth * 30 / 100000), 20);
  int R = (std::min)(((100000 - MyHealth) * 30 / 100000), 20);
  int HCOLOR = (G<<5) | (R<<10); // 555: G at bits 5-9, R at bits 10-14

  int L0 = (L * MyHealth) / 100000;
  int H = WinH / 200;

  FillMemory(static_cast<WORD*>(lpVideoBuf) + ((y0-1)*VideoPitch) + x0-1, L*2+4, 0);
  FillMemory(static_cast<WORD*>(lpVideoBuf) + ((y0+H+1)*VideoPitch) + x0-1, L*2+4, 0);
  for (int y=0; y<=H; y++)
  {
    *(static_cast<WORD*>(lpVideoBuf) + ((y0+y)*VideoPitch) + x0 - 1) = 0;
    *(static_cast<WORD*>(lpVideoBuf) + ((y0+y)*VideoPitch) + x0 + L) = 0;
    for (int x=0; x<L0; x++)
      *(static_cast<WORD*>(lpVideoBuf) + ((y0+y)*VideoPitch) + x0 + x) = HCOLOR;
  }
}


void Render_Cross(int sx, int sy)
{
  int w = WinW / 12;
  for (int x=-w+1; x<w; x++)
  {
    int offset = (sy*VideoPitch) + (sx+x);
	*(static_cast<WORD*>(lpVideoBuf) + offset) = WeapInfo[CurrentWeapon].crossColour565;
  }

  for (int y=-w+1; y<w; y++)
  {
    int offset = ((sy+y)*VideoPitch) + sx;
    *(static_cast<WORD*>(lpVideoBuf) + offset) = WeapInfo[CurrentWeapon].crossColour565;
  }
}

void Init3DHardware()
{
  PrintLog("\n");

  // Create the SoftRenderer instance so the IRenderer dispatch in
  // the game loop actually calls SoftRenderer::DrawFrame (which
  // delegates to ::DrawScene). Without this, g_SoftRenderer stays
  // nullptr and nothing 3D renders.
  if (g_SoftRenderer)
  {
    g_SoftRenderer->Shutdown();
    delete g_SoftRenderer;
    g_SoftRenderer = nullptr;
  }

  g_SoftRenderer = new SoftRenderer();
  if (!g_SoftRenderer->Initialize())
  {
    delete g_SoftRenderer;
    g_SoftRenderer = nullptr;
    DoHalt("Software renderer initialization failed.");
  }

  PrintLog("==Init Direct Draw==\n");
  HRESULT hres;

  hres = DirectDrawCreate( nullptr, &lpDD, nullptr );
  if( hres != DD_OK )
  {
    sprintf_s(logt, sizeof(logt), "DirectDrawCreate Error: %Xh\n", hres);
    PrintLog(logt);
    DoHalt("");
  }
  PrintLog("DirectDrawCreate: Ok\n");

  PrintLog("Direct Draw activated.\n");
  PrintLog("\n");
  DirectActive = true;
}



void Activate3DHardware()
{
  SetVideoMode(WinW, WinH);

  DWORD cl = DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN;
  if (!FULLSCREEN)
    cl = DDSCL_NORMAL;


  HRESULT hres = lpDD->SetCooperativeLevel( hwndMain, cl);
  if( hres != DD_OK )
  {
    sprintf_s(logt, sizeof(logt), "SetCooperativeLevel Error: %Xh\n", hres);
    PrintLog(logt);
    DoHalt("");
  }
  PrintLog("SetCooperativeLevel: Ok\n");

  if (FULLSCREEN)
    hres = lpDD->SetDisplayMode( WinW, WinH, 16);
  else
    hres = DD_OK;

  if (hres != DD_OK)
  {
    sprintf_s(logt, sizeof(logt), "DDRAW: Error set video mode %dx%d\n", WinW, WinH);
    PrintLog(logt);
  }
}

void ShutDown3DHardware()
{
  if (FULLSCREEN && lpDD)
    lpDD->RestoreDisplayMode();
  if (lpDD)
    lpDD->SetCooperativeLevel( hwndMain, DDSCL_NORMAL);

  // Destroy the SoftRenderer instance so the game can be re-initialised
  // (e.g. mid-game restart) without leaking the previous object.
  if (g_SoftRenderer)
  {
    g_SoftRenderer->Shutdown();
    delete g_SoftRenderer;
    g_SoftRenderer = nullptr;
  }
}

#endif // _soft
