// ==========================================================================
// TerrainQueries.cpp
// ==========================================================================

#include "Hunt.h"

float GetLandOH(int x, int y)
{
  return static_cast<float>((HMapO[y][x])) * ctHScale;
}

float GetLandOUH(int x, int y)
{
  if (FMap[y][x] & fmReverse)
    return static_cast<float>((static_cast<int>((HMap[y][x+1]+HMap[y+1][x]))/2.f))*ctHScale;
  else
    return static_cast<float>((static_cast<int>((HMap[y][x]+HMap[y+1][x+1]))/2.f))*ctHScale;
}

float GetLandUpH(float x, float y)
{

  int CX = static_cast<int>(x) / 256;
  int CY = static_cast<int>(y) / 256;

  if (!(FMap[CY][CX] & fmWaterA)) return GetLandH(x,y);

  return static_cast<float>((WaterList[ WMap[CY][CX] ].wlevel * ctHScale));

}

bool waterNear(float x, float y, float maxDist)
{
	for (int varX = -maxDist; varX <= maxDist; varX += maxDist) {
		for (int varY = -maxDist; varY <= maxDist; varY += maxDist) {
			if ((FMap[varY / 256][varX / 256] & fmWaterA)) return true;

		}
	}
	//if ((FMap[static_cast<int>(y) / 256][static_cast<int>(x) / 256] & fmWaterA)) return true;
	return false;
}

float GetLandH(float x, float y)
{
  int CX = static_cast<int>(x) / 256;
  int CY = static_cast<int>(y) / 256;

  int dx = static_cast<int>(x) % 256;
  int dy = static_cast<int>(y) % 256;

  int h1 = HMap[CY][CX];
  int h2 = HMap[CY][CX+1];
  int h3 = HMap[CY+1][CX+1];
  int h4 = HMap[CY+1][CX];


  if (FMap[CY][CX] & fmReverse)
  {
    if (256-dx>dy) h3 = h2+h4-h1;
    else h1 = h2+h4-h3;
  }
  else
  {
    if (dx>dy) h4 = h1+h3-h2;
    else h2 = h1+h3-h4;
  }

  float h = static_cast<float>((h1   * (256-dx) + h2 * dx)) * (256-dy) +
            (h4   * (256-dx) + h3 * dx) * dy;

  return  (h / 256.f / 256.f) * ctHScale;
}

float GetLandLt(float x, float y)
{
  int CX = static_cast<int>(x) / 256;
  int CY = static_cast<int>(y) / 256;

  int dx = static_cast<int>(x) % 256;
  int dy = static_cast<int>(y) % 256;

  int h1 = LMap[CY][CX];
  int h2 = LMap[CY][CX+1];
  int h3 = LMap[CY+1][CX+1];
  int h4 = LMap[CY+1][CX];

  float h = static_cast<float>((h1   * (256-dx) + h2 * dx)) * (256-dy) +
            (h4   * (256-dx) + h3 * dx) * dy;

  return  (h / 256.f / 256.f);
}

float GetLandLt2(float x, float y)
{
  int CX = (static_cast<int>(x) / 512)*2 - CCX;
  int CY = (static_cast<int>(y) / 512)*2 - CCY;

  int dx = static_cast<int>(x) % 512;
  int dy = static_cast<int>(y) % 512;

  int h1 = VMap[CY+kViewGridCenter][CX+kViewGridCenter].Light;
  int h2 = VMap[CY+kViewGridCenter][CX+2+kViewGridCenter].Light;
  int h3 = VMap[CY+2+kViewGridCenter][CX+2+kViewGridCenter].Light;
  int h4 = VMap[CY+2+kViewGridCenter][CX+kViewGridCenter].Light;

  float h = static_cast<float>((h1   * (512-dx) + h2 * dx)) * (512-dy) +
            (h4   * (512-dx) + h3 * dx) * dy;

  return  (h / 512.f / 512.f);
}

void CalcModelGroundLight(TModel *mptr, float x0, float z0, int FI)
{
  float ca = cos(FI * pi / 2);
  float sa = sin(FI * pi / 2);
  for (int v=0; v<mptr->VCount; v++)
  {
    float x = mptr->gVertex[v].x * ca + mptr->gVertex[v].z * sa + x0;
    float z = mptr->gVertex[v].z * ca - mptr->gVertex[v].x * sa + z0;
    mptr->VLight[0][v] = GetLandLt2(x, z) - 128;
  }
}

std::int32_t PointOnBound(float &H, float px, float py, float cx, float cy, float oy, TBound *bound, int angle)
{
  px-=cx;
  py-=cy;

  float ca = static_cast<float>(cos(angle*pi / 2.f));
  float sa = static_cast<float>(sin(angle*pi / 2.f));

  std::int32_t _on = false;
  H=-1000;

  for (int o=0; o<8; o++)
  {

    if (bound[o].a<0) continue;
    if (bound[o].y2 + oy > PlayerY + 128) continue;

    float a,b;
    float ccx = bound[o].cx*ca + bound[o].cy*sa;
    float ccy = bound[o].cy*ca - bound[o].cx*sa;

    if (angle & 1)
    {
      a = bound[o].b;
      b = bound[o].a;
    }
    else
    {
      a = bound[o].a;
      b = bound[o].b;
    }

    if ( ( fabs(px - ccx) < a) &&  (fabs(py - ccy) < b) )
    {
      _on=true;
      if (H < bound[o].y2) H = bound[o].y2;
    }
  }

  return _on;
}

std::int32_t PointUnBound(float &H, float px, float py, float cx, float cy, float oy, TBound *bound, int angle)
{
  px-=cx;
  py-=cy;

  float ca = static_cast<float>(cos(angle*pi / 2.f));
  float sa = static_cast<float>(sin(angle*pi / 2.f));

  std::int32_t _on = false;
  H=+1000;

  for (int o=0; o<8; o++)
  {

    if (bound[o].a<0) continue;
    if (bound[o].y1 + oy < PlayerY + 128) continue;

    float a,b;
    float ccx = bound[o].cx*ca + bound[o].cy*sa;
    float ccy = bound[o].cy*ca - bound[o].cx*sa;

    if (angle & 1)
    {
      a = bound[o].b;
      b = bound[o].a;
    }
    else
    {
      a = bound[o].a;
      b = bound[o].b;
    }

    if ( ( fabs(px - ccx) < a) &&  (fabs(py - ccy) < b) )
    {
      _on=true;
      if (H > bound[o].y1) H = bound[o].y1;
    }
  }

  return _on;
}

float GetLandCeilH(float CameraX, float CameraZ)
{
  float h,hh;

  h = GetLandH(CameraX, CameraZ) + 20480;

  int ccx = static_cast<int>(CameraX) / 256;
  int ccz = static_cast<int>(CameraZ) / 256;

  for (int z=-4; z<=4; z++)
    for (int x=-4; x<=4; x++)
      if (OMap[ccz+z][ccx+x]!=255)
      {
        int ob = OMap[ccz+z][ccx+x];

        float CR = static_cast<float>(MObjects[ob].info.Radius) - 1.f;

        float oz = (ccz+z) * 256.f + 128.f;
        float ox = (ccx+x) * 256.f + 128.f;

        float LandY = GetLandOH(ccx+x, ccz+z);

        if (!(MObjects[ob].info.flags & ofBOUND))
        {
          if (MObjects[ob].info.YLo + LandY > h) continue;
          if (MObjects[ob].info.YLo + LandY < PlayerY+100) continue;
        }

        float r = CR+1;

        if (MObjects[ob].info.flags & ofBOUND)
        {
          float hh;
          if (PointUnBound(hh, CameraX, CameraZ, ox, oz, LandY, MObjects[ob].bound, ((FMap[ccz+z][ccx+x] >> 2) & 3)  ) )
            if (h > LandY + hh) h = LandY + hh;
        }
        else
        {
          if (MObjects[ob].info.flags & ofCIRCLE)
            r = static_cast<float>(sqrt( (ox-CameraX)*(ox-CameraX) + (oz-CameraZ)*(oz-CameraZ) ));
          else
            r = static_cast<float>(MAX( fabs(ox-CameraX), fabs(oz-CameraZ) ));

          if (r<CR) h = MObjects[ob].info.YLo + LandY;
        }

      }
  return h;
}

float GetLandQH(float CameraX, float CameraZ)
{
  float h,hh;

  h = GetLandH(CameraX, CameraZ);
  hh = GetLandH(CameraX-90.f, CameraZ-90.f);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX+90.f, CameraZ-90.f);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX-90.f, CameraZ+90.f);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX+90.f, CameraZ+90.f);
  if (hh>h) h=hh;

  hh = GetLandH(CameraX+128.f, CameraZ);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX-128.f, CameraZ);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX, CameraZ+128.f);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX, CameraZ-128.f);
  if (hh>h) h=hh;

  int ccx = static_cast<int>(CameraX) / 256;
  int ccz = static_cast<int>(CameraZ) / 256;

  for (int z=-4; z<=4; z++)
    for (int x=-4; x<=4; x++)
      if (OMap[ccz+z][ccx+x]!=255)
      {
        int ob = OMap[ccz+z][ccx+x];

        float CR = static_cast<float>(MObjects[ob].info.Radius) - 1.f;

        float oz = (ccz+z) * 256.f + 128.f;
        float ox = (ccx+x) * 256.f + 128.f;

        float LandY = GetLandOH(ccx+x, ccz+z);

        if (!(MObjects[ob].info.flags & ofBOUND))
        {
          if (MObjects[ob].info.YHi + LandY < h) continue;
          if (MObjects[ob].info.YHi + LandY > PlayerY+128) continue;
          //if (MObjects[ob].info.YLo + LandY > PlayerY+256) continue;
        }

        float r = CR+1;

        if (MObjects[ob].info.flags & ofBOUND)
        {
          float hh;
          if (PointOnBound(hh, CameraX, CameraZ, ox, oz, LandY, MObjects[ob].bound, ((FMap[ccz+z][ccx+x] >> 2) & 3)  ) )
            if (h < LandY + hh) h = LandY + hh;
        }
        else
        {
          if (MObjects[ob].info.flags & ofCIRCLE)
            r = static_cast<float>(sqrt( (ox-CameraX)*(ox-CameraX) + (oz-CameraZ)*(oz-CameraZ) ));
          else
            r = static_cast<float>(MAX( fabs(ox-CameraX), fabs(oz-CameraZ) ));

          if (r<CR) h = MObjects[ob].info.YHi + LandY;
        }

      }
  return h;
}

float GetLandHObj(float CameraX, float CameraZ)
{
  float h;

  h = 0;

  int ccx = static_cast<int>(CameraX) / 256;
  int ccz = static_cast<int>(CameraZ) / 256;

  for (int z=-3; z<=3; z++)
    for (int x=-3; x<=3; x++)
      if (OMap[ccz+z][ccx+x]!=255)
      {
        int ob = OMap[ccz+z][ccx+x];
        float CR = static_cast<float>(MObjects[ob].info.Radius) - 1.f;

        float oz = (ccz+z) * 256.f + 128.f;
        float ox = (ccx+x) * 256.f + 128.f;

        if (MObjects[ob].info.YHi + GetLandOH(ccx+x, ccz+z) < h) continue;
        if (MObjects[ob].info.YLo + GetLandOH(ccx+x, ccz+z) > PlayerY+256) continue;
        float r;
        if (MObjects[ob].info.flags & ofCIRCLE)
          r = static_cast<float>(sqrt( (ox-CameraX)*(ox-CameraX) + (oz-CameraZ)*(oz-CameraZ) ));
        else
          r = static_cast<float>(MAX( fabs(ox-CameraX), fabs(oz-CameraZ) ));

        if (r<CR)
          h = MObjects[ob].info.YHi + GetLandOH(ccx+x, ccz+z);
      }

  return h;
}

float GetLandQHNoObj(float CameraX, float CameraZ)
{
  float h,hh;

  h = GetLandH(CameraX, CameraZ);
  hh = GetLandH(CameraX-90.f, CameraZ-90.f);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX+90.f, CameraZ-90.f);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX-90.f, CameraZ+90.f);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX+90.f, CameraZ+90.f);
  if (hh>h) h=hh;

  hh = GetLandH(CameraX+128.f, CameraZ);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX-128.f, CameraZ);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX, CameraZ+128.f);
  if (hh>h) h=hh;
  hh = GetLandH(CameraX, CameraZ-128.f);
  if (hh>h) h=hh;

  return h;
}