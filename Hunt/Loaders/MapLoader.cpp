// ==========================================================================
// MapLoader.cpp
// ==========================================================================

#include "Hunt.h"

// Forward declarations
int GetObjectH(int x, int y, int R);
int GetObjectHWater(int x, int y);

void CreateTMap()
{
  int x,y;
  LandingList.PCount = 0;
  for (y=0; y<ctMapSize; y++)
    for (x=0; x<ctMapSize; x++)
    {
      if (TMap1[y][x]==0xFFFF) TMap1[y][x] = 1;
      if (TMap2[y][x]==0xFFFF) TMap2[y][x] = 1;
    }

  /*
    for (y=1; y<ctMapSize-1; y++)
       for (x=1; x<ctMapSize-1; x++)
  		 if (!(FMap[y][x] & fmWater) ) {

  			 if (FMap[y  ][x+1] & fmWater) { FMap[y][x]|= fmWater2; WMap[y][x] = WMap[y  ][x+1];}
  			 if (FMap[y+1][x  ] & fmWater) { FMap[y][x]|= fmWater2; WMap[y][x] = WMap[y+1][x  ];}
  			 if (FMap[y  ][x-1] & fmWater) { FMap[y][x]|= fmWater2; WMap[y][x] = WMap[y  ][x-1];}
  			 if (FMap[y-1][x  ] & fmWater) { FMap[y][x]|= fmWater2; WMap[y][x] = WMap[y-1][x  ];}

  			 if (FMap[y][x] & fmWater2)
  			     if (HMap[y][x] > WaterList[WMap[y][x]].wlevel) HMap[y][x]=WaterList[WMap[y][x]].wlevel;
  		 }

    for (y=1; y<ctMapSize-1; y++)
       for (x=1; x<ctMapSize-1; x++)
  		 if (FMap[y][x] & fmWater2) {
  			 FMap[y][x]-=fmWater2;
  			 FMap[y][x]+=fmWater;
  		 }
  */

  for (y=1; y<ctMapSize-1; y++)
    for (x=1; x<ctMapSize-1; x++)
      if (!(FMap[y][x] & fmWater) )
      {

        if (FMap[y  ][x+1] & fmWater)
        {
          FMap[y][x]|= fmWater2;
          WMap[y][x] = WMap[y  ][x+1];
        }
        if (FMap[y+1][x  ] & fmWater)
        {
          FMap[y][x]|= fmWater2;
          WMap[y][x] = WMap[y+1][x  ];
        }
        if (FMap[y  ][x-1] & fmWater)
        {
          FMap[y][x]|= fmWater2;
          WMap[y][x] = WMap[y  ][x-1];
        }
        if (FMap[y-1][x  ] & fmWater)
        {
          FMap[y][x]|= fmWater2;
          WMap[y][x] = WMap[y-1][x  ];
        }

        std::int32_t l = true;

#ifdef _soft
        if (FMap[y][x] & fmWater2)
        {
          l = false;
          if (HMap[y][x] > WaterList[WMap[y][x]].wlevel) HMap[y][x]=WaterList[WMap[y][x]].wlevel;
          HMap[y][x]=WaterList[WMap[y][x]].wlevel;
        }
#endif

        if (FMap[y-1][x-1] & fmWater)
        {
          FMap[y][x]|= fmWater2;
          WMap[y][x] = WMap[y-1][x-1];
        }
        if (FMap[y-1][x+1] & fmWater)
        {
          FMap[y][x]|= fmWater2;
          WMap[y][x] = WMap[y-1][x+1];
        }
        if (FMap[y+1][x-1] & fmWater)
        {
          FMap[y][x]|= fmWater2;
          WMap[y][x] = WMap[y+1][x-1];
        }
        if (FMap[y+1][x+1] & fmWater)
        {
          FMap[y][x]|= fmWater2;
          WMap[y][x] = WMap[y+1][x+1];
        }

        if (l)
          if (FMap[y][x] & fmWater2)
            if (HMap[y][x] == WaterList[WMap[y][x]].wlevel) HMap[y][x]+=1;

        //if (FMap[y][x] & fmWater2)

      }

#ifdef _soft
  // Border clamp: HMap is [ctMapSize][ctMapSize], so the y+1/x+1 diagonal
  // reads run one row/column past the array at the far edge (stock-
  // reachable out-of-bounds read, masked in practice by adjacent globals).
  for (y=0; y<ctMapSize-1; y++)
    for (x=0; x<ctMapSize-1; x++ )
    {
      if( abs( HMap[y][x]-HMap[y+1][x+1] ) > abs( HMap[y+1][x]-HMap[y][x+1] ) )
        FMap[y][x] |= fmReverse;
      else
        FMap[y][x] &= ~fmReverse;
    }
#endif

  for (y=0; y<ctMapSize; y++)
    for (x=0; x<ctMapSize; x++)
    {

      if (!(FMap[y][x] & fmWaterA))
        WMap[y][x]=255;

#ifdef _soft
      if (MObjects[OMap[y][x]].info.flags & ofNOSOFT2)
        if ( (x+y) & 1 )
          OMap[y][x]=255;

      if (MObjects[OMap[y][x]].info.flags & ofNOSOFT)
        OMap[y][x]=255;
#endif

      if (OMap[y][x]==254)
      {
        constexpr int landingCapacity =
            static_cast<int>(sizeof(LandingList.list) / sizeof(LandingList.list[0]));
        if (LandingList.PCount >= landingCapacity)
          DoHalt("Map loading error: too many landing markers (max 64).");
        LandingList.list[LandingList.PCount].x = x;
        LandingList.list[LandingList.PCount].y = y;
        LandingList.PCount++;
		//MessageBox(hwndMain, "FOUND A LANDER!", "Woah wee", IDOK);
        OMap[y][x]=255;
      }

      int ob = OMap[y][x];
      if (ob == 255)
      {
        HMapO[y][x] = 0;
        continue;
      }

      //HMapO[y][x] = GetObjectH(x,y);
      if (MObjects[ob].info.flags & ofPLACEGROUND) HMapO[y][x] = GetObjectH(x,y, MObjects[ob].info.GrRad);
      //if (MObjects[ob].info.flags & ofPLACEWATER)  HMapO[y][x] = GetObjectHWater(x,y);

    }

  if (!LandingList.PCount && g_GameMode != GameMode::TrophyMode)
  {
	//MessageBox(hwndMain, "URRRR WHAT?", "Woah what the fuck", IDOK);
    LandingList.list[LandingList.PCount].x = 256;
    LandingList.list[LandingList.PCount].y = 256;
    LandingList.PCount=1;
  }

  /*
  if (g_GameMode == GameMode::TrophyMode)
  {
    LandingList.PCount = 0;
    for (x=0; x<6; x++)
    {
      LandingList.list[LandingList.PCount].x = 69 + x*3;
      LandingList.list[LandingList.PCount].y = 66;
      LandingList.PCount++;
    }

    for (y=0; y<6; y++)
    {
      LandingList.list[LandingList.PCount].x = 87;
      LandingList.list[LandingList.PCount].y = 69 + y*3;
      LandingList.PCount++;
    }

    for (x=0; x<6; x++)
    {
      LandingList.list[LandingList.PCount].x = 84 - x*3;
      LandingList.list[LandingList.PCount].y = 87;
      LandingList.PCount++;
    }

    for (y=0; y<6; y++)
    {
      LandingList.list[LandingList.PCount].x = 66;
      LandingList.list[LandingList.PCount].y = 84 - y*3;
      LandingList.PCount++;
    }
  }
  */

}