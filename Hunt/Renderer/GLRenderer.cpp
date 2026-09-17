// ==========================================================================
// GLRenderer.cpp — OpenGL 3.3 Core Profile renderer for Carnivores 2 ME
// ==========================================================================

#include "Hunt.h"
#include "GLRenderer.h"

#ifdef _gl

#include "glad/glad.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Renderer/GLUtils.h"
#include "Platform/Platform.h"
#include "Core/TerrainFog.h"
#include "Core/WaterColor.h"  // §3.1: water-colour-aware depth modulation

GLRenderer* g_GLRenderer = nullptr;

GLRenderer::GLRenderer() = default;

GLRenderer::~GLRenderer()
{
    Shutdown();
}

bool GLRenderer::CreateContext()
{
    LOG_INFO("OpenGL initialization started");

    m_hasContext = Platform::CreateGLContext();
    if (!m_hasContext) return false;

    if (!gladLoadGLLoader(Platform::GLProcAddress)) {
        LOG_ERROR("Failed to initialize GLAD");
        return false;
    }

    const char* version = (const char*)glGetString(GL_VERSION);
    if (version) {
        LOG_INFO("OpenGL version: %s", version);
    }

    const char* vendor = (const char*)glGetString(GL_VENDOR);
    if (vendor) {
        LOG_INFO("OpenGL vendor: %s", vendor);
    }

    const char* rendererStr = (const char*)glGetString(GL_RENDERER);
    if (rendererStr) {
        LOG_INFO("OpenGL renderer: %s", rendererStr);
    }

    const char* glslVersion = (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (glslVersion) {
        LOG_INFO("GLSL version: %s", glslVersion);
    }

    GLint numExtensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
    LOG_INFO("OpenGL extensions: %d", numExtensions);

    LOG_INFO("OpenGL initialization completed");

    return true;
}

bool GLRenderer::InitGLState()
{
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, WinW, WinH);
    return true;
}

void GLRenderer::LoadGLExtensions()
{
#ifdef GL_PERF_HOOKS
    glperf_init();
#endif
}

bool GLRenderer::Initialize()
{
    LOG_INFO("GLRenderer::Initialize called");

    if (!CreateContext()) {
        LOG_ERROR("GLRenderer initialization failed in CreateContext");
        return false;
    }

    if (!InitGLState()) {
        LOG_ERROR("GLRenderer initialization failed in InitGLState");
        return false;
    }

    LoadGLExtensions();

    if (!m_terrainShader.LoadFromFile("shaders/terrain.vert", "shaders/terrain.frag")) {
        return false;
    }

    if (!m_modelShader.LoadFromFile("shaders/model.vert", "shaders/model.frag")) {
        return false;
    }

    // Phase 2.3+2.4: instanced model shader for geometry instancing.
    if (!m_instancedModelShader.LoadFromFile("shaders/instanced_model.vert", "shaders/instanced_model.frag")) {
        return false;
    }

    if (!InitializeTerrainPipeline()) {
        return false;
    }

    if (!InitializeModelPipeline()) {
        return false;
    }

    // Phase 2.3 fix: StaticMeshPipeline must initialize before
    // InstancingPipeline so m_instanceVAO can reference m_staticMeshVBO
    // (which doesn't exist yet if the order is reversed).
    if (!InitializeStaticMeshPipeline()) {
        return false;
    }

    if (!InitializeInstancingPipeline()) {
        return false;
    }

    InitializeSkyPipeline();
    InitializeHudPipeline();

    m_modelShader.Use();
    glUniform1i(glGetUniformLocation(m_modelShader.GetProgramID(), "uModelTexture"), 0);
    glUniform1f(glGetUniformLocation(m_modelShader.GetProgramID(), "uTintByFogColor"), 0.0f);

    // Phase 2.3: set instanced model shader's texture uniform.
    m_instancedModelShader.Use();
    glUniform1i(glGetUniformLocation(m_instancedModelShader.GetProgramID(), "uModelTexture"), 0);

    m_terrainShader.Use();
    glUniform1i(glGetUniformLocation(m_terrainShader.GetProgramID(), "uTerrainArray"), 0);

    Vector3d fogColor = GetDistanceFogColor();
    glClearColor(fogColor.x, fogColor.y, fogColor.z, 1.0f);

    m_uploadedTerrainTextures.fill(nullptr);

    // ---- Phase 1.1: PerFrame UBO (binding 0) shared by terrain, model, and sky shaders.
    // Create the UBO once and bind all three shaders' PerFrame blocks to binding 0.
    EnsurePerFrameUBO();
    if (m_perFrameUBO) {
        const GLuint perFrameBlock_terrain = glGetUniformBlockIndex(m_terrainShader.GetProgramID(), "PerFrame");
        if (perFrameBlock_terrain != GL_INVALID_INDEX) {
            glUniformBlockBinding(m_terrainShader.GetProgramID(), perFrameBlock_terrain, 0);
        }
        const GLuint perFrameBlock_model = glGetUniformBlockIndex(m_modelShader.GetProgramID(), "PerFrame");
        if (perFrameBlock_model != GL_INVALID_INDEX) {
            glUniformBlockBinding(m_modelShader.GetProgramID(), perFrameBlock_model, 0);
        }
        // Phase 2.3: bind instanced model shader's PerFrame UBO.
        const GLuint perFrameBlock_instanced = glGetUniformBlockIndex(m_instancedModelShader.GetProgramID(), "PerFrame");
        if (perFrameBlock_instanced != GL_INVALID_INDEX) {
            glUniformBlockBinding(m_instancedModelShader.GetProgramID(), perFrameBlock_instanced, 0);
        }
        const GLuint perFrameBlock_sky = glGetUniformBlockIndex(m_skyShader.GetProgramID(), "PerFrame");
        if (perFrameBlock_sky != GL_INVALID_INDEX) {
            glUniformBlockBinding(m_skyShader.GetProgramID(), perFrameBlock_sky, 0);
        }
        // Bind the UBO to binding 0 once. The binding persists for the
        // program's lifetime; we update the data with glBufferSubData.
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_perFrameUBO);
    }

    // ---- Phase 1.6, 1.9: cache uniform locations to eliminate per-draw
    // glGetUniformLocation calls. Sampler uniforms (uModelTexture, uTerrainArray,
    // uSkyTexture) are set once at init; tint/light uniforms are set per draw
    // using the cached location.
    m_locModelTexture    = glGetUniformLocation(m_modelShader.GetProgramID(), "uModelTexture");
    m_locModelTint       = glGetUniformLocation(m_modelShader.GetProgramID(), "uTintByFogColor");
    m_locSkyTexture      = glGetUniformLocation(m_skyShader.GetProgramID(), "uSkyTexture");
    m_locSkyViewport     = glGetUniformLocation(m_skyShader.GetProgramID(), "uViewport");
    m_locSkyVideoCenter  = glGetUniformLocation(m_skyShader.GetProgramID(), "uVideoCenter");
    m_locSkyQ            = glGetUniformLocation(m_skyShader.GetProgramID(), "uQ");
    m_locSkyP            = glGetUniformLocation(m_skyShader.GetProgramID(), "uP");
    m_locSkyR            = glGetUniformLocation(m_skyShader.GetProgramID(), "uR");
    m_locSkyTime         = glGetUniformLocation(m_skyShader.GetProgramID(), "uSkyTime");
    m_locSkyFogBase      = glGetUniformLocation(m_skyShader.GetProgramID(), "uFogBase");
    m_locSkyUnderwaterDepth = glGetUniformLocation(m_skyShader.GetProgramID(), "uUnderwaterDepth");
    m_locSkyWaterLineY     = glGetUniformLocation(m_skyShader.GetProgramID(), "uWaterLineY");
    m_locSkySunScreenPos   = glGetUniformLocation(m_skyShader.GetProgramID(), "uSunScreenPos");
    m_locSkySunVisibility  = glGetUniformLocation(m_skyShader.GetProgramID(), "uSunVisibility");
    m_locSkySunGlow        = glGetUniformLocation(m_skyShader.GetProgramID(), "uSunGlow");
    m_locSkyBodyIsMoon     = glGetUniformLocation(m_skyShader.GetProgramID(), "uBodyIsMoon");
    m_locSkyPocketFog      = glGetUniformLocation(m_skyShader.GetProgramID(), "uPocketFog");
    m_locSkyPocketFogColor = glGetUniformLocation(m_skyShader.GetProgramID(), "uPocketFogColor");
    m_locSkyCamRight       = glGetUniformLocation(m_skyShader.GetProgramID(), "uCamRight");
    m_locSkyCamUp          = glGetUniformLocation(m_skyShader.GetProgramID(), "uCamUp");
    m_locSkyCamForward     = glGetUniformLocation(m_skyShader.GetProgramID(), "uCamForward");

    m_Initialized = true;
    LOG_INFO("GLRenderer initialization completed successfully");
    return true;
}

void GLRenderer::Shutdown()
{
#ifdef GL_PERF_HOOKS
    glperf_shutdown();
#endif

    if (!m_Initialized && !m_hasContext) return;

    ShutdownTerrainPipeline();
    ShutdownModelPipeline();
    ShutdownInstancingPipeline();
    ShutdownStaticMeshPipeline();
    ShutdownSkyPipeline();
    ShutdownHudPipeline();

    DestroyContext();

    m_Initialized = false;
}

void GLRenderer::DestroyContext()
{
    Platform::DestroyGLContext();
    m_hasContext = false;
}












// ==========================================================================
// PerFrame UBO (Phase 1.1 + Phase 2.4)
// Shared by terrain and model shaders. std140 layout:
//   offset 0   : mat4  uProjection           (64 bytes)
//   offset 64  : vec2  uFogRange             ( 8 bytes)  (fadeStart, distance)
//   offset 72  :        (pad to vec3 align)  ( 8 bytes)
//   offset 80  : vec3  uDistanceFogColor     (12 bytes)
//   offset 92  : float uForceFog             ( 4 bytes)
//   offset 96  : vec3  uFogColor             (12 bytes)
//   offset 108 :        (pad to mat4 align)  ( 4 bytes)  -- Phase 2.4
//   offset 112 : mat4  uView                 (64 bytes)  -- Phase 2.4
//   total 192 bytes.
// Phase 2.4: uView is identity for legacy shaders (they receive view-
// space vertices).  The instanced shader uses it as a placeholder for
// the world→view split; currently identity, to be refined in 2.5+.
// ==========================================================================

void GLRenderer::EnsurePerFrameUBO()
{
    if (m_perFrameUBOInitialized) {
        return;
    }
    constexpr GLsizeiptr kUBOBytes = 240;  // Phase 2.4: +64 for uView, +16 for uWaterAlphaFade, +48 for §3.4+
    glGenBuffers(1, &m_perFrameUBO);
    glBindBuffer(GL_UNIFORM_BUFFER, m_perFrameUBO);
    glBufferData(GL_UNIFORM_BUFFER, kUBOBytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    m_perFrameUBOInitialized = true;
}

void GLRenderer::UpdatePerFrameUBO()
{
    UpdatePerFrameUBO(BuildLegacyProjection());
}

void GLRenderer::UpdatePerFrameUBO(const std::array<float, 16>& projection,
                                   float waterAlphaEnabled,
                                   float waterAlphaFadeStart,
                                   float waterAlphaFadeEnd,
                                   float waterAlphaFadeStep)
{
    if (!m_perFrameUBO || !m_perFrameUBOInitialized) {
        return;
    }

    // Caller-provided projection. World passes use BuildLegacyProjection();
    // near-model passes (weapon viewmodel + its Phong/env effects) use the
    // cached near-model projection from RenderNearModel; NDC-space passes
    // (full-screen glare, HUD circles) use the identity matrix. Previously
    // the no-arg overload was the only entry point and the caller-supplied
    // projection in DrawModelVertices was ignored, which broke the env-map
    // alignment on the weapon and the sun glare overlay (regression from
    // 6bda7780).
    // near-model path (wind/compass/weapon viewmodels) changes between
    // draws. BuildLegacyProjection is cheap enough that recomputing per
    // draw costs microseconds, vs. the visual regression of a stale
    // matrix.
    m_cachedProjection = projection;

    // Fog range only depends on ctViewR, but ctViewR can change at runtime
    // (widescreen FOV), so we recompute unconditionally too.
    m_cachedFogStart    = static_cast<float>(ctViewR) * 192.0f;
    m_cachedFogDistance = static_cast<float>(ctViewR) * 256.0f;

    // Distance fog color is read fresh from GetDistanceFogColor() (it can
    // change every frame as the smoothed sky color updates).
    const Vector3d distFogColor = GetDistanceFogColor();
    m_cachedDistanceFogColor[0] = distFogColor.x;
    m_cachedDistanceFogColor[1] = distFogColor.y;
    m_cachedDistanceFogColor[2] = distFogColor.z;

    // Sky fog color tracks the smoothed value in m_smoothedSkyFogColor.
    m_cachedFogColor[0] = m_smoothedSkyFogColor.x;
    m_cachedFogColor[1] = m_smoothedSkyFogColor.y;
    m_cachedFogColor[2] = m_smoothedSkyFogColor.z;

    // uForceFog is no longer referenced by any shader (the sky now uses
    // CameraWaterDepthFactor/uUnderwaterDepth and the 3dfx fog formula).
    // It is kept at offset 92 for UBO layout compatibility until all
    // PerFrame UBO declarations are updated in lockstep.
    m_cachedForceFog = 0.0f;

    // Phase 2.4: pack into a 60-float (240-byte) buffer.
    //   offset 0   : mat4 uProjection           (16 floats)
    //   offset 64  : vec2 uFogRange             ( 2 floats)
    //   offset 72  :        (pad to vec3 align) ( 2 floats)
    //   offset 80  : vec3 uDistanceFogColor     ( 3 floats)
    //   offset 92  : float uForceFog            ( 1 float)  -- DEPRECATED, kept for layout
    //   offset 96  : vec3 uFogColor             ( 3 floats)
    //   offset 108 :        (pad to mat4 align) ( 1 float)   -- Phase 2.4
    //   offset 112 : mat4 uView                 (16 floats)   -- Phase 2.4
    //   offset 176 : vec4 uWaterAlphaFade       ( 4 floats)   -- x=start, y=end, z=enabled, w=fade step
    //   offset 192 : float uWaterDepthFactor    ( 1 float)    -- §3.4
    //   offset 196 : float uCloudCover           ( 1 float)    -- §3.7
    std::array<float, 60> data{};
    std::memcpy(&data[0],  m_cachedProjection.data(), 16 * sizeof(float));
    data[16] = m_cachedFogStart;     // uFogRange.x
    data[17] = m_cachedFogDistance;  // uFogRange.y
    data[18] = 0.0f;                 // pad to vec3 alignment
    data[19] = 0.0f;                 // pad to vec3 alignment
    data[20] = m_cachedDistanceFogColor[0];
    data[21] = m_cachedDistanceFogColor[1];
    data[22] = m_cachedDistanceFogColor[2];
    data[23] = m_cachedForceFog;
    data[24] = m_cachedFogColor[0];
    data[25] = m_cachedFogColor[1];
    data[26] = m_cachedFogColor[2];
    data[27] = 0.0f;                 // pad to mat4 alignment (Phase 2.4)
    data[28] = 1.0f; data[29] = 0.0f; data[30] = 0.0f; data[31] = 0.0f;  // col0
    data[32] = 0.0f; data[33] = 1.0f; data[34] = 0.0f; data[35] = 0.0f;  // col1
    data[36] = 0.0f; data[37] = 0.0f; data[38] = 1.0f; data[39] = 0.0f;  // col2
    data[40] = 0.0f; data[41] = 0.0f; data[42] = 0.0f; data[43] = 1.0f;  // col3
    // Phase 2.4: uWaterAlphaFade packed after uView.
    data[44] = waterAlphaFadeStart;   // uWaterAlphaFade.x
    data[45] = waterAlphaFadeEnd;     // uWaterAlphaFade.y
    data[46] = waterAlphaEnabled;     // uWaterAlphaFade.z
    data[47] = waterAlphaFadeStep;    // uWaterAlphaFade.w
    // §3.4: wavelength attenuation on terrain
    data[48] = m_isUnderwater ? CameraWaterDepthFactor : 0.0f;  // uWaterDepthFactor
    // §3.7: cloud colour temperature — overcast (low sun visibility) shifts
    // terrain light cool/blue.  clamp(1 - traceK*1.2, 0, 1): fully clear at
    // traceK≈0.83+, fully overcast at traceK=0.
    data[49] = (std::max)(0.0f, (std::min)(1.0f, 1.0f - m_skyTraceK * 1.2f));  // uCloudCover

    glBindBuffer(GL_UNIFORM_BUFFER, m_perFrameUBO);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, data.size() * sizeof(float), data.data());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}


















void GLRenderer::ApplyModelDistanceFade(const Vector3d& rpos)
{
    const float distanceSq = VectorLengthSq(rpos);
    // Use the same fade range as terrain (smoothstep curve)
    const float modelFadeStart = static_cast<float>((ctViewR - 8) << 8);
    const float modelFadeEnd = 256.0f * static_cast<float>(ctViewR - 4);

    m_modelDistanceAlpha = CalcTerrainAlpha(distanceSq, modelFadeStart,
                                            modelFadeStart * modelFadeStart,
                                            modelFadeEnd, m_isUnderwater);
    // Keep GlassL = 0 for the GL path (legacy renderers still use it)
    GlassL = 0;
}


void GLRenderer::Render3DHardwarePosts()
{
#ifdef GL_PERF_HOOKS
    GL_PERF_SCOPE("Render3DHardwarePosts");
#endif

    // ── Characters (dinosaurs, hunters) ──────────────────────────────
    for (int c = 0; c < ChCount; c++) {
        TCharacter* cptr = &Characters[c];
        cptr->rpos.x = cptr->pos.x - CameraX;
        cptr->rpos.y = cptr->pos.y - CameraY;
        cptr->rpos.z = cptr->pos.z - CameraZ;

        float r = static_cast<float>((std::max)(fabs(cptr->rpos.x), fabs(cptr->rpos.z)));
        int ri = -1 + static_cast<int>(r / 256.0f + 0.5f);
        if (ri < 0) ri = 0;
        if (ri > ctViewR) continue;

        cptr->rpos = RotateVector(cptr->rpos);

        float br = BackViewR + DinoInfo[cptr->CType].Radius;
        if (cptr->rpos.z > br) continue;
        if (fabs(cptr->rpos.x) > -cptr->rpos.z + br) continue;
        if (fabs(cptr->rpos.y) > -cptr->rpos.z + br) continue;

        // Morph the character model
        CreateChMorphedModel(cptr);

        float zs = sqrtf(cptr->rpos.x * cptr->rpos.x +
                         cptr->rpos.y * cptr->rpos.y +
                         cptr->rpos.z * cptr->rpos.z);
        // Step 4: Extend culling distance to allow fade-out to complete
        const float extendedViewR = ctViewR * 256.0f + 765.0f;
        if (zs > extendedViewR) continue;

        // Use CalcTerrainAlpha for smoothstep model fade
        const float modelFadeStart = static_cast<float>((ctViewR - 8) << 8);
        const float modelFadeEnd = 256.0f * static_cast<float>(ctViewR - 4);
        m_modelDistanceAlpha = CalcTerrainAlpha(zs * zs, modelFadeStart,
                                                modelFadeStart * modelFadeStart,
                                                modelFadeEnd, m_isUnderwater);
        GlassL = 0;  // Legacy compatibility

        // Cull fully transparent models
        if (m_modelDistanceAlpha <= 0.005f) continue;

        waterclip = false;

        if (cptr->rpos.z > -256.0f * 10.0f)
            RenderModelClip(cptr->pinfo->mptr.get(),
                            cptr->rpos.x, cptr->rpos.y, cptr->rpos.z, 210, 0,
                            -cptr->alpha + pi / 2.0f + CameraAlpha,
                            CameraBeta);
        else
            RenderModel(cptr->pinfo->mptr.get(),
                        cptr->rpos.x, cptr->rpos.y, cptr->rpos.z, 210, 0,
                        -cptr->alpha + pi / 2.0f + CameraAlpha,
                        CameraBeta);
    }

    // ── Multiplayer players ──────────────────────────────────────────
    if (Multiplayer) {
        for (int c = 0; c < 1; c++) {
            TCharacter* cptr = &MPlayers[c];
            cptr->rpos.x = cptr->pos.x - CameraX;
            cptr->rpos.y = cptr->pos.y - CameraY;
            cptr->rpos.z = cptr->pos.z - CameraZ;

            float r = static_cast<float>((std::max)(fabs(cptr->rpos.x), fabs(cptr->rpos.z)));
            int ri = -1 + static_cast<int>(r / 256.0f + 0.5f);
            if (ri < 0) ri = 0;
            if (ri > ctViewR) continue;

            cptr->rpos = RotateVector(cptr->rpos);

            float br = BackViewR + DinoInfo[cptr->CType].Radius;
            if (cptr->rpos.z > br) continue;
            if (fabs(cptr->rpos.x) > -cptr->rpos.z + br) continue;
            if (fabs(cptr->rpos.y) > -cptr->rpos.z + br) continue;

            CreateChMorphedModel(cptr);

            float zs = sqrtf(cptr->rpos.x * cptr->rpos.x +
                             cptr->rpos.y * cptr->rpos.y +
                             cptr->rpos.z * cptr->rpos.z);
            // Step 4: Extend culling distance to allow fade-out to complete
            const float extendedViewR = ctViewR * 256.0f + 765.0f;
            if (zs > extendedViewR) continue;

            // Use CalcTerrainAlpha for smoothstep model fade
            const float modelFadeStart = static_cast<float>((ctViewR - 8) << 8);
            const float modelFadeEnd = 256.0f * static_cast<float>(ctViewR - 4);
            m_modelDistanceAlpha = CalcTerrainAlpha(zs * zs, modelFadeStart,
                                                    modelFadeStart * modelFadeStart,
                                                    modelFadeEnd, m_isUnderwater);
            GlassL = 0;  // Legacy compatibility

            // Cull fully transparent models
            if (m_modelDistanceAlpha <= 0.005f) continue;

            waterclip = false;

            if (cptr->rpos.z > -256.0f * 10.0f)
                RenderModelClip(cptr->pinfo->mptr.get(),
                                cptr->rpos.x, cptr->rpos.y, cptr->rpos.z, 210, 0,
                                -cptr->alpha + pi / 2.0f + CameraAlpha,
                                CameraBeta);
            else
                RenderModel(cptr->pinfo->mptr.get(),
                            cptr->rpos.x, cptr->rpos.y, cptr->rpos.z, 210, 0,
                            -cptr->alpha + pi / 2.0f + CameraAlpha,
                            CameraBeta);
        }
    }

    // ── Ship ─────────────────────────────────────────────────────────
    Ship.rpos.x = Ship.pos.x - CameraX;
    Ship.rpos.y = Ship.pos.y - CameraY;
    Ship.rpos.z = Ship.pos.z - CameraZ;
    {
        float r = static_cast<float>((std::max)(fabs(Ship.rpos.x), fabs(Ship.rpos.z)));
        int ri = -1 + static_cast<int>(r / 256.0f + 0.2f);
        if (ri < 0) ri = 0;
        if (ri < ctViewR) {
            Ship.rpos = RotateVector(Ship.rpos);
            if (Ship.rpos.z <= BackViewR &&
                fabs(Ship.rpos.x) <= -Ship.rpos.z + BackViewR) {
                if (Ship.State != -1) {
                    ApplyModelDistanceFade(Ship.rpos);

                    CreateMorphedModel(ShipModel.mptr.get(), &ShipModel.Animation[0], Ship.FTime, 1.0);

                    if (fabs(Ship.rpos.z) < 4000.0f)
                        RenderModelClip(ShipModel.mptr.get(),
                                        Ship.rpos.x, Ship.rpos.y, Ship.rpos.z, 210, 0,
                                        -Ship.alpha - pi / 2.0f + CameraAlpha, CameraBeta);
                    else
                        RenderModel(ShipModel.mptr.get(),
                                    Ship.rpos.x, Ship.rpos.y, Ship.rpos.z, 210, 0,
                                    -Ship.alpha - pi / 2.0f + CameraAlpha, CameraBeta);
                }
            }
        }
    }

    // ── Super Ship ───────────────────────────────────────────────────
    SShip.rpos.x = SShip.pos.x - CameraX;
    SShip.rpos.y = SShip.pos.y - CameraY;
    SShip.rpos.z = SShip.pos.z - CameraZ;
    {
        float r = static_cast<float>((std::max)(fabs(SShip.rpos.x), fabs(SShip.rpos.z)));
        int ri = -1 + static_cast<int>(r / 256.0f + 0.2f);
        if (ri < 0) ri = 0;
        if (ri < ctViewR) {
            SShip.rpos = RotateVector(SShip.rpos);
            if (SShip.rpos.z <= BackViewR &&
                fabs(SShip.rpos.x) <= -SShip.rpos.z + BackViewR) {
                if (SShip.State >= 1) {
                    ApplyModelDistanceFade(SShip.rpos);

                    CreateMorphedModelBetaGamma(SShipModel.mptr.get(), &SShipModel.Animation[0],
                                                SShip.FTime, 1.0, SShip.beta, SShip.gamma);

                    if (fabs(SShip.rpos.z) < 4000.0f)
                        RenderModelClip(SShipModel.mptr.get(),
                                        SShip.rpos.x, SShip.rpos.y, SShip.rpos.z, 210, 0,
                                        -SShip.alpha - pi / 2.0f + CameraAlpha, CameraBeta);
                    else
                        RenderModel(SShipModel.mptr.get(),
                                    SShip.rpos.x, SShip.rpos.y, SShip.rpos.z, 210, 0,
                                    -SShip.alpha - pi / 2.0f + CameraAlpha, CameraBeta);
                }
            }
        }
    }

    // ── Ammo Bag ─────────────────────────────────────────────────────
    AmmoBag.rpos.x = AmmoBag.pos.x - CameraX;
    AmmoBag.rpos.y = AmmoBag.pos.y - CameraY;
    AmmoBag.rpos.z = AmmoBag.pos.z - CameraZ;
    {
        float r = static_cast<float>((std::max)(fabs(AmmoBag.rpos.x), fabs(AmmoBag.rpos.z)));
        int ri = -1 + static_cast<int>(r / 256.0f + 0.2f);
        if (ri < 0) ri = 0;
        if (ri < ctViewR) {
            AmmoBag.rpos = RotateVector(AmmoBag.rpos);
            if (AmmoBag.rpos.z <= BackViewR &&
                fabs(AmmoBag.rpos.x) <= -AmmoBag.rpos.z + BackViewR) {
                if (AmmoBag.State >= 1) {
                    ApplyModelDistanceFade(AmmoBag.rpos);

                    CreateMorphedModel(BagModel.mptr.get(), &BagModel.Animation[0], AmmoBag.FTime, 1.0);

                    if (fabs(AmmoBag.rpos.z) < 4000.0f)
                        RenderModelClip(BagModel.mptr.get(),
                                        AmmoBag.rpos.x, AmmoBag.rpos.y, AmmoBag.rpos.z, 210, 0,
                                        -pi / 2.0f + CameraAlpha, CameraBeta);
                    else
                        RenderModel(BagModel.mptr.get(),
                                    AmmoBag.rpos.x, AmmoBag.rpos.y, AmmoBag.rpos.z, 210, 0,
                                    -pi / 2.0f + CameraAlpha, CameraBeta);
                }
            }
        }
    }

    // ── Bullets ──────────────────────────────────────────────────────
    for (int b = 0; b < bulletCh; b++) {
        if (!WeapInfo[bullet[b].parent].bullet) continue;

        bullet[b].rpos.x = bullet[b].a.x - CameraX;
        bullet[b].rpos.y = bullet[b].a.y - CameraY;
        bullet[b].rpos.z = bullet[b].a.z - CameraZ;
        float r = static_cast<float>((std::max)(fabs(bullet[b].rpos.x), fabs(bullet[b].rpos.z)));
        int ri = -1 + static_cast<int>(r / 256.0f + 0.2f);
        if (ri < 0) ri = 0;
        if (ri < ctViewR) {
            bullet[b].rpos = RotateVector(bullet[b].rpos);
            if (bullet[b].rpos.z <= BackViewR &&
                fabs(bullet[b].rpos.x) <= -bullet[b].rpos.z + BackViewR) {
                ApplyModelDistanceFade(bullet[b].rpos);

                CreateMorphedModelBetaGamma(Weapon.Bullet[bullet[b].parent].mptr.get(),
                                            &Weapon.Bullet[bullet[b].parent].Animation[0],
                                            bullet[b].FTime, 1.0, bullet[b].beta, 0.0f);

                // Same formula as ships (which are authored long-axis-in-Z just
                // like these projectiles — verified from the .car extents) and
                // as the untouched vanilla software path below: the morph above
                // already baked the trajectory pitch into the vertices, so the
                // render adds only yaw + camera pitch. The -beta - pi/2 that
                // was here tilted bolts off-axis (double pitch + quarter turn),
                // so stuck bolts visibly swung with the camera.
                if (fabs(bullet[b].rpos.z) < 4000.0f)
                    RenderModelClip(Weapon.Bullet[bullet[b].parent].mptr.get(),
                                    bullet[b].rpos.x, bullet[b].rpos.y, bullet[b].rpos.z, 210, 0,
                                    -bullet[b].alpha - pi / 2.0f + CameraAlpha,
                                    CameraBeta);
                else
                    RenderModel(Weapon.Bullet[bullet[b].parent].mptr.get(),
                                bullet[b].rpos.x, bullet[b].rpos.y, bullet[b].rpos.z, 210, 0,
                                -bullet[b].alpha - pi / 2.0f + CameraAlpha,
                                CameraBeta);
            }
        }
    }

    // Flush all queued models (characters, ships, bullets) to GPU
    RenderWorldModels();
}

// §3.1: Underwater full-screen overlay — restores the missing colour wash
// that both legacy C2 renderers (D3D and 3DFX) applied when submerged.
// The overlay uses CurFogColor (already set to WaterList[w].fogRGB by
// Controls.cpp) for dynamic per-water-body colouring, and darkens with
// CameraWaterDepthFactor.  Called from ShowVideo() (GLUI.cpp) like the
// legacy renderers.
void GLRenderer::DrawUnderwaterOverlay()
{
    if (!m_isUnderwater) return;

    float depth = CameraWaterDepthFactor;          // 0 at surface, 1 at ~1024u

    // CurFogColor is the depth-modulated water fog colour, refreshed every
    // frame by Controls.cpp §3.2 (via FogsList[127].fogRGB).  It is packed
    // BGR: bits 0-7 = Blue, 8-15 = Green, 16-23 = Red (DecodeFogColorBGR).
    int baseB = CurFogColor & 0xFF;
    int baseG = (CurFogColor >> 8) & 0xFF;
    int baseR = (CurFogColor >> 16) & 0xFF;

    // Base alpha matches original C2 D3D: ~44%, deeper at depth.
    float alpha = 0.44f + depth * 0.30f;           // up to ~0.74 at max depth

    // The per-water-body depth tint is already baked into CurFogColor by §3.2,
    // so the overlay simply washes that (correctly hued) colour over the
    // scene.  No extra blue-biased channel attenuation here — the old code
    // forced brown swamp water toward blue at depth even after §3.2 had
    // already tinted it correctly.
    // RenderFSRect decodes the packed colour as r = bits 16-23, g = bits 8-15,
    // b = bits 0-7 (i.e. red in the HIGH byte, blue in the LOW byte), so the
    // physical channels must be placed accordingly.  CurFogColor is BGR
    // (low byte = Blue, high byte = Red), hence baseR is the high byte and
    // baseB is the low byte below.
    uint32_t packed =
        (std::clamp(baseR, 0, 255) << 16) |
        (std::clamp(baseG, 0, 255) <<  8) |
        (std::clamp(baseB, 0, 255))       |
        (std::clamp(static_cast<int>(alpha * 255.0f), 0, 255) << 24);

    // Standard blend (not additive) — overlay recolours and dims toward the
    // water body's own depth colour.
    RenderFSRect(packed, false);
}

// ── 2.17 Elements batching ──────────────────────────────────────────────
// Builds one element "circle" (octagon, matching D3D/3DFX) into `out` using the
// exact geometry/colour/depth math of RenderCircle(). Shared by the batched
// instanced path (RenderElements) so the optimised and fallback paths cannot
// diverge. RenderCircle() itself is left untouched as the per-element fallback.
void GLRenderer::BuildElementOctagon(std::vector<ModelVertex>& out,
                                float cx, float cy, float z,
                                float R, uint32_t RGBA, uint32_t RGBA2)
{
    auto unpackABGR = [](uint32_t c, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
        a = static_cast<uint8_t>((c >> 24) & 0xFF);
        b = static_cast<uint8_t>((c >> 16) & 0xFF);
        g = static_cast<uint8_t>((c >> 8) & 0xFF);
        r = static_cast<uint8_t>(c & 0xFF);
    };
    uint8_t cr, cg, cb, ca;
    uint8_t er, eg, eb, ea;
    unpackABGR(RGBA, cr, cg, cb, ca);
    unpackABGR(RGBA2, er, eg, eb, ea);

    float r  = floorf(R * 16.0f) / 16.0f;
    float r2 = floorf(0.65f * R * 16.0f) / 16.0f;

    float ndcX = (cx - VideoCX) / VideoCX;
    float ndcY = (VideoCY - cy) / VideoCY;

    const float nearPlane = 16.0f;
    const float farPlane  = static_cast<float>(ctViewR) * 256.0f + 4096.0f;
    const float fpn = farPlane + nearPlane;
    const float fmn = farPlane - nearPlane;
    float ndcZ_depth = 1.0f;
    if (z < 0.0f) {
        ndcZ_depth = fpn / fmn + (2.0f * farPlane * nearPlane) / (fmn * z);
        if (ndcZ_depth < -1.0f) ndcZ_depth = -1.0f;
        if (ndcZ_depth >  1.0f) ndcZ_depth =  1.0f;
    }

    const uint8_t lightByte  = 255;
    const uint8_t fogByte    = 255;
    const uint8_t cutoutByte = 0;

    auto makeCircleVertex = [&](float x, float y, uint8_t vr, uint8_t vg,
                                uint8_t vb, uint8_t va) -> ModelVertex {
        return {x, y, ndcZ_depth, 0.0f, 0.0f,
                lightByte, fogByte, va, cutoutByte,
                vr, vg, vb, {0,0,0,0,0}};
    };

    const float dx[8] = { 0.0f,  r2,  r,  r2,  0.0f, -r2, -r, -r2 };
    const float dy[8] = { -r,   -r2, 0.0f, r2,   r,    r2,  0.0f, -r2 };

    for (int i = 0; i < 8; i++) {
        int next = (i + 1) % 8;
        out.push_back(makeCircleVertex(ndcX, ndcY, cr, cg, cb, ca));
        float ex1 = ndcX + dx[i] / VideoCX;
        float ey1 = ndcY - dy[i] / VideoCY;
        out.push_back(makeCircleVertex(ex1, ey1, er, eg, eb, ea));
        float ex2 = ndcX + dx[next] / VideoCX;
        float ey2 = ndcY - dy[next] / VideoCY;
        out.push_back(makeCircleVertex(ex2, ey2, er, eg, eb, ea));
    }
}

// Draws a whole batch of element octagons in ONE call (2.17). Replicates
// RenderCircle()'s exact GL state setup/teardown so behaviour is identical to
// the per-element path, just with a single draw + buffer orphan.
void GLRenderer::DrawElementBatch(const std::vector<ModelVertex>& batch)
{
    if (batch.empty()) return;

    const std::array<float, 16> identity = {
        1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f
    };
    UpdatePerFrameUBO(identity);
    m_modelShader.Use();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
#ifdef GL_PERF_HOOKS
    GL_PERF_TEXTURE_BIND(m_whiteTexture);
#endif
    glBindVertexArray(m_modelVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_modelVBO);
    const GLsizeiptr vertexSize = static_cast<GLsizeiptr>(batch.size() * sizeof(ModelVertex));
    glBufferData(GL_ARRAY_BUFFER, vertexSize, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexSize, batch.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.size()));
#ifdef GL_PERF_HOOKS
    GL_PERF_DRAW(static_cast<uint32_t>(batch.size()) / 3);
#endif
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void GLRenderer::RenderCircle(float cx, float cy, float z, float R, uint32_t RGBA, uint32_t RGBA2)
{
    // The game stores colors in ABGR format (R in bits 0-7, B in bits 16-23).
    // D3D vertex colors are ARGB, so D3D inherently swaps R↔B when reading.
    // We must do the same: extract ABGR and pass as-is to the shader.
    auto unpackABGR = [](uint32_t c, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
        a = static_cast<uint8_t>((c >> 24) & 0xFF);
        b = static_cast<uint8_t>((c >> 16) & 0xFF);
        g = static_cast<uint8_t>((c >> 8) & 0xFF);
        r = static_cast<uint8_t>(c & 0xFF);
    };

    uint8_t cr, cg, cb, ca;
    uint8_t er, eg, eb, ea;
    unpackABGR(RGBA, cr, cg, cb, ca);
    unpackABGR(RGBA2, er, eg, eb, ea);

    // Clamp radius to sub-pixel precision (matching D3D)
    float r  = floorf(R * 16.0f) / 16.0f;
    float r2 = floorf(0.65f * R * 16.0f) / 16.0f;

    // Convert screen-space position to NDC (-1 to 1)
    float ndcX = (cx - VideoCX) / VideoCX;
    float ndcY = (VideoCY - cy) / VideoCY;

    // Compute proper NDC depth from camera-space z so particles are
    // correctly occluded by scene geometry. The hard-coded z=0.0001
    // mapped every particle to the middle of the depth buffer, causing
    // them to draw in front of almost everything.
    const float nearPlane = 16.0f;
    const float farPlane  = static_cast<float>(ctViewR) * 256.0f + 4096.0f;
    const float fpn = farPlane + nearPlane;
    const float fmn = farPlane - nearPlane;
    float ndcZ_depth = 1.0f;          // default: far plane (behind everything)
    if (z < 0.0f) {
        // Standard OpenGL perspective projection of view-space z into NDC [-1,1].
        ndcZ_depth = fpn / fmn + (2.0f * farPlane * nearPlane) / (fmn * z);
        if (ndcZ_depth < -1.0f) ndcZ_depth = -1.0f;
        if (ndcZ_depth >  1.0f) ndcZ_depth =  1.0f;
    }

    // Use the packed ModelVertex layout (Phase 1.4: 32 bytes, color
    // attributes are uint8 normalized). The model VAO's attribute
    // pointers expect this layout; the old CircleVertex used floats
    // for color fields, which the driver read as raw bytes — producing
    // garbage colors (same regression as RenderFSRect, fixed in d3c7d25).
    //
    // fog=255 (normalized to 1.0) makes the model shader output the
    // fog color (our desired particle color) instead of the texture.
    const uint8_t lightByte  = 255;
    const uint8_t fogByte    = 255;
    const uint8_t cutoutByte = 0;

    // 8 triangles forming an octagon with alternating outer/inner radius
    // Matches D3D: angle 0=R, 45=R2, 90=R, 135=R2, ...
    std::vector<ModelVertex> vertices;
    vertices.reserve(24);

    auto makeCircleVertex = [&](float x, float y, uint8_t vr, uint8_t vg,
                               uint8_t vb, uint8_t va) -> ModelVertex {
        return {x, y, ndcZ_depth, 0.0f, 0.0f,
                lightByte, fogByte, va, cutoutByte,
                vr, vg, vb, {0,0,0,0,0}};
    };

    // 8 screen-space offsets matching D3D/3DFX octagon layout.
    // The diagonals are at (±R2, ±R2) giving effective radius ≈0.919·R,
    // producing a much smoother octagon than the old polar code that
    // placed diagonal vertices at only 0.65·R (making them look pointy).
    const float dx[8] = { 0.0f,  r2,  r,  r2,  0.0f, -r2, -r, -r2 };
    const float dy[8] = { -r,   -r2, 0.0f, r2,   r,    r2,  0.0f, -r2 };

    for (int i = 0; i < 8; i++) {
        int next = (i + 1) % 8;

        // Triangle: center, vertex i, vertex i+1
        vertices.push_back(makeCircleVertex(ndcX, ndcY, cr, cg, cb, ca));

        float ex1 = ndcX + dx[i] / VideoCX;
        float ey1 = ndcY - dy[i] / VideoCY;
        vertices.push_back(makeCircleVertex(ex1, ey1, er, eg, eb, ea));

        float ex2 = ndcX + dx[next] / VideoCX;
        float ey2 = ndcY - dy[next] / VideoCY;
        vertices.push_back(makeCircleVertex(ex2, ey2, er, eg, eb, ea));
    }

    // 2D HUD circles: vertices are in NDC space. Upload the identity
    // projection to the UBO so gl_Position = projection * vec4(pos, 1)
    // passes the NDC coordinates through unchanged.
    const std::array<float, 16> identity = {
        1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f
    };

    UpdatePerFrameUBO(identity);
    m_modelShader.Use();
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    // uProjection in PerFrame UBO (Phase 1.1)

    glEnable(GL_DEPTH_TEST);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glEnable(GL_BLEND);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
#ifdef GL_PERF_HOOKS
    GL_PERF_TEXTURE_BIND(m_whiteTexture);
#endif
    glBindVertexArray(m_modelVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_modelVBO);
    const GLsizeiptr vertexSize = static_cast<GLsizeiptr>(vertices.size() * sizeof(ModelVertex));
    glBufferData(GL_ARRAY_BUFFER, vertexSize, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexSize, vertices.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
#ifdef GL_PERF_HOOKS
    GL_PERF_DRAW(static_cast<uint32_t>(vertices.size()) / 3);
#endif
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

// Blend an ABGR color (R in bits 0-7, B in 16-23, A in 24-31) toward the
// current fog color at the given world position, preserving the source
// alpha. Used to fog particles, blood trails, and snow — the legacy D3D/3DFX
// renderers pre-baked fog into the element RGBA in Game.cpp, but the GL
// pipeline doesn't reuse that, so we apply the fog here at draw time using
// the same CalcFogLevel() the rest of the scene uses.
static uint32_t ApplyFogToABGR(uint32_t abgr, const Vector3d& worldPos)
{
    const FogSample fog = SampleFogAtPoint(worldPos, false);
    if (fog.amount <= 0.0f) {
        return abgr;
    }

    const float a = static_cast<float>((abgr >> 24) & 0xFF) / 255.0f;
    const float b = static_cast<float>((abgr >> 16) & 0xFF) / 255.0f;
    const float g = static_cast<float>((abgr >> 8) & 0xFF) / 255.0f;
    const float r = static_cast<float>(abgr & 0xFF) / 255.0f;

    const float k = fog.amount;
    const float outR = r * (1.0f - k) + fog.color.x * k;
    const float outG = g * (1.0f - k) + fog.color.y * k;
    const float outB = b * (1.0f - k) + fog.color.z * k;

    const uint32_t A = static_cast<uint32_t>(a * 255.0f);
    const uint32_t R = static_cast<uint32_t>(outR * 255.0f);
    const uint32_t G = static_cast<uint32_t>(outG * 255.0f);
    const uint32_t B = static_cast<uint32_t>(outB * 255.0f);

    return (A << 24) | (B << 16) | (G << 8) | R;
}

void GLRenderer::RenderElements()
{
#ifdef GL_PERF_HOOKS
    GL_PERF_SCOPE("RenderElements");
#endif

    // 2.17: when GPUF_ELEMENTS_INSTANCING is on, accumulate all element
    // octagons into one vertex buffer and draw them in a single call
    // (DrawElementBatch) instead of one draw per element. The original
    // per-element RenderCircle() path is kept as the fallback and is used
    // when the feature is off (disable via config.cfg "gpufeatures 0").
    const bool elementsInstanced = GpuFeatureEnabled(GPUF_ELEMENTS_INSTANCING);
    std::vector<ModelVertex> elementBatch;
    elementBatch.reserve(1024);

    // ── Regular elements (muzzle flashes, impact sparks, etc.) ─────
    for (int eg = 0; eg < ElCount; eg++) {
        for (int e = 0; e < Elements[eg].ECount; e++) {
            TElement* el = &Elements[eg].EList[e];
            Vector3d rpos;
            rpos.x = el->pos.x - CameraX;
            rpos.y = el->pos.y - CameraY;
            rpos.z = el->pos.z - CameraZ;
            float r = el->R;

            // Sample fog at the world-relative position before we rotate
            // into view space (CalcFogLevel expects world coords).
            const uint32_t fogRGBA = ApplyFogToABGR(Elements[eg].RGBA, rpos);
            const uint32_t fogRGBA2 = ApplyFogToABGR(Elements[eg].RGBA2, rpos);

            rpos = RotateVector(rpos);
            if (rpos.z > -64) continue;
            if (fabs(rpos.x) > -rpos.z) continue;
            if (fabs(rpos.y) > -rpos.z) continue;

            float sx = VideoCX - static_cast<int>((CameraW * rpos.x / rpos.z * 16)) / 16.0f;
            float sy = VideoCY + static_cast<int>((CameraH * rpos.y / rpos.z * 16)) / 16.0f;
            if (elementsInstanced)
                BuildElementOctagon(elementBatch, sx, sy, rpos.z, -r * CameraW * 0.64f / rpos.z, fogRGBA, fogRGBA2);
            else
                RenderCircle(sx, sy, rpos.z, -r * CameraW * 0.64f / rpos.z,
                             fogRGBA, fogRGBA2);
        }
    }

    // ── Blood trails ──────────────────────────────────────────────
    for (int b = 0; b < BloodTrail.Count; b++) {
        Vector3d rpos = BloodTrail.Trail[b].pos;
        uint32_t A1 = (0xE0 * BloodTrail.Trail[b].LTime / 20000);
        if (A1 > 0xE0) A1 = 0xE0;
        uint32_t A2 = (0x20 * BloodTrail.Trail[b].LTime / 20000);
        if (A2 > 0x20) A2 = 0x20;

        rpos.x = rpos.x - CameraX;
        rpos.y = rpos.y - CameraY;
        rpos.z = rpos.z - CameraZ;

        // Blood colors are constructed in ARGB format (R<<16 | G<<8 | B).
        // conv_xGx processes them but returns unchanged in daytime.
        // RenderCircle expects ABGR, so convert: swap R and B channels.
        int dr = DinoInfo[BloodTrail.Trail[b].Owner].bloodRed;
        int dg = DinoInfo[BloodTrail.Trail[b].Owner].bloodGreen;
        int db = DinoInfo[BloodTrail.Trail[b].Owner].bloodBlue;
        uint32_t centerColor = (A1 << 24) | conv_xGx((db << 16) | (dg << 8) | dr);
        uint32_t edgeColor   = (A2 << 24) | conv_xGx((db/2 << 16) | (dg/2 << 8) | dr/2);

        // Apply fog at the world-relative position before rotation.
        const uint32_t fogCenter = ApplyFogToABGR(centerColor, rpos);
        const uint32_t fogEdge   = ApplyFogToABGR(edgeColor, rpos);

        rpos = RotateVector(rpos);
        if (rpos.z > -64) continue;
        if (fabs(rpos.x) > -rpos.z) continue;
        if (fabs(rpos.y) > -rpos.z) continue;

        float sx = VideoCX - static_cast<int>((CameraW * rpos.x / rpos.z * 16)) / 16.0f;
        float sy = VideoCY + static_cast<int>((CameraH * rpos.y / rpos.z * 16)) / 16.0f;

        if (elementsInstanced)
            BuildElementOctagon(elementBatch, sx, sy, rpos.z, -12.0f * CameraW * 0.64f / rpos.z, fogCenter, fogEdge);
        else
            RenderCircle(sx, sy, rpos.z, -12.0f * CameraW * 0.64f / rpos.z,
                         fogCenter, fogEdge);
    }

    // ── Snow particles ────────────────────────────────────────────
    for (int st = 0; st < SnowCh; st++) {
        for (int s = SnowInfo[st].addr; s < SnowInfo[st].addr + SnowInfo[st].SnCount; s++) {
            Vector3d rpos = Snow[s].pos;
            rpos.x = rpos.x - CameraX;
            rpos.y = rpos.y - CameraY;
            rpos.z = rpos.z - CameraZ;

            uint32_t A11 = SnowInfo[st].snow_a;
            if (Snow[s].ftime) {
                A11 = A11 * (2000 - Snow[s].ftime) / 2000;
            }

            // Snow colors use conv_xGx which expects ABGR (R in bits 0-7)
            uint32_t centerColor = (A11 << 24) |
                conv_xGx((SnowInfo[st].snow_b << 16) |
                (SnowInfo[st].snow_g << 8) |
                SnowInfo[st].snow_r);

            uint32_t edgeColor = ((A11 / 7) << 24) |
                conv_xGx((SnowInfo[st].snow_b / 2 << 16) |
                (SnowInfo[st].snow_g / 2 << 8) |
                SnowInfo[st].snow_r / 2);

            // Apply fog at the world-relative position before rotation.
            const uint32_t fogCenter = ApplyFogToABGR(centerColor, rpos);
            const uint32_t fogEdge   = ApplyFogToABGR(edgeColor, rpos);

            rpos = RotateVector(rpos);
            if (rpos.z > -64) continue;
            if (fabs(rpos.x) > -rpos.z) continue;
            if (fabs(rpos.y) > -rpos.z) continue;

            float sx = VideoCX - static_cast<int>((CameraW * rpos.x / rpos.z * 16)) / 16.0f;
            float sy = VideoCY + static_cast<int>((CameraH * rpos.y / rpos.z * 16)) / 16.0f;

            if (elementsInstanced)
                BuildElementOctagon(elementBatch, sx, sy, rpos.z,
                                   -8.0f * CameraW * 0.64f / rpos.z * SnowInfo[st].snow_rad,
                                   fogCenter, fogEdge);
            else
                RenderCircle(sx, sy, rpos.z,
                             -8.0f * CameraW * 0.64f / rpos.z * SnowInfo[st].snow_rad,
                             fogCenter, fogEdge);
        }
    }

    if (elementsInstanced && !elementBatch.empty()) {
        DrawElementBatch(elementBatch);
    }
}










unsigned int GLRenderer::Expand1555to8888(unsigned short c)
{
    if (c == 0) return 0x00000000;

    unsigned int r = (c >> 10) & 0x1F;
    unsigned int g = (c >> 5) & 0x1F;
    unsigned int b = c & 0x1F;

    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);

    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

std::array<float, 16> GLRenderer::BuildLegacyProjection()
{
    const float nearPlane = 16.0f;
    const float farPlaneCandidate = static_cast<float>(ctViewR * 256 + 4096);
    const float farPlane = nearPlane + 1.0f > farPlaneCandidate ? nearPlane + 1.0f : farPlaneCandidate;
    const float left = -nearPlane * static_cast<float>(VideoCX) / CameraW;
    const float right = nearPlane * static_cast<float>(WinW - VideoCX) / CameraW;
    const float top = nearPlane * static_cast<float>(VideoCY) / CameraH;
    const float bottom = -nearPlane * static_cast<float>(WinH - VideoCY) / CameraH;

    return {
        (2.0f * nearPlane) / (right - left), 0.0f, 0.0f, 0.0f,
        0.0f, (2.0f * nearPlane) / (top - bottom), 0.0f, 0.0f,
        (right + left) / (right - left), (top + bottom) / (top - bottom), -(farPlane + nearPlane) / (farPlane - nearPlane), -1.0f,
        0.0f, 0.0f, -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane), 0.0f
    };
}

Vector3d GLRenderer::DecodeFogColor(int rgb)
{
    return {
        static_cast<float>(rgb & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
        static_cast<float>((rgb >> 16) & 0xFF) / 255.0f
    };
}

Vector3d GLRenderer::GetCurrentFogColor()
{
    if (IsUnderwater() && FogsList[127].fogRGB) {
        return DecodeFogColorBGR(FogsList[127].fogRGB);
    }

    if (CAMERAINFOG && CameraFogI > 0) {
        return DecodeFogColor(FogsList[CameraFogI].fogRGB);
    }

    // When not in a fog volume, always return the sky color.
    // The previous code fell through to CurFogColor (a CalcFogLevel
    // side-effect) which could be a stale fog pocket color from a
    // terrain vertex lookup, causing a color mismatch with the sky.
    return {
        static_cast<float>(SkyR) / 255.0f,
        static_cast<float>(SkyG) / 255.0f,
        static_cast<float>(SkyB) / 255.0f
    };
}

Vector3d GLRenderer::GetFogColorForMapPoint(int mapX, int mapY)
{
    return GetFogColorForMapPoint(GetFogIndexForMapPoint(mapX, mapY));
}

// Phase 2.x: fogIndex overload — caller already computed GetFogIndexForMapPoint.
// Eliminates the duplicate FogsMap lookup when both fog amount and fog color
// are needed for the same (x,y) corner.
Vector3d GLRenderer::GetFogColorForMapPoint(int fogIndex)
{
    if (IsUnderwater()) {
        return GetDistanceFogColor();
    }

    // Clear terrain inherits the camera pocket's calculated fog amount, so
    // its colour must inherit the same volume rather than use the sky colour.
    fogIndex = ResolveTerrainFogIndex(fogIndex, CAMERAINFOG != 0, CameraFogI);
    if (FOGON && fogIndex > 0) {
        const float sunLight = g_GLRenderer ? g_GLRenderer->GetSunLight() : 0.0f;
        return DecodeFogColor(ApplySunFogColourShift(FogsList[fogIndex].fogRGB, sunLight));
    }

    return GetDistanceFogColor();
}

Vector3d GLRenderer::GetCachedTerrainFogColor(int fogIndex)
{
    // FogsMap is an unsigned byte, but keep the fallback defensive because
    // this helper is also used by the water path and should never index the
    // cache out of bounds if a malformed map value reaches it.
    if (fogIndex < 0 || fogIndex >= static_cast<int>(m_terrainFogColorCache.size())) {
        fogIndex = 0;
    }

    const size_t index = static_cast<size_t>(fogIndex);
    if (!m_terrainFogColorValid[index]) {
        // All inputs used by GetFogColorForMapPoint are stable between the
        // terrain and water passes in one rendered frame: underwater state,
        // FOGON, camera pocket state, sky colour, fog records, and sun visibility.
        // Index 0 now caches the camera-pocket colour while inside fog. Resolve
        // each index once instead of decoding it for every terrain corner.
        m_terrainFogColorCache[index] = GetFogColorForMapPoint(fogIndex);
        m_terrainFogColorValid[index] = 1;
    }
    return m_terrainFogColorCache[index];
}

void GLRenderer::UpdateCameraFogEnvelope()
{
    // §3.10: compute the *target* envelope for this frame, then smooth the
    // actual values toward it so entering/leaving a fog volume fades the
    // global envelope in/out instead of snapping.  flb is low-passed
    // (m_camEnvFlbSmooth) so head-bob — which oscillates CameraY and thus flb
    // — does not flip the check on/off every bob.
    float targetAmount = 0.0f;
    Vector3d targetColor = {0.0f, 0.0f, 0.0f};

    if (FOGON && !IsUnderwater() && CAMERAINFOG && CameraFogI > 0 && CameraFogI < 127) {
        const TFogEntity& fog = FogsList[CameraFogI];

        // CAMERAINFOG is set from the camera's map cell (Controls.cpp), so it
        // is true whenever the camera is horizontally inside a fog volume
        // regardless of eye height.  flb is the fog top relative to the eye:
        // >0 = top above the eye (tall fog), <0 = a shorter ground/body-level
        // layer the player still stands in.  Only a truly foot-level puddle
        // (flb far below the feet) is excluded (see kFootCut).
        // flb = how far the eye sits below the fog top (fog units).  Low-passed
        // into m_camEnvFlbSmooth so head-bob — which oscillates CameraY and
        // thus flb — cannot flip the submersion gate on/off every bob.
        const float flb = (fog.YBegin * ctHScale - CameraY) / ctHScale;
        constexpr float kFlbSmooth = 0.05f;   // low-pass on flb (kills head-bob)
        m_camEnvFlbSmooth += (flb - m_camEnvFlbSmooth) * kFlbSmooth;

        // §3.10 strength now tracks the volume's REAL density.  fog.FLimit is
        // the max opacity the engine assigns the volume (0..255), so a light /
        // low-FLimit pocket only ever produces a light global envelope instead
        // of the old fixed 0.5 floor that over-fogged thin volumes.  flb is
        // used purely as the submersion GATE: the envelope engages only while
        // the eye is actually below the fog top, so a foot-level puddle (or any
        // volume the eye sits above) stays clear.
        if (m_camEnvFlbSmooth > 0.0f) {
            // FLimit is the ceiling; Transp is the OTHER half of "how dense
            // the fog is" — the distance-ramp speed (smaller = builds up
            // faster = reads denser).  Fold Transp in as a gentle, bounded
            // strength nudge so a fast-building volume fogs harder globally and
            // a soft one fogs lighter, matching how the in-scene fog already
            // uses Transp.  Bounded by the clamp so a light volume still
            // stays light (no return of the over-fogging bug).  kRefTransp is a
            // typical Transp; tune it against the map's actual fog values.
            constexpr float kRefTransp = 160.0f;   // typical Transp (tune)
            const float transpFactor = std::clamp(
                kRefTransp / std::max(fog.Transp, 1.0f), 0.5f, 1.5f);
            // Clamped to 1.0: every consumer mix()es with this amount, and
            // anything above 1.0 extrapolates past the fog colour (inverted
            // skies) and clips to white (blowout) instead of fogging fully.
            targetAmount = std::min((fog.FLimit / 255.0f) * transpFactor, 1.0f);
            targetColor  = GetFogColorForMapPoint(CameraFogI);
        }
    }

    // Temporal smoothing (per-frame lerp).  §3.10: gentle fade in/out.
    // Tunable: higher = snappier, lower = slower.
    constexpr float kSmooth = 0.05f;
    m_camEnvelopeAmount += (targetAmount - m_camEnvelopeAmount) * kSmooth;
    m_camEnvelopeColor.x += (targetColor.x - m_camEnvelopeColor.x) * kSmooth;
    m_camEnvelopeColor.y += (targetColor.y - m_camEnvelopeColor.y) * kSmooth;
    m_camEnvelopeColor.z += (targetColor.z - m_camEnvelopeColor.z) * kSmooth;
}

float GLRenderer::Clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}








float GetTerrainFogAmountForMapPoint(int mapX, int mapY, int legacyFog)
{
    return GetTerrainFogAmountForMapPoint(GetFogIndexForMapPoint(mapX, mapY), legacyFog);
}

// Phase 2.x: fogIndex overload — caller already computed GetFogIndexForMapPoint.
// Eliminates the duplicate FogsMap lookup when both fog amount and fog color
// are needed for the same (x,y) corner.












void GLRenderer::DrawVertexBatch(const TerrainVertex* vertices, size_t count, const char* drawScopeName)
{
    if (count == 0 || !m_terrainVBO) {
        return;
    }

    const GLsizeiptr vertexSize = static_cast<GLsizeiptr>(count * sizeof(TerrainVertex));
    const bool persistentReady = m_usePersistentTerrainVBO && EnsureTerrainStreamCapacity(count) && m_terrainMappedPtr;
    glBindVertexArray(m_terrainVAO);
    if (persistentReady) {
        const size_t slice = m_terrainStreamNextSlice;
        m_terrainStreamNextSlice = (m_terrainStreamNextSlice + 1) % kTerrainStreamSlices;

        GLsync& fence = m_terrainStreamFences[slice];
        if (fence) {
            GLenum wait = glClientWaitSync(fence, 0, 0);
            if (wait == GL_TIMEOUT_EXPIRED) {
                wait = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
            }
            (void)wait;
            glDeleteSync(fence);
            fence = nullptr;
        }

        const size_t byteOffset = slice * m_terrainStreamSliceBytes;
        std::memcpy(static_cast<unsigned char*>(m_terrainMappedPtr) + byteOffset, vertices, static_cast<size_t>(vertexSize));
        {
            GL_PERF_SCOPE(drawScopeName);
            glDrawArrays(GL_TRIANGLES, static_cast<GLint>(slice * m_terrainStreamSliceVertices), static_cast<GLsizei>(count));
        }
        fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, m_terrainVBO);
        glBufferData(GL_ARRAY_BUFFER, vertexSize, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertexSize, vertices);
        {
            GL_PERF_SCOPE(drawScopeName);
            glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(count));
        }
    }
#ifdef GL_PERF_HOOKS
    GL_PERF_DRAW(static_cast<uint32_t>(count) / 3);
#endif
}

void GLRenderer::DrawFrame(const RenderFrameContext& ctx)
{
    // Phase 2.1: forward to the full rendering pipeline.
    // DrawScene() calls PreCashGroundModel (updates VMap/VMap2 from
    // current camera position), then renders sky, terrain, water,
    // models, vegetation, shadows, and UI elements.
    // Future: replace global reads with ctx.WinW, ctx.fogColor, etc.
    (void)ctx;
    ::DrawScene();
}

void GLRenderer::DrawScene()
{
    RenderGround();
    RenderWater();
}






void GLRenderer::ClearVideoBuf()
{
    // NOTE: do NOT reset m_uploadedTerrainTextures here — ClearVideoBuf is
    // called every frame from RenderSkyPlane(). Invalidating the cache
    // per-frame forces RenderTerrain() and RenderWaterSurface() to re-upload
    // every terrain texture layer on every draw (~10-20 glTexSubImage3D
    // calls per frame), which tanks CPU and GPU performance.
    const Vector3d fogColor = GetDistanceFogColor();
    glClearColor(fogColor.x, fogColor.y, fogColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void GLRenderer::WaitRetrace()
{
}

void GLRenderer::PostProcess()
{
    if (m_hasContext) {
#ifdef GL_PERF_HOOKS
        glperf_swap_begin();
#endif
        Platform::SwapGLBuffers();
#ifdef GL_PERF_HOOKS
        glperf_swap_end();
#endif
    }
}



















// ============================================================================
// HUD Pipeline
// ============================================================================








// ======================================================================
// Night desaturation + darkness overlay
// ======================================================================









// Copy picture pixels directly to lpVideoBuf (matching C1 and D3D/3DFX approach).
// lpVideoBuf is a 16-bit DIB section, pictures are already in 565 format after conv_pic.


// Phase 2.20: upload lpVideoBuf directly as GL_RGB5 texture.
// lpVideoBuf is 16-bit X1R5G5B5, top-down, stride = VideoPitch.
// The fragment shader converts 555→RGBA8 and handles transparency.





void GLRenderer::SetVideoMode(int w, int h)
{
    glViewport(0, 0, w, h);
}

void GLRenderer::SetFullScreen()
{
}

bool GLRenderer::IsSoftwareStyle() const
{
    return false;
}

#endif // _gl
