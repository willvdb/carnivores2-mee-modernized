// AudioTypes.h — Audio-related type definitions
// Extracted from Hunt.h (Phase 0.1 — Split god header into focused headers)
#pragma once

#include <vector>
#include <cstdint>

struct TSFX
{
  int  length;
  std::vector<short int> lpData;
};

// Phase 5B.1: sizeof(TSFX) grows from 8 (int + raw pointer) to 16+ bytes.
static_assert(sizeof(TSFX) > 8,
              "TSFX is back to its old 8-std::uint8_t raw-pointer layout — the Phase 5B.1 "
              "std::vector migration was reverted. Re-apply the migration or update "
              "the doc.");

struct TRD
{
  int  RNumber, RVolume, RFreq;
  unsigned short REnvir, Flags;
};

struct TAmbient
{
  TSFX sfx;
  TRD  rdata[16];
  int  RSFXCount;
  int  AVolume;
  int  RndTime;
};

struct AudioQuad
{
  float x1, y1, z1;
  float x2, y2, z2;
  float x3, y3, z3;
  float x4, y4, z4;
};
