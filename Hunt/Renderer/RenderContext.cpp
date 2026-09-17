// RenderContext.cpp — RenderFrameContext implementation
#include "Renderer/RenderContext.h"
#include "Core/GameState.h"
#include "Core/GameMode.h"
#include "Core/Constants.h"

RenderFrameContext RenderFrameContext::FromGlobals()
{
    RenderFrameContext ctx;

    // Viewport — use :: prefix to resolve global vs member name conflict
    ctx.winW = ::WinW;
    ctx.winH = ::WinH;
    ctx.videoCX = ::VideoCX;
    ctx.videoCY = ::VideoCY;
    ctx.videoPitch = ::VideoPitch;

    // Projection
    ctx.cameraW = ::CameraW;
    ctx.cameraH = ::CameraH;
    ctx.softPerspK = ::Soft_Persp_K;

    // Camera
    ctx.cameraX = ::CameraX;
    ctx.cameraY = ::CameraY;
    ctx.cameraZ = ::CameraZ;

    // Fog
    // FogRR/FogGG/FogBB not found in globals; use fog colour from first entity or fallback
    ctx.fogColor.x = static_cast<float>(::SkyR) / 255.0f;
    ctx.fogColor.y = static_cast<float>(::SkyG) / 255.0f;
    ctx.fogColor.z = static_cast<float>(::SkyB) / 255.0f;
    ctx.distanceFogColor = ctx.fogColor;
    ctx.fogDensity = 1.0f;
    ctx.underwater = IsUnderwater();
    ctx.fogEnabled = ::FOGENABLE != 0;
    ctx.cameraInFog = ::CAMERAINFOG != 0;
    ctx.ctViewR = ::ctViewR;
    ctx.forceFog = 0.0f;
    ctx.curFogColor = 0;
    ctx.cameraFogI = ::CameraFogI;

    // Sky
    ctx.skyColor.x = static_cast<float>(::SkyR) / 255.0f;
    ctx.skyColor.y = static_cast<float>(::SkyG) / 255.0f;
    ctx.skyColor.z = static_cast<float>(::SkyB) / 255.0f;
    ctx.skyTraceK = 1.0f;
    ctx.traceK = 1.0f;
    ctx.sunScrX = 0;
    ctx.sunScrY = 0;
    ctx.skyMin = ::SKYMin;
    ctx.skyDTime = ::SKYDTime;

    // Lighting — SunLight is computed per-frame in the renderer
    ctx.sunLight = 0.0f;  // computed by renderer
    ctx.hard3D = ::HARD3D != 0;

    // UI
    ctx.uiScale = 1.0f;
    ctx.lpVideoBuf = ::lpVideoBuf;
#ifdef _WIN32
    ctx.hbmpVideoBuf = ::hbmpVideoBuf;
    ctx.hdcCMain = ::hdcCMain;
#else
    ctx.hbmpVideoBuf = nullptr;
    ctx.hdcCMain = nullptr;
#endif

    // Night vision
    ctx.nightVisionOn = ::NightVisionOn != 0;
    ctx.nightVisionMode = ::NightVisionMode != 0;

    // Water
    ctx.waterReverse = ::WATERREVERSE != 0;
    ctx.waterClip = ::waterclip != 0;

    // Misc
    ctx.gourard = ::GOURAUD != 0;
    ctx.correction = ::CORRECTION != 0;
    ctx.clip3D = ::CLIP3D != 0;
    ctx.noDarkBack = ::NODARKBACK != 0;
    ctx.noClip = ::NOCLIP != 0;
    ctx.glassL = ::GlassL;
    ctx.shadowSize = 256;

    return ctx;
}
