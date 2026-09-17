// Hunt.h — Umbrella header (legacy compatibility, will be removed)
// All declarations have been moved to focused headers in Core/.
#pragma once

#define WIN32_LEAN_AND_MEAN

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include <type_traits>
#include <vector>
#include <array>

#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

#include "Memory.h"
#include "Platform/Files.h"
#include "math.h"
#ifdef _WIN32
#include "winuser.h"
#endif
#include "AppRes.h"
#ifdef _WIN32
#include "ddraw.h"
#endif

#include "Core/Strings.h"
#include "Core/LegacyKeys.h"
#include "Core/Constants.h"
#include "Core/MathTypes.h"
#include "Core/AudioTypes.h"
#include "Core/RenderTypes.h"
#include "Core/ModelTypes.h"
#include "Core/GameTypes.h"
#include "Core/GameState.h"
#include "Core/GameMode.h"

#include "Renderer/RenderContext.h"
#include "Core/EngineAPI.h"
#include "Debug/Log.h"

#ifdef _gl
#include "Renderer/GLRenderer.h"
#endif

#ifdef _soft
#include "Renderer/SoftRenderer.h"
#endif

#ifdef _d3d
#include "d3d.h"
#endif

// Runtime layout checks for the supported Windows x86/x64 ABIs. These objects
// own native pointers and are never serialized whole. Fixed disk record checks
// are kept separately in Core/GameTypes.h and tests/test_serialized_layout.cpp.
// Memory.h checks that the smart-pointer deleters add no storage overhead.
static_assert(sizeof(TAni) == (sizeof(void*) == 8 ? 56 : 48), "TAni runtime layout changed");
static_assert(sizeof(TVTL) == (sizeof(void*) == 8 ? 24 : 16), "TVTL runtime layout changed");
static_assert(sizeof(TPicture) == (sizeof(void*) == 8 ? 16 : 12), "TPicture runtime layout changed");
static_assert(sizeof(TBMPModel) == (sizeof(void*) == 8 ? 56 : 52), "TBMPModel runtime layout changed");
static_assert(sizeof(TModel) == (sizeof(void*) == 8 ? 88 : 52), "TModel runtime layout changed");

// Containers can vary with STL debug settings; these bounds are runtime sanity
// checks, not file sizes or offsets into a serialized object array.
static_assert(sizeof(TObject) >= 300 && sizeof(TObject) <= 400,
              "TObject runtime size is outside expected range");
static_assert(sizeof(TCharacterInfo) >= 3000 && sizeof(TCharacterInfo) <= 8000,
              "TCharacterInfo runtime size is outside expected range");

// The world-zoom factor Controls.cpp applies to CameraW/H each frame: the
// optic magnification while a scoped weapon is raised, 1 otherwise. HUD
// near-model renders (wind, compass) divide it back out so they stay
// pixel-stable while the world magnifies behind them.
inline float ActiveWorldZoom()
{
  if (IsScopeView() &&
      (!WeapInfo[CurrentWeapon].unzoom || Weapon.state == 2))
    return ScopePower;
  return 1.0f;
}
