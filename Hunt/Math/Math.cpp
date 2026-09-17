#include "Hunt.h"
#include "Loaders/LoadValidate.h"

// Imported from Math/Vector.cpp
extern float PointToVectorDSq(Vector3d A, Vector3d AB, Vector3d C);
extern float FindVectorAlpha(float vx, float vy);

// LUT for cos(angle*pi/2), sin(angle*pi/2) with integer angle 0..3
namespace { constexpr float kRotCos[4] = { 1.0f, 0.0f, -1.0f, 0.0f }; constexpr float kRotSin[4] = { 0.0f, 1.0f, 0.0f, -1.0f }; }
// Octant LUT for cos(q*pi/4), sin(q*pi/4) with q=0..7
namespace { constexpr float kOctCos[8] = { 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f, 0.0f, 0.70710678f }; constexpr float kOctSin[8] = { 0.0f, 0.70710678f, 1.0f, 0.70710678f, 0.0f, -0.70710678f, -1.0f, -0.70710678f }; }

Vector3d TraceA, TraceNv;

int      TraceRes;



//====================================================























































void CheckBoundCollision(float &px, float &py, float cx, float cy, float oy, TBound *bound, int angle)

{

  float ppx=px-cx;

  float ppy=py-cy;



  float ca = kRotCos[angle & 3];
  float sa = kRotSin[angle & 3];

  float w,h;



  for (int o=0; o<8; o++)

  {



    if (bound[o].a<0) continue;

    if (bound[o].y2 + oy < PlayerY + 128) continue;

    if (bound[o].y1 + oy > PlayerY + 256) continue;



    float ccx = bound[o].cx*ca + bound[o].cy*sa;

    float ccy = bound[o].cy*ca - bound[o].cx*sa;



    if (angle & 1)

    {

      w = bound[o].b+2.f;

      h = bound[o].a+2.f;

    }

    else

    {

      w = bound[o].a+2.f;

      h = bound[o].b+2.f;

    }



    float dw = fabs(ppx - ccx) - w;

    float dh = fabs(ppy - ccy) - h;



    if ( (dw > 0) || (dh > 0) ) continue;



    if (dw > dh)

    {

      px = cx+ccx + w * SGN(ppx-ccx);

    }

    else

    {

      py = cy+ccy + h * SGN(ppy-ccy);

    }

  }

}







void CheckCollision(float &cx, float &cz)

{

  if (cx < 36*256) cx = 36*256;

  if (cz < 36*256) cz = 36*256;

  if (cx >980*256) cx =980*256;

  if (cz >980*256) cz =980*256;

  int ccx = static_cast<int>(cx) / 256;

  int ccz = static_cast<int>(cz) / 256;



  for (int z=-4; z<=4; z++)

    for (int x=-4; x<=4; x++)

      if (OMap[ccz+z][ccx+x]!=255)

      {

        int ob = OMap[ccz+z][ccx+x];

        float CR = static_cast<float>(MObjects[ob].info.Radius);



        float oz = (ccz+z) * 256.f + 128.f;

        float ox = (ccx+x) * 256.f + 128.f;



        float LandY = GetLandOH(ccx+x, ccz+z);



        if (!(MObjects[ob].info.flags & ofBOUND))

        {

          if (MObjects[ob].info.YHi + LandY < PlayerY + 128) continue;

          if (MObjects[ob].info.YLo + LandY > PlayerY + 256) continue;

        }



        if (MObjects[ob].info.flags & ofBOUND)

        {

          CheckBoundCollision(cx, cz, ox, oz, LandY, MObjects[ob].bound, ((FMap[ccz+z][ccx+x] >> 2) & 3)  );

        }

        else if (MObjects[ob].info.flags & ofCIRCLE)

        {

          float dx = ox - cx;

          float dz = oz - cz;

          float distSq = dx * dx + dz * dz;

          float crSq = CR * CR;

          if (distSq < crSq)

          {

            float r = static_cast<float>(sqrt(distSq));

            if (r > 0.0f)

            {

              float push = (CR - r) / r;

              cx = cx - dx * push;

              cz = cz - dz * push;

            }

          }

        }

        else

        {

          float r = static_cast<float>(MAX( fabs(ox-cx), fabs(oz-cz) ));

          if (r<CR)

          {

            if (fabs(ox-cx) > fabs(oz-cz) )

              cx = cx - (ox - cx) * (CR-r)/r;

            else

              cz = cz - (oz - cz) * (CR-r)/r;

          }

        }

      }



  if (!InTrophyRoom()) return;

  for (int c=0; c<ChCount; c++)

  {

    // Static mounts remain solid even when movement changes the mode slot.
    if (Characters[c].StateF != 0xFF) continue;

    float px = Characters[c].pos.x;

    float pz = Characters[c].pos.z;

    float CR = DinoInfo[ Characters[c].CType ].Radius;

    float dx = px - cx;

    float dz = pz - cz;

    float distSq = dx * dx + dz * dz;

    float crSq = CR * CR;

    if (distSq < crSq)

    {

      float r = static_cast<float>(sqrt(distSq));

      if (r > 0.0f)

      {

        float push = (CR - r) / r;

        cx = cx - dx * push;

        cz = cz - dz * push;

      }
      else
      {
        // A spawn/teleport exactly at the centre still needs separation.
        cx = px + CR;
      }

    }



  }



}







int TraceCheckPlane(Vector3d a, Vector3d b, Vector3d c)

{

  Vector3d pnv,hp;

  float sa, sb;

  MulVectorsVect(SubVectors(b,a), SubVectors(c,a), pnv);

  NormVector(pnv, 1.f);



  MulVectorsScal(SubVectors(TraceA,a), pnv, sa);

  MulVectorsScal(SubVectors(TraceB,a), pnv, sb);

  if (sa*sb>-1.f) return 0;



//========= calc hit point =======//

  float SCLN,SCVN;



  MulVectorsScal(SubVectors(TraceA,a), pnv, SCLN);

  MulVectorsScal(TraceNv, pnv, SCVN);



  SCLN/=SCVN;

  SCLN=static_cast<float>(fabs(SCLN));

  hp.x = TraceA.x + TraceNv.x * SCLN;

  hp.y = TraceA.y + TraceNv.y * SCLN;

  hp.z = TraceA.z + TraceNv.z * SCLN;



  Vector3d vm;

  MulVectorsVect( SubVectors(b,a), SubVectors(hp,a), vm);

  MulVectorsScal( vm, pnv, sa);

  if (sa<0) return 0;



  MulVectorsVect( SubVectors(c,b), SubVectors(hp,b), vm);

  MulVectorsScal( vm, pnv, sa);

  if (sa<0) return 0;



  MulVectorsVect( SubVectors(a,c), SubVectors(hp,c), vm);

  MulVectorsScal( vm, pnv, sa);

  if (sa<0) return 0;





  Vector3d traceDir = SubVectors(TraceB, TraceA);

  if (VectorLengthSq(SubVectors(hp, TraceA)) < VectorLengthSq(traceDir))

  {

    TraceB = hp;

    return 1;

  }



  return 0;

}





void TraceModel(int xx, int zz, int o)

{

  TModel *mptr = MObjects[o].model.get();

  v[0].x = xx * 256.f + 128.f;

  v[0].z = zz * 256.f + 128.f;

  v[0].y = static_cast<float>((HMapO[zz][xx])) * ctHScale;



  v[0].y+=700.f;

  if (PointToVectorDSq(TraceA, TraceNv, v[0]) > 1400.f * 1400.f) return;

  v[0].y-=700.f;



  const int q = ((FMap[zz][xx] >> 2) & 7);
  const float ca = kOctCos[q];
  const float sa = kOctSin[q];



  for (int vv=0; vv<mptr->VCount; vv++)

  {

    rVertex[vv].x = mptr->gVertex[vv].x * ca + mptr->gVertex[vv].z * sa  + v[0].x;

    rVertex[vv].y = mptr->gVertex[vv].y + v[0].y;

    rVertex[vv].z = mptr->gVertex[vv].z * ca - mptr->gVertex[vv].x * sa  + v[0].z;

  }



  for (int f=0; f<mptr->FCount; f++)

  {

    TFace *fptr = &mptr->gFace[f];

    if (fptr->Flags & (sfOpacity + sfTransparent) ) continue;

    v[0] = rVertex[fptr->v1];

    v[1] = rVertex[fptr->v2];

    v[2] = rVertex[fptr->v3];

    if (TraceCheckPlane(v[0], v[1], v[2]) )

      TraceRes = tresModel;

  }

}







void TraceHitBox()

{

	THitBox *cptr = &HitBox;



	if (PointToVectorDSq(TraceA, TraceNv, cptr->pos) > 1024.f * 1024.f) return;



	TModel *mptr = HitBoxModel.mptr.get();

	CreateMorphedModel(HitBoxModel.mptr.get(), &HitBoxModel.Animation[HitBox.phase], 0, 1.0);

	static float cachedAlpha = 0.0f;
	static float cachedCa = 0.0f;
	static float cachedSa = 1.0f;
	static bool cacheValid = false;
	if (!cacheValid || cachedAlpha != cptr->alpha)
	{
		cachedAlpha = cptr->alpha;
		// cos(-alpha + pi/2) == sin(alpha), sin(-alpha + pi/2) == cos(alpha)
		cachedCa = static_cast<float>(sin(cptr->alpha));
		cachedSa = static_cast<float>(cos(cptr->alpha));
		cacheValid = true;
	}
	const float ca = cachedCa;
	const float sa = cachedSa;

	for (int vv = 0; vv < mptr->VCount; vv++)

	{

		rVertex[vv].x = mptr->gVertex[vv].x * ca + mptr->gVertex[vv].z * sa + cptr->pos.x;

		rVertex[vv].y = mptr->gVertex[vv].y + cptr->pos.y;

		rVertex[vv].z = mptr->gVertex[vv].z * ca - mptr->gVertex[vv].x * sa + cptr->pos.z;

	}



	for (int f = 0; f < mptr->FCount; f++)

	{

		TFace *fptr = &mptr->gFace[f];

		//if (fptr->Flags & (sfOpacity + sfTransparent)) continue;

		v[0] = rVertex[fptr->v1];

		v[1] = rVertex[fptr->v2];

		v[2] = rVertex[fptr->v3];

		if (TraceCheckPlane(v[0], v[1], v[2]))

		{

			TraceRes = tresHunter;

		}



	}

}







void TraceCharacter(int c)

{

  TCharacter *cptr = &Characters[c];



  if (PointToVectorDSq(TraceA, TraceNv, cptr->pos) > 1024.f * 1024.f) return;



  TModel *mptr = cptr->pinfo->mptr.get();

  CreateChMorphedModel(cptr);

  // Character animation maintains lookz=sin(alpha), lookx=cos(alpha).
  // Reuse those cached basis terms instead of doing two trig calls per traced character.
  const float ca = cptr->lookz;
  const float sa = cptr->lookx;

  for (int vv=0; vv<mptr->VCount; vv++)

  {

    rVertex[vv].x = mptr->gVertex[vv].x * ca + mptr->gVertex[vv].z * sa  + cptr->pos.x;

    rVertex[vv].y = mptr->gVertex[vv].y + cptr->pos.y;

    rVertex[vv].z = mptr->gVertex[vv].z * ca - mptr->gVertex[vv].x * sa  + cptr->pos.z;

  }



  for (int f=0; f<mptr->FCount; f++)

  {

    TFace *fptr = &mptr->gFace[f];

    if (fptr->Flags & (sfOpacity + sfTransparent) ) continue;

    v[0] = rVertex[fptr->v1];

    v[1] = rVertex[fptr->v2];

    v[2] = rVertex[fptr->v3];

    if (TraceCheckPlane(v[0], v[1], v[2]) )

    {

      TraceRes = tresChar;

      ShotDino = c;

      if (fptr->Flags & sfMortal) TraceRes |= 0x8000;

    }



  }

}





void FillVGround(Vector3d &v, int xx, int zz)

{

  v.x = xx*256.f;

  v.z = zz*256.f;

  v.y = static_cast<float>(HMap[zz][xx])*ctHScale;

}





void FillWGround(Vector3d &v, int xx, int zz)

{

  v.x = xx*256.f;

  v.z = zz*256.f;

  v.y = static_cast<float>(WaterList[ WMap[zz][xx] ].wlevel)*ctHScale;

}





int  TraceLook(float ax, float ay, float az,

               float bx, float by, float bz)

{

  TraceA.x = ax;

  TraceA.y = ay;

  TraceA.z = az;

  TraceB.x = bx;

  TraceB.y = by;

  TraceB.z = bz;



  TraceNv =  SubVectors(TraceB, TraceA);



  Vector3d TraceNvP;



  TraceNvP = TraceNv;

  TraceNvP.y = 0;



  NormVector(TraceNv, 1.0f);

  NormVector(TraceNvP, 1.0f);

  ObjectsOnLook=0;



  int axi = static_cast<int>((ax/256.f));

  int azi = static_cast<int>((az/256.f));



  int bxi = static_cast<int>((bx/256.f));

  int bzi = static_cast<int>((bz/256.f));



  int xm1 = MIN(axi, bxi) - 2;

  int xm2 = MAX(axi, bxi) + 2;

  int zm1 = MIN(azi, bzi) - 2;

  int zm2 = MAX(azi, bzi) + 2;



//======== trace ground model and static objects ============//

  for (int zz=zm1; zz<=zm2; zz++)

    for (int xx=xm1; xx<=xm2; xx++)

    {

      if (xx<2 || xx>1010) continue;

      if (zz<2 || zz>1010) continue;



      std::int32_t ReverseOn = (FMap[zz][xx] & fmReverse);



      FillVGround(v[0], xx, zz);

      FillVGround(v[1], xx+1, zz);

      if (ReverseOn) FillVGround(v[2], xx, zz+1);

      else FillVGround(v[2], xx+1, zz+1);

      if (TraceCheckPlane(v[0], v[1], v[2])) return 1;



      if (ReverseOn)

      {

        v[0] = v[2];

        FillVGround(v[2], xx+1, zz+1);

      }

      else

      {

        v[1] = v[2];

        FillVGround(v[2], xx, zz+1);

      }

      if (TraceCheckPlane(v[0], v[1], v[2])) return 1;



      int o = OMap[zz][xx];

      if ( o!=255)

      {

        float s1,s2;

        v[0].x = xx * 256.f + 128.f;

        v[0].z = zz * 256.f + 128.f;

        v[0].y = TraceB.y;

        MulVectorsScal( SubVectors(v[0], TraceB), TraceNv, s1);

        s1*=-1;

        v[0].y = TraceA.y;

        MulVectorsScal( SubVectors(v[0], TraceA), TraceNv, s2);



        if (s1>0 && s2>0)

          if (PointToVectorDSq(TraceA, TraceNvP, v[0]) < 180.f * 180.f)

          {

            ObjectsOnLook++;

            if (MObjects[o].info.Radius > 32)	ObjectsOnLook++;

          }

      }

    }



  return 0;

}







int  TraceShot(float  ax, float  ay, float az,

               float &bx, float &by, float &bz,

		   	   bool bDanger, bool bCDanger)

{

  TraceA.x = ax;

  TraceA.y = ay;

  TraceA.z = az;

  TraceB.x = bx;

  TraceB.y = by;

  TraceB.z = bz;



  TraceNv =  SubVectors(TraceB, TraceA);

  NormVector(TraceNv, 1.0f);

  TraceRes = -1;



  int axi = static_cast<int>((ax/256.f));

  int azi = static_cast<int>((az/256.f));



  int bxi = static_cast<int>((bx/256.f));

  int bzi = static_cast<int>((bz/256.f));



  int xm1 = MIN(axi, bxi) - 2;

  int xm2 = MAX(axi, bxi) + 2;

  int zm1 = MIN(azi, bzi) - 2;

  int zm2 = MAX(azi, bzi) + 2;



//======== trace ground model and static objects ============//

  for (int zz=zm1; zz<=zm2; zz++)

    for (int xx=xm1; xx<=xm2; xx++)

    {

      if (xx<2 || xx>1010) continue;

      if (zz<2 || zz>1010) continue;



      std::int32_t ReverseOn = (FMap[zz][xx] & fmReverse);



	  // hunter shot







      FillVGround(v[0], xx, zz);

      FillVGround(v[1], xx+1, zz);

      if (ReverseOn) FillVGround(v[2], xx, zz+1);

      else FillVGround(v[2], xx+1, zz+1);

	  if (TraceCheckPlane(v[0], v[1], v[2])) TraceRes = tresGround;



      if (ReverseOn)

      {

        v[0] = v[2];

        FillVGround(v[2], xx+1, zz+1);

      }

      else

      {

        v[1] = v[2];

        FillVGround(v[2], xx, zz+1);

      }

      if (TraceCheckPlane(v[0], v[1], v[2])) TraceRes = tresGround;



      if ( (FMap[zz][xx] & fmWaterA)>0)

      {

        FillWGround(v[0], xx, zz);

        FillWGround(v[1], xx+1, zz);

        FillWGround(v[2], xx+1, zz+1);

		if (TraceCheckPlane(v[0], v[1], v[2])) TraceRes = tresWater;

        v[1] = v[2];

        FillWGround(v[2], xx, zz+1);

		if (TraceCheckPlane(v[0], v[1], v[2])) TraceRes = tresWater;

      }



      if (OMap[zz][xx] !=255)

        TraceModel(xx, zz, OMap[zz][xx]);

    }



//======== trace characters ============//

  if (bCDanger)

	  for (int c=0; c<ChCount; c++)

	    TraceCharacter(c);





  if (bDanger && MyHealth) TraceHitBox();



  float l;

  if ((TraceRes & 0xFF)==tresChar || TraceRes == tresHunter) l = 32.f;

  else l=16.f;

  bx = TraceB.x - TraceNv.x * l;

  by = TraceB.y - TraceNv.y * l;

  bz = TraceB.z - TraceNv.z * l;



  return TraceRes;

}









void InitClips2()

{

  ClipA.v1.x = -0.625208f;  // sin(pi/4-0.11)

  ClipA.v1.y = 0; /*-constant*/

  ClipA.v1.z =  0.780458f;   // cos(pi/4-0.11)

  ClipA.v2.x = 0;

  ClipA.v2.y = 1;

  ClipA.v2.z = 0;

  MulVectorsVect(ClipA.v1, ClipA.v2, ClipA.nv);



  ClipC.v1.x = +0.625208f;  // sin(pi/4-0.11)

  ClipC.v1.y = 0; /*-constant*/

  ClipC.v1.z =  0.780458f;   // cos(pi/4-0.11)

  ClipC.v2.x = 0;

  ClipC.v2.y =-1;

  ClipC.v2.z = 0;

  MulVectorsVect(ClipC.v1, ClipC.v2, ClipC.nv);





  ClipB.v1.x = 0;

  ClipB.v1.y =  0.571488f;  // sin(pi/5-0.02)

  ClipB.v1.z =  0.820610f;   // cos(pi/5-0.02)

  ClipB.v2.x = 1;

  ClipB.v2.y = 0;

  ClipB.v2.z = 0;

  MulVectorsVect(ClipB.v1, ClipB.v2, ClipB.nv);



  ClipD.v1.x = 0;

  ClipD.v1.y = -0.571488f;  // sin(pi/5-0.02)

  ClipD.v1.z =  0.820610f;   // cos(pi/5-0.02)

  ClipD.v2.x =-1;

  ClipD.v2.y = 0;

  ClipD.v2.z = 0;

  MulVectorsVect(ClipD.v1, ClipD.v2, ClipD.nv);



  ClipZ.v1.x = 0;

  ClipZ.v1.y = 1;

  ClipZ.v1.z = 0;

  ClipZ.v2.x = 1;

  ClipZ.v2.y = 0;

  ClipZ.v2.z = 0;

  MulVectorsVect(ClipZ.v1, ClipZ.v2, ClipZ.nv);



}







void InitClips()

{

  // Build the 4 frustum clip plane normals (left/right/top/bottom)

  // from the half-FOV angles. The + 0.01f widen is resolution-

  // independent (~0.57 deg past the strict screen edge) and prevents

  // terrain triangles right at the screen edge from being clipped

  // when their projected vertices fall just past the screen. The

  // previous C2 ME formula widened by 1-2 pixels, which is negligible

  // at any modern resolution and caused visible culling on 16:9

  // displays. C1 uses the same atan2-based formula with the same

  // 0.01 radian widen.

  static bool clipAnglesValid = false;
  static int cachedVideoCX = 0;
  static int cachedVideoCY = 0;
  static float cachedCameraW = 0.0f;
  static float cachedCameraH = 0.0f;

  if (!clipAnglesValid || cachedVideoCX != VideoCX || cachedVideoCY != VideoCY ||
      cachedCameraW != CameraW || cachedCameraH != CameraH)
  {
    cachedVideoCX = VideoCX;
    cachedVideoCY = VideoCY;
    cachedCameraW = CameraW;
    cachedCameraH = CameraH;
    clipAnglesValid = true;

    const float h_angle = static_cast<float>(atan2(static_cast<float>(VideoCX), CameraW)) + 0.01f;
    const float v_angle = static_cast<float>(atan2(static_cast<float>(VideoCY), CameraH)) + 0.01f;

    // Compute sin/cos once per angle when the projection changes.
    const float sh = sinf(h_angle);
    const float ch = cosf(h_angle);
    const float sv = sinf(v_angle);
    const float cv = cosf(v_angle);

    ClipA.v1.x = -sh;
    ClipA.v1.y = 0;
    ClipA.v1.z =  ch;
    ClipA.v2.x = 0;
    ClipA.v2.y = 1;
    ClipA.v2.z = 0;
    MulVectorsVect(ClipA.v1, ClipA.v2, ClipA.nv);

    ClipC.v1.x = +sh;
    ClipC.v1.y = 0;
    ClipC.v1.z =  ch;
    ClipC.v2.x = 0;
    ClipC.v2.y =-1;
    ClipC.v2.z = 0;
    MulVectorsVect(ClipC.v1, ClipC.v2, ClipC.nv);

    ClipB.v1.x = 0;
    ClipB.v1.y =  sv;
    ClipB.v1.z =  cv;
    ClipB.v2.x = 1;
    ClipB.v2.y = 0;
    ClipB.v2.z = 0;
    MulVectorsVect(ClipB.v1, ClipB.v2, ClipB.nv);

    ClipD.v1.x = 0;
    ClipD.v1.y = -sv;
    ClipD.v1.z =  cv;
    ClipD.v2.x =-1;
    ClipD.v2.y = 0;
    ClipD.v2.z = 0;
    MulVectorsVect(ClipD.v1, ClipD.v2, ClipD.nv);

    ClipZ.v1.x = 0;
    ClipZ.v1.y = 1;
    ClipZ.v1.z = 0;
    ClipZ.v2.x = 1;
    ClipZ.v2.y = 0;
    ClipZ.v2.z = 0;
    MulVectorsVect(ClipZ.v1, ClipZ.v2, ClipZ.nv);
  }

  ClipW.nv.x =  0;
  ClipW.nv.y = cb;
  ClipW.nv.z = sb;



}











void CalcLights(TModel* mptr)

{

  int VCount = mptr->VCount;

  int FCount = mptr->FCount;

  int FUsed;

  float c;

  // Load-time scratch that lives for the duration of this call only.
  // A Level tag would strand unreclaimable arena space until the next
  // bulk reset (arena _HeapFree is a no-op); the heap free at the end of
  // the function is real, so tag it Global.
  size_t normalBytes = 0;
  if (FCount < 0 || !CheckedBytes2(static_cast<size_t>(FCount), sizeof(Vector3d), normalBytes))
    DoHalt("Face normal allocation size overflow.");
  Vector3d* norms = (Vector3d*)_HeapAlloc(Heap, 0, normalBytes, MemoryTag::Global);

  Vector3d a, b, nv, rv;

  Vector3d slight;

  slight.x =-Sun3dPos.x;

  slight.y =-Sun3dPos.y/2;

  slight.z =-Sun3dPos.z;





  NormVector(slight, 1.0f);



  for (int f=0; f<FCount; f++)

  {

    int v1 = mptr->gFace[f].v1;

    int v2 = mptr->gFace[f].v2;

    int v3 = mptr->gFace[f].v3;



    a.x = mptr->gVertex[v2].x - mptr->gVertex[v1].x;

    a.y = mptr->gVertex[v2].y - mptr->gVertex[v1].y;

    a.z = mptr->gVertex[v2].z - mptr->gVertex[v1].z;



    b.x = mptr->gVertex[v3].x - mptr->gVertex[v1].x;

    b.y = mptr->gVertex[v3].y - mptr->gVertex[v1].y;

    b.z = mptr->gVertex[v3].z - mptr->gVertex[v1].z;



    MulVectorsVect(a, b, norms[f]);

    NormVector(norms[f], 1.0f);

  }



  for (int VT=0; VT<4; VT++)

  {

    float ca = kRotCos[VT & 3];
    float sa = kRotSin[VT & 3];

    for (int v=0; v<VCount; v++)

    {

      FUsed = 0;

      nv.x=0;

      nv.y=0;

      nv.z=0;

      for (int f=0; f<FCount; f++)

        if (!(mptr->gFace[f].Flags & sfDoubleSide) )

          if (mptr->gFace[f].v1 == v || mptr->gFace[f].v2 == v || mptr->gFace[f].v3 == v )

          {

            FUsed++;

            nv = AddVectors(nv, norms[f]);

          }



      if (!FUsed) mptr->VLight[VT][v] = 0;

      else

      {

        NormVector(nv, 1.0f);

        rv.y = nv.y;

        rv.x = nv.x * sa + nv.z * ca;

        rv.z = nv.z * sa - nv.x * ca;

        MulVectorsScal(rv, slight, c);

        c = c * 64;

        //if (c>64) c=64;

        //if (c<-64) c=-64;

        mptr->VLight[VT][v] = c;

      }

    }

  }



  (void)_HeapFree(Heap, 0, norms);

}





/*

void CalcGouraud(TModel* mptr, Vecto3d *nvs[])

{

	int VCount = mptr->VCount;

	int FCount = mptr->FCount;

    int FUsed;

	float c;

	Vector3d norms[1024];

	Vector3d a, b, nv, rv;

    Vector3d slight;

	slight.x =-Sun3dPos.x;

	slight.y =-Sun3dPos.y;

	slight.z =-Sun3dPos.z;



	NormVector(slight, 1.0f);



    for (int f=0; f<FCount; f++) {

		int v1 = mptr->gFace[f].v1;

		int v2 = mptr->gFace[f].v2;

		int v3 = mptr->gFace[f].v3;



		a.x = mptr->gVertex[v2].x - mptr->gVertex[v1].x;

		a.y = mptr->gVertex[v2].y - mptr->gVertex[v1].y;

		a.z = mptr->gVertex[v2].z - mptr->gVertex[v1].z;



		b.x = mptr->gVertex[v3].x - mptr->gVertex[v1].x;

		b.y = mptr->gVertex[v3].y - mptr->gVertex[v1].y;

		b.z = mptr->gVertex[v3].z - mptr->gVertex[v1].z;



		MulVectorsVect(a, b, norms[f]);

		NormVector(norms[f], 1.0f);

	}



	for (int v=0; v<VCount; v++) {

		FUsed = 0;

		nv.x=0; nv.y=0; nv.z=0;

		for (f=0; f<FCount; f++)

		  if (!(mptr->gFace[f].Flags & sfDoubleSide) )

			if (mptr->gFace[f].v1 == v || mptr->gFace[f].v2 == v || mptr->gFace[f].v3 == v )

			{ FUsed++;  nv = AddVectors(nv, norms[f]); }



		if (!FUsed) mptr->VLight[0][v] = 0;

		else {

           NormVector(nv, 1.0f);

           MulVectorsScal(nv, slight, c);

		   if (c<0) c=0; c=(c-0.5)*2;

		   c=c*c*c;

		   c = c * 98;

		   if (c>96) c=96;

		   if (c<-96) c=-96;

		   mptr->VLight[0][v] = c;

		}



	}

}

*/





void CalcGouraud(TModel* mptr, Vector3d *nvs)

{

  int VCount = mptr->VCount;

  float c;

  Vector3d slight;

  slight.x =-Sun3dPos.x;

  slight.y =-Sun3dPos.y;

  slight.z =-Sun3dPos.z;



  NormVector(slight, 1.0f);



  for (int v=0; v<VCount; v++)

  {

    MulVectorsScal(nvs[v], slight, c);

    if (c<0) c=0;

    c=(c-0.5)*2;

    c=c*c*c;

    c = c * 96;

    if (c>96) c=96;

    if (c<-64) c=-64;

    mptr->VLight[0][v] = c;

  }

}







void CalcNormals(TModel* mptr, Vector3d *nvs)

{

  int VCount = mptr->VCount;

  int FCount = mptr->FCount;

  float c;

  Vector3d a, b, nv, rv;

  memset(nvs, 0, 3*4*VCount);



  for (int f=0; f<FCount; f++)

  {

    if (mptr->gFace[f].Flags & sfDoubleSide) continue;

    int v1 = mptr->gFace[f].v1;

    int v2 = mptr->gFace[f].v2;

    int v3 = mptr->gFace[f].v3;



    a.x = mptr->gVertex[v2].x - mptr->gVertex[v1].x;

    a.y = mptr->gVertex[v2].y - mptr->gVertex[v1].y;

    a.z = mptr->gVertex[v2].z - mptr->gVertex[v1].z;



    b.x = mptr->gVertex[v3].x - mptr->gVertex[v1].x;

    b.y = mptr->gVertex[v3].y - mptr->gVertex[v1].y;

    b.z = mptr->gVertex[v3].z - mptr->gVertex[v1].z;



    MulVectorsVect(a, b, nv);

    NormVector(nv, 1000.0f);

    nvs[v1]=AddVectors(nvs[v1], nv);

    nvs[v2]=AddVectors(nvs[v2], nv);

    nvs[v3]=AddVectors(nvs[v3], nv);

  }



  for (int v=0; v<VCount; v++)

    NormVector(nvs[v], 1.0);

}





/*

void CalcNormals(TModel* mptr, Vector3d *nvs)

{

	int VCount = mptr->VCount;

	int FCount = mptr->FCount;

    int FUsed;

	float c;

	Vector3d norms[1024];

	Vector3d a, b, nv, rv;

    Vector3d slight;

	slight.x =-Sun3dPos.x;

	slight.y =-Sun3dPos.y;

	slight.z =-Sun3dPos.z;



	NormVector(slight, 1.0f);



    for (int f=0; f<FCount; f++) {

		int v1 = mptr->gFace[f].v1;

		int v2 = mptr->gFace[f].v2;

		int v3 = mptr->gFace[f].v3;



		a.x = mptr->gVertex[v2].x - mptr->gVertex[v1].x;

		a.y = mptr->gVertex[v2].y - mptr->gVertex[v1].y;

		a.z = mptr->gVertex[v2].z - mptr->gVertex[v1].z;



		b.x = mptr->gVertex[v3].x - mptr->gVertex[v1].x;

		b.y = mptr->gVertex[v3].y - mptr->gVertex[v1].y;

		b.z = mptr->gVertex[v3].z - mptr->gVertex[v1].z;



		MulVectorsVect(a, b, norms[f]);

		NormVector(norms[f], 1.0f);

	}



	for (int v=0; v<VCount; v++) {

		FUsed = 0;

		nv.x=0; nv.y=0; nv.z=0;

		for (f=0; f<FCount; f++)

		  if (!(mptr->gFace[f].Flags & sfDoubleSide) )

			if (mptr->gFace[f].v1 == v || mptr->gFace[f].v2 == v || mptr->gFace[f].v3 == v )

			{ FUsed++;  nv = AddVectors(nv, norms[f]); }



		if (!FUsed) { nv.x=0; nv.y=1; nv.z=0; }

        NormVector(nv, 1.0f);

        nvs[v] = nv;

	}

} */







void CalcPhongMapping(TModel* mptr, Vector3d *nv)

{

  Vector3d l,v,m, tx, ty, tv;

  float x,y;

  l.x =-Sun3dPos.x;

  l.y =-Sun3dPos.y;

  l.z =-Sun3dPos.z;



  tv.z = 0;

  tv.x = 1;

  tv.y = 1;



  NormVector(l, 1.0f);

  for (int i=0; i<mptr->VCount; i++)

  {

    v.x = mptr->gVertex[i].x;

    v.y = mptr->gVertex[i].y;

    v.z = mptr->gVertex[i].z;

    NormVector(v, 1.0f);

    m = AddVectors(l, v);

    NormVector(m, 1.0f);





    if (l.z < 0) MulVectorsVect(m, tv, tx);

    else MulVectorsVect(m, v, tx);



    NormVector(tx, 1.0f);

    MulVectorsVect(m, tx, ty);

    NormVector(ty, 1.0f);

    MulVectorsScal(tx, nv[i], x);

    MulVectorsScal(ty, nv[i], y);

    PhongMapping[i].x = 128 + x *127;

    PhongMapping[i].y = 128 + y *127;

  }

}





void CalcEnvMapping(TModel* mptr, Vector3d *nv)

{

  Vector3d l,v,m, tx, ty;

  float x,y,s;

  float cc = cos((PlayerX + PlayerZ) / 500);

  float ss = sin((PlayerX + PlayerZ) / 500);

  tx.x = cc;

  tx.y = 0.0f;

  tx.z =-ss;

  ty.x = ss;

  ty.y = 0.0f;

  ty.z = cc;



  tx = RotateVector(tx);

  ty = RotateVector(ty);



  NormVector(l, 1.0f);

  for (int i=0; i<mptr->VCount; i++)

  {

    v.x = mptr->gVertex[i].x;

    v.y = mptr->gVertex[i].y;

    v.z = mptr->gVertex[i].z;

    MulVectorsScal(v, nv[i], s);

    m = nv[i];

    NormVector(m, s*2);

    m = AddVectors(m, v);

    NormVector(m, 1.0f);



    MulVectorsScal(tx, m, x);

    MulVectorsScal(ty, m, y);

    PhongMapping[i].x = 128 + x * 122;

    PhongMapping[i].y = 128 + y * 122;

  }

}







void CalcBoundBox(TModel* mptr, TBound *bound)

{

  float x1, x2, y1, y2, z1, z2;

  std::int32_t first;



  for (int o=0; o<8; o++)

  {

    first = true;

    bound[o].a=-1;





    for (int v=0; v<mptr->VCount; v++)

    {

      TPoint3d p = mptr->gVertex[v];

      if (p.hide) continue;

      if (p.owner!=o) continue;



      if (first)

      {

        x1 = p.x-1.0f;

        x2 = p.x+1.0f;

        y1 = p.y-1.0f;

        y2 = p.y+1.0f;

        z1 = p.z-1.0f;

        z2 = p.z+1.0f;

        first = false;

      }



      if (p.x < x1) x1=p.x;

      if (p.x > x2) x2=p.x;



      if (p.y < y1) y1=p.y;

      if (p.y > y2) y2=p.y;



      if (p.z < z1) z1=p.z;

      if (p.z > z2) z2=p.z;

    }



    if (first) continue;



    x1-=72.f;

    x2+=72.f;

    z1-=72.f;

    z2+=72.f;



    bound[o].y1 = y1;

    bound[o].y2 = y2;

    bound[o].cx = (x1+x2) / 2;

    bound[o].cy = (z1+z2) / 2;

    bound[o].a  = (x2-x1) / 2;

    bound[o].b  = (z2-z1) / 2;



  }



}

