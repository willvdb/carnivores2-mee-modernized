// ModelTypes.h — Model/animation-related type definitions
// Extracted from Hunt.h (Phase 0.1 — Split god header into focused headers)
#pragma once

#include "Memory.h"
#include "Core/MathTypes.h"
#include <cstdint>
#include <type_traits>

struct TAni
{
  char aniName[32];
  int aniKPS, FramesCount, AniTime;
  unique_heap_ptr<short int[]> aniData;
};

struct TVTL
{
  int aniKPS, FramesCount, AniTime;
  unique_heap_ptr<short int[]> aniData;
};

struct TPoint3d
{
  float x;
  float y;
  float z;
  short owner;
  short hide;
};

struct TFace
{
  int v1, v2, v3;
#ifdef _soft
  // The x86 software rasterizer (DrawModelFace / DrawCorrectedTexturedFace in
  // renderasm.cpp) expects per-face UVs in 16.16 fixed-point int. ModelLoader's
  // _soft path (CorrectModel) bit-copies that fixed-point value into these
  // fields; the consumer does `mscrp.tx = fptr->tax` (int = int). Making these
  // float (as the Phase 1.4 TFace unification did) turns that read into an
  // int<-float VALUE conversion of garbage bits -> UVs collapse to 0 -> black
  // model textures. Keep int under _soft, float otherwise.
  int   tax, tbx, tcx, tay, tby, tcy;
#else
  float tax, tbx, tcx, tay, tby, tcy;
#endif
  unsigned short Flags, DMask;
  int Distant, Next, group;
  char reserv[12];
};

// TFacef is now identical to TFace (unified in Phase 1.4). Kept as an alias
// for compatibility; the union in TModel can use either.
using TFacef = TFace;

struct TObj
{
  char OName[32];
  float ox;
  float oy;
  float oz;
  short owner;
  short hide;
};

struct TObjInfo
{
  int  Radius;
  int  YLo, YHi;
  int  linelenght, lintensity;
  int  circlerad, cintensity;
  int  flags;
  int  GrRad;
  int  DefLight;
  int  LastAniTime;
  float BoundR;
  unsigned char res[16];
};

struct TBMPModel
{
  Vector3d  gVertex[4];
  unique_heap_ptr<unsigned short[]> lpTexture;
};

struct TBound
{
  float cx, cy, a, b, y1, y2;
};

struct TModel
{
  int VCount, FCount, TextureSize, TextureHeight;
  unique_heap_ptr<TPoint3d[]> gVertex;

  union
  {
    TFace    *gFace;
    TFacef   *gFacef;
  };

  unique_heap_ptr<unsigned short[]> lpTexture, lpTexture2, lpTexture3;

  float*    VLight[4];

  TModel() = default;

  TModel(const TModel&) = delete;
  TModel& operator=(const TModel&) = delete;

  // Move ops are deleted because the hand-written versions leak gFace and
  // VLight[0-3] (raw heap pointers that must be freed explicitly before
  // overwriting). TModel is always managed through unique_obj_ptr, which
  // never moves the pointee — only the pointer itself moves, so move ops
  // are never needed. If move semantics become necessary in the future,
  // the new implementation must free the destination's gFace and VLight
  // buffers before moving from the source.
  TModel(TModel&& other) noexcept = delete;
  TModel& operator=(TModel&& other) noexcept = delete;

  ~TModel() = default;
};

// ReleaseModelBuffers() (Hunt/Loaders/ModelLoader.cpp) frees VLight[0] through
// _HeapFree and then nulls all four slots. That only works while the field is a
// raw pointer, so migrating it to unique_heap_ptr the way lpTexture and gFace
// were migrated would turn the manual free into a double-free.
static_assert(std::is_same<decltype(TModel::VLight), float*[4]>::value,
              "TModel::VLight must stay a raw float*[4] — it is freed by hand "
              "in ReleaseModelBuffers()");

struct TObject
{
  TObjInfo info;
  TBound   bound[8];
  TBMPModel bmpmodel;
  unique_obj_ptr<TModel> model;
  TVTL    vtl;
};
