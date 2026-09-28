// MathTypes.h — Vector and geometry types
// Extracted from Hunt.h (Phase 0.1 — Split god header into focused headers)
#pragma once

#include <cstddef>
#include <type_traits>

struct Vector3d
{
  float x, y, z;
};

struct TPoint3di
{
  int x, y, z;
};

struct Vector2di
{
  int x, y;
};

struct Vector2df
{
  float x, y;
};

struct ScrPoint
{
#ifdef _soft
  // The x86 software rasterizer (DrawTexturedFace / DrawCorrectedTexturedFace
  // in renderasm.cpp) reads scrp as raw 32-bit integers and shifts x by 16 to
  // build 16.16 fixed-point span extents. These MUST stay int under _soft;
  // a float field would have its integer value converted on store, so the
  // asm would reinterpret the float bit-pattern as a coordinate -> garbage
  // geometry (terrain culled to nothing, near-model/weapon writes OOB -> AV).
  int   x, y, tx, ty;
#else
  float x, y, tx, ty;
#endif
  int Light, z, r2, r3;
};

// The same asm walks scrp[0..2] with a fixed 32-byte stride (`scrp + 4`,
// `scrp + 4 + 32`, `scrp + 4 + 64`), so the entry size is load-bearing too.
static_assert(sizeof(ScrPoint) == 32,
              "ScrPoint size changed — renderasm.cpp walks scrp[] with a "
              "32-byte stride");
#ifdef _soft
static_assert(std::is_same<decltype(ScrPoint::x), int>::value &&
                  std::is_same<decltype(ScrPoint::y), int>::value &&
                  std::is_same<decltype(ScrPoint::tx), int>::value &&
                  std::is_same<decltype(ScrPoint::ty), int>::value,
              "ScrPoint x/y/tx/ty must stay int under _soft — renderasm.cpp "
              "reads them as 16.16 fixed-point dwords");
static_assert(offsetof(ScrPoint, x) == 0 && offsetof(ScrPoint, y) == 4 &&
                  offsetof(ScrPoint, tx) == 8 && offsetof(ScrPoint, ty) == 12,
              "ScrPoint coordinate offsets changed — renderasm.cpp reads "
              "them at fixed byte offsets");
#endif

struct MScrPoint
{
  int x, y, tx, ty;
};

struct CLIPPLANE
{
  Vector3d v1, v2, nv;
};

struct EPoint
{
  Vector3d v;
  unsigned short DFlags;
  short int ALPHA;
  int  scrx, scry, Light;
  float Fog;
};

struct ClipPoint
{
  EPoint ev;
  float tx, ty;
};
