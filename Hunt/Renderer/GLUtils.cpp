// ==========================================================================
// GLUtils.cpp — Shared helper implementations for OpenGL renderer
// ==========================================================================

#include "Hunt.h"
#include "GLRenderer.h"
#include "Renderer/GLUtils.h"
#include "Core/TerrainFog.h"

#ifdef _gl

#include "glad/glad.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

GLuint CompileShader(GLenum type, const char* source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        infoLog[sizeof(infoLog) - 1] = '\0';
        LOG_ERROR("Shader compilation failed: %s", infoLog);
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint LinkProgram(GLuint vertexShader, GLuint fragmentShader)
{
    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        infoLog[sizeof(infoLog) - 1] = '\0';
        LOG_ERROR("Program linking failed: %s", infoLog);
        glDeleteProgram(program);
        return 0;
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

Vector3d DecodeFogColor(int rgb)
{
    return {
        static_cast<float>(rgb & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f
    };
}

Vector3d DecodeFogColorBGR(int rgb)
{
    return {
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>(rgb & 0xFF) / 255.0f
    };
}

// §3.6: Sun-angle fog colour shift — warm by sun elevation (warmth)
// and dimmed by low sun visibility (shadowDim).  Factored out of the
// inline CalcFogLevel block so the SAME shift is applied to BOTH the
// terrain fog colour (via GetFogColorForMapPoint) and the model/water
// fog colour (via CurFogColor in CalcFogLevel).  Called per fog volume
// (per corner at most), so the result is uniform across a volume's
// vertices — not per-vertex.  sunLight is GLRenderer::GetSunLight();
// pass 0.0f to skip the shift entirely.
int ApplySunFogColourShift(int fogRGB, float sunLight)
{
    if (sunLight <= 0.1f) {
        return fogRGB;
    }
    // Sun elevation is constant for the fixed sun position; hoist the sqrt
    // out of the (per-volume) call path — it is a compile-time constant.
    static const float kSunElevation =
        4048.0f / std::sqrt(2048.0f * 2048.0f + 4048.0f * 4048.0f + 2048.0f * 2048.0f);
    const float warmth       = std::clamp(1.0f - kSunElevation, 0.0f, 0.5f);
    const float rBoost       = 1.0f + warmth * 0.15f;
    const float gBoost       = 1.0f + warmth * 0.05f;
    const float bBoost       = 1.0f - warmth * 0.10f;
    const float sunVisibility = std::clamp(sunLight / kMaxSunLight, 0.0f, 1.0f);
    const float shadowDim    = 0.6f + 0.4f * sunVisibility;
    const float r = static_cast<float>(fogRGB & 0xFF) / 255.0f;
    const float g = static_cast<float>((fogRGB >> 8) & 0xFF) / 255.0f;
    const float b = static_cast<float>((fogRGB >> 16) & 0xFF) / 255.0f;
    const int sr = static_cast<int>(std::min(1.0f, r * rBoost * shadowDim) * 255.0f) & 0xFF;
    const int sg = static_cast<int>(std::min(1.0f, g * gBoost * shadowDim * 0.9f) * 255.0f) & 0xFF;
    const int sb = static_cast<int>(std::max(0.0f, b * bBoost * shadowDim * 0.8f) * 255.0f) & 0xFF;
    return sr | (sg << 8) | (sb << 16);
}

Vector3d GetFogColor()
{
    if (IsUnderwater() && FogsList[127].fogRGB) {
        return DecodeFogColorBGR(FogsList[127].fogRGB);
    }

    if (CAMERAINFOG && CameraFogI > 0) {
        return DecodeFogColor(FogsList[CameraFogI].fogRGB);
    }

    return {
        static_cast<float>(SkyR) / 255.0f,
        static_cast<float>(SkyG) / 255.0f,
        static_cast<float>(SkyB) / 255.0f
    };
}

Vector3d GetDistanceFogColor()
{
    if (IsUnderwater() && FogsList[127].fogRGB) {
        return DecodeFogColorBGR(FogsList[127].fogRGB);
    }

    return {
        static_cast<float>(SkyR) / 255.0f,
        static_cast<float>(SkyG) / 255.0f,
        static_cast<float>(SkyB) / 255.0f
    };
}

int GetFogIndexForMapPoint(int mapX, int mapY)
{
    const int fogX = (mapX & (ctMapSize - 1)) >> 1;
    const int fogY = (mapY & (ctMapSize - 1)) >> 1;
    return FogsMap[fogY][fogX];
}

Vector2df DecodeLegacyFaceUV(float tx, float ty, int texHeight)
{
    const float h = static_cast<float>((texHeight > 1) ? texHeight : 1);
    return { tx / 256.0f, ty / h };
}

Vector3d TransformModelVertex(const TPoint3d& source, float x0, float y0, float z0, float ca, float sa, float cb, float sb)
{
    Vector3d result;
    result.x = source.x * ca + source.z * sa + x0;
    const float vz = source.z * ca - source.x * sa;
    result.y = source.y * cb - vz * sb + y0;
    result.z = vz * cb + source.y * sb + z0;
    return result;
}

bool ShouldCullModelFace(WORD flags, const Vector3d& p0, const Vector3d& p1, const Vector3d& p2)
{
    if ((flags & (sfDarkBack | sfNeedVC)) == 0) {
        return false;
    }

    const Vector3d edge1 = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
    const Vector3d edge2 = {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
    const Vector3d normal = {
        edge1.y * edge2.z - edge1.z * edge2.y,
        edge1.z * edge2.x - edge1.x * edge2.z,
        edge1.x * edge2.y - edge1.y * edge2.x
    };

    const float facing = normal.x * p0.x + normal.y * p0.y + normal.z * p0.z;
    return facing < 0.0f;
}

float DistanceToWaterPlane(const Vector3d& position)
{
    const float dx = position.x - waterclipbase.x;
    const float dy = position.y - waterclipbase.y;
    const float dz = position.z - waterclipbase.z;
    return dx * ClipW.nv.x + dy * ClipW.nv.y + dz * ClipW.nv.z;
}

ModelClipVertex InterpolateClipVertex(const ModelClipVertex& a, const ModelClipVertex& b, float t)
{
    return {
        { a.position.x + (b.position.x - a.position.x) * t,
          a.position.y + (b.position.y - a.position.y) * t,
          a.position.z + (b.position.z - a.position.z) * t },
        { a.uv.x + (b.uv.x - a.uv.x) * t,
          a.uv.y + (b.uv.y - a.uv.y) * t },
        a.light + (b.light - a.light) * t
    };
}

void ClipTriangleAgainstWater(const ModelClipVertex& a,
    const ModelClipVertex& b,
    const ModelClipVertex& c,
    std::vector<ModelClipVertex>& output)
{
    output.clear();
    output.reserve(4);

    const std::array<ModelClipVertex, 3> input = {a, b, c};

    const float da = DistanceToWaterPlane(a.position);
    const float db = DistanceToWaterPlane(b.position);
    const float dc = DistanceToWaterPlane(c.position);

    const bool aIn = da >= 0.0f;
    const bool bIn = db >= 0.0f;
    const bool cIn = dc >= 0.0f;

    const int inCount = (aIn ? 1 : 0) + (bIn ? 1 : 0) + (cIn ? 1 : 0);

    auto emit = [&](const ModelClipVertex& v) {
        output.push_back(v);
    };

    auto clipEdge = [&](const ModelClipVertex& v1, const ModelClipVertex& v2, float d1, float d2) -> ModelClipVertex {
        const float t = d1 / (d1 - d2);
        return InterpolateClipVertex(v1, v2, t);
    };

    if (inCount == 3) {
        emit(a); emit(b); emit(c);
    } else if (inCount == 2) {
        const ModelClipVertex *in1, *in2, *out1;
        float d_in1, d_in2, d_out1;
        if (!aIn) { out1 = &a; d_out1 = da; in1 = &b; d_in1 = db; in2 = &c; d_in2 = dc; }
        else if (!bIn) { out1 = &b; d_out1 = db; in1 = &a; d_in1 = da; in2 = &c; d_in2 = dc; }
        else { out1 = &c; d_out1 = dc; in1 = &a; d_in1 = da; in2 = &b; d_in2 = db; }
        emit(*in1);
        emit(*in2);
        emit(clipEdge(*in1, *out1, d_in1, d_out1));
        emit(clipEdge(*in2, *out1, d_in2, d_out1));
    } else if (inCount == 1) {
        const ModelClipVertex *in1, *out1, *out2;
        float d_in1, d_out1, d_out2;
        if (aIn) { in1 = &a; d_in1 = da; out1 = &b; d_out1 = db; out2 = &c; d_out2 = dc; }
        else if (bIn) { in1 = &b; d_in1 = db; out1 = &a; d_out1 = da; out2 = &c; d_out2 = dc; }
        else { in1 = &c; d_in1 = dc; out1 = &a; d_out1 = da; out2 = &b; d_out2 = db; }
        emit(*in1);
        emit(clipEdge(*in1, *out1, d_in1, d_out1));
        emit(clipEdge(*in1, *out2, d_in1, d_out2));
    }
}

void EnsureNightSceneTex(GLuint& tex, int& texW, int& texH, int winW, int winH)
{
    if (!tex || texW != winW || texH != winH) {
        if (!tex) glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, winW, winH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        texW = winW;
        texH = winH;
    }
}

WORD Conv565to555(WORD c)
{
    int r = (c >> 11) & 0x1F;
    int g = (c >> 5) & 0x3F;
    int b = c & 0x1F;
    return (r << 10) | ((g >> 1) << 5) | b;
}

float VertexDistanceSq(const Vector3d& v)
{
    return v.x * v.x + v.y * v.y + v.z * v.z;
}

float CalcTerrainAlpha(float distanceSq, float fadeStart, float fadeStartSq, float fadeEnd)
{
    return CalcTerrainAlpha(distanceSq, fadeStart, fadeStartSq, fadeEnd, IsUnderwater());
}

// Phase 5: overload with cached isUnderwater — avoids the per-call
// global IsUnderwater() load (~500K calls saved per frame at max view).
float CalcTerrainAlpha(float distanceSq, float fadeStart, float fadeStartSq, float fadeEnd, bool isUnderwater)
{
    if (isUnderwater) {
        return 1.0f;
    }

    // Phase 1: distanceSq in [0, fadeStartSq] → alpha = 1.0 (no fade)
    if (distanceSq <= fadeStartSq) {
        return 1.0f;
    }

    // Phase 2: distanceSq in (fadeStartSq, maxDistSq] → smooth fade
    // 765 = 255 * 3 (preserves original total fade zone length)
    const float maxDist = fadeEnd + 765.0f;
    const float maxDistSq = maxDist * maxDist;

    if (distanceSq >= maxDistSq) {
        return 0.0f;
    }

    // Normalised position within the fade zone: 0 at fadeEnd, 1 at maxDist
    const float distance = std::sqrt(distanceSq);
    float fadeZone = maxDist - fadeEnd;
    float t = std::clamp((distance - fadeEnd) / fadeZone, 0.0f, 1.0f);

    // Edge case — short fade distance: fall back to linear to prevent stretching
    if (fadeZone < 256.0f) {
        return 1.0f - t;
    }

    // Smoothstep (Hermite) ease-in-out curve:
    // t=0.0 → alpha=1.0
    // t=0.1 → alpha≈0.972  (subtle start)
    // t=0.5 → alpha=0.5
    // t=0.9 → alpha≈0.028  (gentle end)
    // t=1.0 → alpha=0.0
    return 1.0f - (t * t * (3.0f - 2.0f * t));
}

float GetTerrainFogAmountForMapPoint(int fogIndex, int legacyFog)
{
    return GetTerrainFogAmountForMapPoint(fogIndex, legacyFog, IsUnderwater());
}

// Phase 5: overload with cached isUnderwater — avoids the per-call
// global IsUnderwater() load.
float GetTerrainFogAmountForMapPoint(int fogIndex, int legacyFog, bool isUnderwater)
{
    return ResolveTerrainFogAmount(fogIndex, legacyFog, FOGON != 0,
        isUnderwater, CAMERAINFOG != 0, CameraFogI);
}
#endif // _gl
