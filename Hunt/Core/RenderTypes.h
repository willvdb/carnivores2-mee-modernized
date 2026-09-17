// RenderTypes.h — Rendering-related type definitions
// Extracted from Hunt.h (Phase 0.1 — Split god header into focused headers)
#pragma once

#include "Memory.h"
#include "Core/Constants.h"
#include "Core/MathTypes.h"
#include <array>
#include <cstddef>
#include <cstdint>

struct TMessageList
{
  int timeleft;
  char mtext[256];
};

struct TRGB
{
  unsigned char B;
  unsigned char G;
  unsigned char R;
};

struct TRes
{
  int w, h;
};

struct TEXTURE
{
  unsigned short DataA[128*128];
  unsigned short DataB[64*64];
  unsigned short DataC[32*32];
  unsigned short DataD[16*16];
  unsigned short SDataC[2][32*32];
  int mR, mG, mB;
};

struct TPicture
{
  int W, H;
  unique_heap_ptr<unsigned short[]> lpImage;
};

struct TFogEntity
{
  int fogRGB;
  float YBegin;
  int Mortal;
  float Transp, FLimit;
};

namespace TerrainUV {
using Triangle = std::array<Vector2df, 3>;

inline constexpr float kMin = static_cast<float>(TCMIN) / (128.0f * 65536.0f);
inline constexpr float kMax = static_cast<float>(TCMAX) / (128.0f * 65536.0f);

// Indexed by reverse:bit 3, second-triangle:bit 2, direction:bits 0-1.
// This preserves the exact legacy 16-way terrain/water texture mapping.
inline constexpr std::array<Triangle, 16> kTriangles = {{
    {{{kMin, kMin}, {kMax, kMin}, {kMax, kMax}}},
    {{{kMin, kMax}, {kMin, kMin}, {kMax, kMin}}},
    {{{kMax, kMax}, {kMin, kMax}, {kMin, kMin}}},
    {{{kMax, kMin}, {kMax, kMax}, {kMin, kMax}}},
    {{{kMin, kMin}, {kMax, kMax}, {kMin, kMax}}},
    {{{kMin, kMax}, {kMax, kMin}, {kMax, kMax}}},
    {{{kMax, kMax}, {kMin, kMin}, {kMax, kMin}}},
    {{{kMax, kMin}, {kMin, kMax}, {kMin, kMin}}},
    {{{kMin, kMin}, {kMax, kMin}, {kMin, kMax}}},
    {{{kMin, kMax}, {kMin, kMin}, {kMax, kMax}}},
    {{{kMax, kMax}, {kMin, kMax}, {kMax, kMin}}},
    {{{kMax, kMin}, {kMax, kMax}, {kMin, kMin}}},
    {{{kMin, kMax}, {kMax, kMin}, {kMax, kMax}}},
    {{{kMax, kMax}, {kMin, kMin}, {kMax, kMin}}},
    {{{kMax, kMin}, {kMin, kMax}, {kMin, kMin}}},
    {{{kMin, kMin}, {kMax, kMax}, {kMin, kMax}}}
}};

inline const Triangle& Get(bool reverse, bool second, int direction)
{
    const std::size_t index = (reverse ? 8u : 0u) |
                              (second ? 4u : 0u) |
                              static_cast<unsigned>(direction & 3);
    return kTriangles[index];
}
} // namespace TerrainUV
