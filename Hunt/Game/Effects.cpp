// ==========================================================================
// Effects.cpp
// ==========================================================================

#include "Hunt.h"
#include <array>
#include <cmath>

void PreCashGroundModel()
{
  SKYDTime = RealTime>>1;
  int x,y;

  int kx = SKYDTime & 255;
  int ky = SKYDTime & 255;
  int SKYDT = SKYDTime>>8;

  int VideoCX16 = VideoCX * 16;
  int VideoCY16 = VideoCY * 16;
  float CameraW16 = CameraW * 16;
  float CameraH16 = CameraH * 16;

  std::int32_t FogFound = false;
  NeedWater = false;

  static float waveCache[32][32];
  static int waveCacheRandomMap[32][32] = {};
  static int waveCacheTime = 0;
  static bool waveCacheReady = false;

  // §3.6: Multi-component wave cache for water surface displacement
  struct WaveCacheEntry { float wx[3], wy[3], wz[3]; };
  static WaveCacheEntry waveCacheSurface[32][32];
  static int waveCacheSurfaceTime = 0;
  static bool waveCacheSurfaceReady = false;

  if (!waveCacheReady || waveCacheTime != RealTime) {
    waveCacheTime = RealTime;
    for (int wy = 0; wy < 32; wy++) {
      for (int wx = 0; wx < 32; wx++) {
        waveCacheRandomMap[wy][wx] = RandomMap[wy][wx];
        waveCache[wy][wx] = static_cast<float>(sin(-pi/2 + waveCacheRandomMap[wy][wx] / 128 + static_cast<float>(RealTime) / 200.f));
      }
    }
    waveCacheReady = true;
  } else {
    for (int wy = 0; wy < 32; wy++) {
      for (int wx = 0; wx < 32; wx++) {
        if (waveCacheRandomMap[wy][wx] != RandomMap[wy][wx]) {
          waveCacheRandomMap[wy][wx] = RandomMap[wy][wx];
          waveCache[wy][wx] = static_cast<float>(sin(-pi/2 + waveCacheRandomMap[wy][wx] / 128 + static_cast<float>(RealTime) / 200.f));
        }
      }
    }
  }

  // §3.6: Pre-compute multi-component wave offsets for water surface
  // Invalidate cache when wave parameters change (for real-time debug tuning)
  static float lastWave1Amp = 0, lastWave2Amp = 0, lastWave3Amp = 0, lastWaveSpeed = 0;
  if (WWave1Amp != lastWave1Amp || WWave2Amp != lastWave2Amp ||
      WWave3Amp != lastWave3Amp || WWaveSpeed != lastWaveSpeed) {
    waveCacheSurfaceReady = false;
    lastWave1Amp = WWave1Amp;
    lastWave2Amp = WWave2Amp;
    lastWave3Amp = WWave3Amp;
    lastWaveSpeed = WWaveSpeed;
  }
  if (!waveCacheSurfaceReady || waveCacheSurfaceTime != RealTime) {
    waveCacheSurfaceTime = RealTime;
    float t = RealTime / 200.f * WWaveSpeed;
    // Y amplitude is ~80% of X/Z amplitude for a natural wave shape
    float amp1y = WWave1Amp * 0.8f;
    float amp2y = WWave2Amp * 0.8f;
    float amp3y = WWave3Amp * 0.8f;
    for (int wy = 0; wy < 32; wy++) {
      for (int wx = 0; wx < 32; wx++) {
        int r1 = RandomMap[wy][wx];
        int r2 = RandomMap[(wy + 11) & 31][(wx + 7) & 31];
        float px = static_cast<float>(wx) * 0.5f;
        float py = static_cast<float>(wy) * 0.5f;
        // Wave 1: primary swell — large slow heave
        waveCacheSurface[wy][wx].wx[0] = static_cast<float>(sin(px + py + t)) * WWave1Amp;
        waveCacheSurface[wy][wx].wy[0] = static_cast<float>(sin(px * 0.8f + py * 0.6f + t * 0.9f)) * amp1y;
        waveCacheSurface[wy][wx].wz[0] = static_cast<float>(sin(pi/2.f + px + py + t)) * WWave1Amp;
        // Wave 2: secondary cross-wave — medium amplitude, different direction
        waveCacheSurface[wy][wx].wx[1] = static_cast<float>(sin(px * 1.5f - py * 0.7f + t * 1.3f + r1 * 0.01f)) * WWave2Amp;
        waveCacheSurface[wy][wx].wy[1] = static_cast<float>(sin(px * 1.2f - py * 0.9f + t * 1.1f + r1 * 0.01f)) * amp2y;
        waveCacheSurface[wy][wx].wz[1] = static_cast<float>(sin(pi/3.f + px * 1.5f - py * 0.7f + t * 1.3f + r2 * 0.01f)) * WWave2Amp;
        // Wave 3: fine detail — small fast ripples
        waveCacheSurface[wy][wx].wx[2] = static_cast<float>(sin(px * 2.3f + py * 1.2f + t * 2.1f + r2 * 0.01f)) * WWave3Amp;
        waveCacheSurface[wy][wx].wy[2] = static_cast<float>(sin(px * 2.0f + py * 1.5f + t * 1.8f + r2 * 0.01f)) * amp3y;
        waveCacheSurface[wy][wx].wz[2] = static_cast<float>(sin(pi/4.f + px * 2.3f + py * 1.2f + t * 2.1f + r1 * 0.01f)) * WWave3Amp;
      }
    }
    waveCacheSurfaceReady = true;
  }

  MapMinY = 10241024;
  Vector3d rv;

  // Cache the camera basis used by RotateVector for the entire ground precache
  // sweep. The globals are fixed for this call; keeping them local removes
  // thousands of function calls/global loads from the terrain+water grid pass.
  const float localCa = ca;
  const float localSa = sa;
  const float localCb = cb;
  const float localSb = sb;
  auto rotateCached = [localCa, localSa, localCb, localSb](const Vector3d& in) {
    Vector3d out;
    const float vx = in.x * localCa + in.z * localSa;
    const float vz = in.z * localCa - in.x * localSa;
    out.x = vx;
    out.y = in.y * localCb - vz * localSb;
    out.z = vz * localCb + in.y * localSb;
    return out;
  };

  // The square sweep revisits every X column for every Y row. Cache the
  // wrapped map coordinate, camera-relative position and yaw products once
  // per axis; terrain points can then combine them without repeating the
  // same conversions and multiplies ~200K times. Animated water still uses
  // rotateCached() after applying its per-point wave offsets.
  std::array<int, kViewGridSize> mapX{};
  std::array<int, kViewGridSize> mapY{};
  std::array<float, kViewGridSize> worldX{};
  std::array<float, kViewGridSize> worldZ{};
  std::array<float, kViewGridSize> xCa{};
  std::array<float, kViewGridSize> xSa{};
  std::array<float, kViewGridSize> zCa{};
  std::array<float, kViewGridSize> zSa{};
  const int gridRadius = ctViewR + 3;
  for (int offset = -gridRadius; offset < gridRadius; ++offset) {
    const int grid = kViewGridCenter + offset;
    mapX[grid] = (CCX + offset) & 1023;
    mapY[grid] = (CCY + offset) & 1023;
    worldX[grid] = static_cast<float>(mapX[grid] * 256) - CameraX;
    worldZ[grid] = static_cast<float>(mapY[grid] * 256) - CameraZ;
    xCa[grid] = worldX[grid] * localCa;
    xSa[grid] = worldX[grid] * localSa;
    zCa[grid] = worldZ[grid] * localCa;
    zSa[grid] = worldZ[grid] * localSa;
  }

  // CalcFogLevel normally derives the FogsMap cell from a world-space point.
  // This loop already has the exact map coordinates, and each 512-unit fog
  // cell covers a 2x2 block of these 256-unit samples. Keep the last value
  // for each map-column so the four samples in a fog cell share one lookup.
  int fogCacheRow[512];
  unsigned char fogCacheValue[512];
  for (int i = 0; i < 512; ++i) fogCacheRow[i] = -1;
  auto getCachedFogIndex = [&](int fogCellX, int fogCellY) -> int {
    if (fogCacheRow[fogCellX] != fogCellY) {
      fogCacheRow[fogCellX] = fogCellY;
      fogCacheValue[fogCellX] = FogsMap[fogCellY][fogCellX];
    }
    return fogCacheValue[fogCellX];
  };


  for (y=-(ctViewR+3); y<(ctViewR+3); y++)
    for (x=-(ctViewR+3); x<(ctViewR+3); x++)
    {

      int r = MAX((MAX(y,-y)), (MAX(x,-x)));

      const int gridX = kViewGridCenter + x;
      const int gridY = kViewGridCenter + y;
      const int xx = mapX[gridX];
      const int yy = mapY[gridY];

      v[0].x = worldX[gridX];
      v[0].z = worldZ[gridY];
      v[0].y = static_cast<float>((static_cast<int>(HMap[yy][xx])))*ctHScale - CameraY;


//========= water section ===========//

      //if (RunMode)
      if ((FMap[yy][xx] & fmWaterA)>0)
      {
        rv = v[0];
        rv.y = WaterList[ WMap[yy][xx] ].wlevel*ctHScale - CameraY;

        // Use the per-RealTime wave offset cache built at the top of
        // PreCashGroundModel(). The cache reduces ~2,600 sin() calls per
        // frame to 1,024 per RealTime change (and zero in steady state).
        // The 4ab70c8 commit introduced the cache but never wired the
        // lookup; this change completes that fix.
        float wdelta = waveCache[yy & 31][xx & 31];

        if ( (FMap[yy][xx] & fmWater) && (r < ctViewR-4))
        {
          // §3.6: Multi-component wave displacement from cache
          const WaveCacheEntry& wc = waveCacheSurface[yy & 31][xx & 31];
          rv.x += wc.wx[0] + wc.wx[1] + wc.wx[2];
          rv.y += wc.wy[0] + wc.wy[1] + wc.wy[2];
          rv.z += wc.wz[0] + wc.wz[1] + wc.wz[2];
        }

        rv = rotateCached(rv);
        VMap2[kViewGridCenter + y][kViewGridCenter + x].v = rv;

        // FOVK-scaled like the terrain VMap test below: without it wide FOV
        // (FOVK < 1) drops visible side water vertices, leaving stale
        // Light/ALPHA/Fog (or zero ALPHA, which skips the tile) and possibly
        // a false NeedWater=false for the whole frame.
        if (fabs(rv.x * FOVK) > -rv.z + 1524)
        {
#if !defined(_gl)
          VMap2[kViewGridCenter + y][kViewGridCenter + x].DFlags = 128;
#endif
        }
        else
        {
          NeedWater = true;
          VMap2[kViewGridCenter + y][kViewGridCenter + x].Light = 168-static_cast<int>((wdelta*24));

          float Alpha;
          if (IsUnderwater())
          {
            Alpha =	160 - VectorLength(rv)* 160 / 220 / ctViewR;
            if (Alpha<10) Alpha=10;
          }
          else if (r < ctViewR+2)
          {
            int wi = WMap[yy][xx];
            Alpha = static_cast<float>(((WaterList[wi].wlevel - HMap[yy][xx])*2+4))*WaterList[wi].transp;
            Alpha+=VectorLength(rv) / 256;
            Alpha+=wdelta*2;
            if (Alpha<0) Alpha=0;
            Vector3d va = v[0];
            NormVector(va,1.0f);
            va.y=-va.y;
            if (va.y<0) va.y=0;
            Alpha*=6.f/(va.y+0.1f);
            if (Alpha>255) Alpha=255.f;
          }
          else Alpha = 255.f;

          VMap2[kViewGridCenter + y][kViewGridCenter + x].ALPHA=static_cast<int>(Alpha);

          // Water surface fog: apply depth-based fog when underwater
          // so the surface fades out at depth (matching terrain fog behavior).
          if (IsUnderwater()) {
              float extinction = 1.0f - std::exp(-CameraWaterDepthFactor * 3.5f);
              float fogAmount = extinction * 200.0f;
              VMap2[kViewGridCenter + y][kViewGridCenter + x].Fog = static_cast<int>(fogAmount);
          } else {
              VMap2[kViewGridCenter + y][kViewGridCenter + x].Fog = 0;
          }

#if !defined(_gl)
          if (rv.z>-256.0) VMap2[kViewGridCenter + y][kViewGridCenter + x].DFlags=128;
          else
          {
#ifdef _soft
            VMap2[kViewGridCenter + y][kViewGridCenter + x].scrx = VideoCX - static_cast<int>((rv.x / rv.z * CameraW));
            VMap2[kViewGridCenter + y][kViewGridCenter + x].scry = VideoCY + static_cast<int>((rv.y / rv.z * CameraH));

            int DF = 0;
            if (VMap2[kViewGridCenter + y][kViewGridCenter + x].scrx < 0)     DF+=1;
            if (VMap2[kViewGridCenter + y][kViewGridCenter + x].scrx > WinEX) DF+=2;
            if (VMap2[kViewGridCenter + y][kViewGridCenter + x].scry < 0)     DF+=4;
            if (VMap2[kViewGridCenter + y][kViewGridCenter + x].scry > WinEY) DF+=8;
#else
            VMap2[kViewGridCenter + y][kViewGridCenter + x].scrx = VideoCX16 - static_cast<int>((rv.x / rv.z * CameraW16));
            VMap2[kViewGridCenter + y][kViewGridCenter + x].scry = VideoCY16 + static_cast<int>((rv.y / rv.z * CameraH16));

            int DF = 0;
            if (VMap2[kViewGridCenter + y][kViewGridCenter + x].scrx < 0)        DF+=1;
            if (VMap2[kViewGridCenter + y][kViewGridCenter + x].scrx > WinEX*16) DF+=2;
            if (VMap2[kViewGridCenter + y][kViewGridCenter + x].scry < 0)        DF+=4;
            if (VMap2[kViewGridCenter + y][kViewGridCenter + x].scry > WinEY*16) DF+=8;
#endif
            VMap2[kViewGridCenter + y][kViewGridCenter + x].DFlags = DF;

          }
#endif
        }
      }



#ifdef _soft
#else
#endif

      const float terrainVz = zCa[gridY] - xSa[gridX];
      rv.x = xCa[gridX] + zSa[gridY];
      rv.y = v[0].y * localCb - terrainVz * localSb;
      rv.z = terrainVz * localCb + v[0].y * localSb;


      if (fabs(rv.x * FOVK) > -rv.z + 1600)
      {
        VMap[kViewGridCenter + y][kViewGridCenter + x].v = rv;
#if !defined(_gl)
        VMap[kViewGridCenter + y][kViewGridCenter + x].DFlags = 128;
#endif
        continue;
      }


      if (HARD3D)
        if (  ((FMap[yy][xx] & fmWater)==0) || IsUnderwater())
        {
          const int fogIndex = (FOGON && !IsUnderwater())
            ? getCachedFogIndex(
                (static_cast<int>((v[0].x + CameraX))) >> 9,
                (static_cast<int>((v[0].z + CameraZ))) >> 9)
            : -1;
          VMap[kViewGridCenter + y][kViewGridCenter + x].Fog = CalcFogLevel(v[0], fogIndex);
        }
        else
          VMap[kViewGridCenter + y][kViewGridCenter + x].Fog = 0;

#if !defined(_gl)
      VMap[kViewGridCenter + y][kViewGridCenter + x].ALPHA = 255;
#endif

      v[0]=rv;

      if (v[0].z<1024)
        if (FOGENABLE)
          if (FogsMap[yy>>1][xx>>1]) FogFound = true;

      VMap[kViewGridCenter + y][kViewGridCenter + x].v = v[0];

      int  DF = 0;
      int  db = 0;

      if (v[0].z<256)
      {
        if (Clouds)
        {
          int shmx = (xx + SKYDT) & 127;
          int shmy = (yy + SKYDT) & 127;

          int db1 = SkyMap[shmy * 128 + shmx ];
          int db2 = SkyMap[shmy * 128 + ((shmx+1) & 127) ];
          int db3 = SkyMap[((shmy+1) & 127) * 128 + shmx ];
          int db4 = SkyMap[((shmy+1) & 127) * 128 + ((shmx+1) & 127) ];
          db = (db1 * (256 - kx) + db2 * kx) * (256-ky) +
               (db3 * (256 - kx) + db4 * kx) * ky;
          db>>=17;
          db = db - 40;
          if (db<0) db=0;
          if (db>48) db=48;
        }

        int clt = LMap[yy][xx];
        clt= MAX(64, clt-db);
        VMap[kViewGridCenter + y][kViewGridCenter + x].Light = clt;
      }



#if !defined(_gl)
      if (v[0].z>-256.0) DF+=128;
      else
      {

#ifdef _soft
        VMap[kViewGridCenter + y][kViewGridCenter + x].scrx = VideoCX - static_cast<int>((v[0].x / v[0].z * CameraW));
        VMap[kViewGridCenter + y][kViewGridCenter + x].scry = VideoCY + static_cast<int>((v[0].y / v[0].z * CameraH));

        if (VMap[kViewGridCenter + y][kViewGridCenter + x].scrx < 0)        DF+=1;
        if (VMap[kViewGridCenter + y][kViewGridCenter + x].scrx > WinEX)    DF+=2;
        if (VMap[kViewGridCenter + y][kViewGridCenter + x].scry < 0)        DF+=4;
        if (VMap[kViewGridCenter + y][kViewGridCenter + x].scry > WinEY)    DF+=8;
#else
        VMap[kViewGridCenter + y][kViewGridCenter + x].scrx = VideoCX16 - static_cast<int>((v[0].x / v[0].z * CameraW16));
        VMap[kViewGridCenter + y][kViewGridCenter + x].scry = VideoCY16 + static_cast<int>((v[0].y / v[0].z * CameraH16));

        if (VMap[kViewGridCenter + y][kViewGridCenter + x].scrx < 0)        DF+=1;
        if (VMap[kViewGridCenter + y][kViewGridCenter + x].scrx > WinEX*16) DF+=2;
        if (VMap[kViewGridCenter + y][kViewGridCenter + x].scry < 0)        DF+=4;
        if (VMap[kViewGridCenter + y][kViewGridCenter + x].scry > WinEY*16) DF+=8;
#endif

      }

      VMap[kViewGridCenter + y][kViewGridCenter + x].DFlags = DF;
#endif
    }

  FOGON = FogFound || IsUnderwater();
}

void AddShadowCircle(int x, int y, int R, int D)
{
  if (IsUnderwater()) return;

  int cx = x / 256;
  int cy = y / 256;
  int cr = 1 + R / 256;
  for (int yy=-cr; yy<=cr; yy++)
    for (int xx=-cr; xx<=cr; xx++)
    {
      int tx = (cx+xx)*256;
      int ty = (cy+yy)*256;
      int r = static_cast<int>(sqrt(static_cast<float>(((tx-x)*(tx-x) + (ty-y)*(ty-y))) ));
      if (r>R) continue;
      VMap[cy + yy - CCY + kViewGridCenter][cx + xx - CCX + kViewGridCenter].Light-= D * (R-r) / R;
      if (VMap[cy + yy - CCY + kViewGridCenter][cx + xx - CCX + kViewGridCenter].Light < 32)
        VMap[cy + yy - CCY + kViewGridCenter][cx + xx - CCX + kViewGridCenter].Light = 32;
    }
}
