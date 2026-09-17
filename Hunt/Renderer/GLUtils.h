// ==========================================================================
// GLUtils.h — Shared helpers for OpenGL renderer files
// ==========================================================================

#pragma once

#include "Core/MathTypes.h"
#include "Core/Constants.h"
#include "Core/EngineAPI.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifdef _gl
#include "glad/glad.h"
#endif


// ---------- constants ----------
constexpr float kModelNearClip = -16.0f;

// Depth-range slice the viewmodel is drawn into. The weapon body and its
// specular/env-map overlays are the SAME triangles at the SAME depth, so
// they must be drawn under the SAME glDepthRange: a range mismatch makes
// the overlay's window depth 20x the stored value, and GL_LEQUAL silently
// discards every overlay fragment. Keep all three call sites on this
// constant, and restore (0, 1) when the viewmodel block ends.
constexpr double kViewmodelDepthRangeMax = 0.05;

// ---------- structs ----------
struct FogSample
{
    float amount;
    Vector3d color;
};

struct ModelClipVertex
{
    Vector3d position;
    Vector2df uv;
    float light;
};

// ---------- function declarations ----------
#ifdef _gl
GLuint CompileShader(GLenum type, const char* source);
GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader);
#endif

Vector3d DecodeFogColor(int rgb);
Vector3d DecodeFogColorBGR(int rgb);
// §3.6: sun-angle fog colour shift (warm/dim by sun elevation + visibility).
// Shared by the terrain fog colour (GetFogColorForMapPoint) and the
// model/water fog colour (CalcFogLevel -> CurFogColor) so the SAME
// shift reaches all three paths.  Pass sunLight <= 0.1f (or 0.0f)
// to skip the shift.  Deterministic in (fogRGB, sunLight), so calling
// it per vertex/per corner still yields a per-volume-uniform result.
int ApplySunFogColourShift(int fogRGB, float sunLight);
Vector3d GetFogColor();
Vector3d GetDistanceFogColor();
int GetFogIndexForMapPoint(int mapX, int mapY);
Vector2df DecodeLegacyFaceUV(float tx, float ty, int texHeight);
Vector3d TransformModelVertex(const TPoint3d& source, float x0, float y0, float z0, float ca, float sa, float cb, float sb);
bool ShouldCullModelFace(std::uint16_t flags, const Vector3d& p0, const Vector3d& p1, const Vector3d& p2);
float DistanceToWaterPlane(const Vector3d& position);
ModelClipVertex InterpolateClipVertex(const ModelClipVertex& a, const ModelClipVertex& b, float t);
void ClipTriangleAgainstWater(const ModelClipVertex& a,
    const ModelClipVertex& b,
    const ModelClipVertex& c,
    std::vector<ModelClipVertex>& output);

// Ensure the scene-copy texture exists and matches window size
#ifdef _gl
void EnsureNightSceneTex(GLuint& tex, int& texW, int& texH, int winW, int winH);
#endif
std::uint16_t Conv565to555(std::uint16_t c);

// Clipping / fog helpers. Point is camera-relative WORLD space (before
// camera rotation). The full sampler includes CPU horizon fog for consumers
// without shader distance fog (screen-space particles). World models and
// shadows use the pocket-only sampler because their fragment shader applies
// the common distance ramp.
FogSample SampleFogAtPoint(const Vector3d& point, bool disableFog);
FogSample SamplePocketFogAtPoint(const Vector3d& point, bool disableFog);

// Single implementation for the public APIs and hot exact/legacy model loops.
// The compile-time policy removes CPU horizon work from shader-fog consumers.
template<bool DistanceFallback>
inline FogSample SampleFogAtPointInline(const Vector3d& point, bool disableFog)
{
    if (disableFog) {
        return {0.0f, GetDistanceFogColor()};
    }

    // Preserve the existing underwater model treatment.
    if (IsUnderwater()) {
        float d = VectorLength(point);
        const TFogEntity& fog = FogsList[127];
        float fla = -(point.y + CameraY - fog.YBegin * ctHScale) / ctHScale;
        float flb = -(CameraY - fog.YBegin * ctHScale) / ctHScale;
        float fl = 0.0f;
        if (!(fla < 0.0f && flb < 0.0f)) {
            if (fla < 0.0f) { d *= flb / (flb - fla); fla = 0.0f; }
            if (flb < 0.0f) { d *= fla / (fla - flb); flb = 0.0f; }
            fl = std::clamp((fla + flb) * (d + fog.Transp * 0.5f) / fog.Transp,
                            0.0f, fog.FLimit);
        }

        if (fl <= 0.0f) {
            fl = (d + fog.Transp * 0.5f) / fog.Transp;
        }

        const float extinction = 1.0f - std::exp(-CameraWaterDepthFactor * 3.5f);
        fl *= 1.0f + extinction * 0.5f;

        const float vertDepth = (std::max)(0.0f, fog.YBegin * ctHScale - (point.y + CameraY));
        const float vertFactor = std::clamp(vertDepth / 512.0f, 0.0f, 1.0f);
        fl *= 1.0f + vertFactor * 2.5f;

        const float capBoost = (1.0f - std::exp(-CameraWaterDepthFactor * 2.0f)) * 50.0f;
        fl = (std::min)(fl, fog.FLimit + capBoost);

        const float amount = std::clamp(fl / 255.0f, 0.0f, (fog.FLimit + capBoost) / 255.0f);
        return {amount, DecodeFogColorBGR(fog.fogRGB)};
    }

    if (FOGON) {
        const int worldX = static_cast<int>(point.x + CameraX);
        const int worldZ = static_cast<int>(point.z + CameraZ);
        const int mapFogIndex = FogsMap[(worldZ >> 9) & 511][(worldX >> 9) & 511];
        // Preserve zero-index camera-pocket traversal, but skip the otherwise
        // side-effect-free CalcFogLevel clear-cell return.
        if (mapFogIndex != 0 || CAMERAINFOG) {
            const float amount = std::clamp(CalcFogLevel(point, mapFogIndex) / 255.0f, 0.0f, 1.0f);
            if (amount > 0.0f) {
                return {amount, DecodeFogColor(CurFogColor)};
            }
        }
    }

    if constexpr (!DistanceFallback) {
        return {0.0f, GetDistanceFogColor()};
    } else {
        const float d = VectorLength(point);
        const float fogDistance = static_cast<float>(ctViewR) * 256.0f;
        const float fogFadeStart = static_cast<float>(ctViewR) * 192.0f;
        const float fadeRange = fogDistance - fogFadeStart;
        const float distanceFog = std::clamp(
            (d - fogFadeStart) / (fadeRange > 1.0f ? fadeRange : 1.0f), 0.0f, 1.0f);
        return {distanceFog, GetDistanceFogColor()};
    }
}

// Terrain / water helpers
float VertexDistanceSq(const Vector3d& v);
float CalcTerrainAlpha(float distanceSq, float fadeStart, float fadeStartSq, float fadeEnd);
// Phase 5: overload with cached isUnderwater to avoid per-call global load
float CalcTerrainAlpha(float distanceSq, float fadeStart, float fadeStartSq, float fadeEnd, bool isUnderwater);
float GetTerrainFogAmountForMapPoint(int fogIndex, int legacyFog);
// Phase 5: overload with cached isUnderwater
float GetTerrainFogAmountForMapPoint(int fogIndex, int legacyFog, bool isUnderwater);
