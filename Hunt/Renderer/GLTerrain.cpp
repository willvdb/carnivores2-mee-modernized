// ==========================================================================
// GLTerrain.cpp � Terrain rendering pipeline
// ==========================================================================

#include "Hunt.h"
#include "Platform/Platform.h"
#include "GLRenderer.h"
#include "Renderer/GLUtils.h"

#ifdef _gl

#include "glad/glad.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif
#ifndef GL_DYNAMIC_STORAGE_BIT
#define GL_DYNAMIC_STORAGE_BIT 0x0100
#endif

// Phase 2: shared alpha-cull threshold for the terrain collect paths.
namespace {
constexpr float kAlphaCullThreshold = 0.02f;
using PFNGLBUFFERSTORAGEPROC_LOCAL = void (APIENTRYP)(GLenum target, GLsizeiptr size, const void* data, GLbitfield flags);
PFNGLBUFFERSTORAGEPROC_LOCAL g_glBufferStorage = nullptr;

bool HasExtension(const char* name)
{
    if (!name || !glGetStringi) {
        return false;
    }

    GLint extensionCount = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    for (GLint i = 0; i < extensionCount; ++i) {
        const char* extension = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (extension && std::strcmp(extension, name) == 0) {
            return true;
        }
    }
    return false;
}

bool SupportsBufferStorage()
{
    const bool core44 = (GLVersion.major > 4) || (GLVersion.major == 4 && GLVersion.minor >= 4);
    return (core44 || HasExtension("GL_ARB_buffer_storage")) && Platform::GLProcAddress("glBufferStorage") != nullptr;
}
}

bool GLRenderer::InitializeTerrainPipeline()
{
    glGenVertexArrays(1, &m_terrainVAO);
    glGenBuffers(1, &m_terrainVBO);

    glBindVertexArray(m_terrainVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_terrainVBO);
    ConfigureTerrainVertexAttributes();

    m_usePersistentTerrainVBO = SupportsBufferStorage();
    if (m_usePersistentTerrainVBO) {
        g_glBufferStorage = reinterpret_cast<PFNGLBUFFERSTORAGEPROC_LOCAL>(Platform::GLProcAddress("glBufferStorage"));
        if (!g_glBufferStorage) {
            m_usePersistentTerrainVBO = false;
        }
    }

    if (!m_usePersistentTerrainVBO) {
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void GLRenderer::ConfigureTerrainVertexAttributes()
{
    // Phase 1.5: packed TerrainVertex layout (32 bytes).
    //   attribute 0: vec3  aPos                (12 bytes, float)
    //   attribute 1: vec2  aTexCoord            ( 8 bytes, float)
    //   attribute 2: float aLayer               ( 4 bytes, float)
    //   attribute 3: vec4  light/fog/alpha/pad  ( 4 bytes, uint8 normalized)
    //   attribute 4: vec3  fogR/fogG/fogB       ( 3 bytes, uint8 normalized)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT,         GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT,         GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT,         GL_FALSE, sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, layer)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, light)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(TerrainVertex), reinterpret_cast<void*>(offsetof(TerrainVertex, fogR)));
}

void GLRenderer::ShutdownTerrainPipeline()
{
    if (m_terrainTextureArray) {
        glDeleteTextures(1, &m_terrainTextureArray);
        m_terrainTextureArray = 0;
    }
    ShutdownTerrainPersistentMapping();
    if (m_terrainVBO) {
        glDeleteBuffers(1, &m_terrainVBO);
        m_terrainVBO = 0;
    }
    if (m_terrainVAO) {
        glDeleteVertexArrays(1, &m_terrainVAO);
        m_terrainVAO = 0;
    }


    m_terrainVertices.reset();
    m_terrainVertexCapacity = 0;
    m_terrainVertexCount = 0;
    m_waterVertices.reset();
    m_waterVertexCapacity = 0;
    m_waterVertexCount = 0;
    m_uploadedTerrainTextures.fill(nullptr);
}

void GLRenderer::EnsureTerrainVertexCapacity(size_t needed)
{
    if (needed <= m_terrainVertexCapacity) return;
    // Grow to the needed size (no shrinking — ctViewR rarely decreases).
    auto newBuf = std::make_unique<TerrainVertex[]>(needed);
    m_terrainVertices = std::move(newBuf);
    m_terrainVertexCapacity = needed;
}

void GLRenderer::EnsureWaterVertexCapacity(size_t needed)
{
    if (needed <= m_waterVertexCapacity) return;
    auto newBuf = std::make_unique<TerrainVertex[]>(needed);
    m_waterVertices = std::move(newBuf);
    m_waterVertexCapacity = needed;
}

void GLRenderer::ShutdownTerrainPersistentMapping()
{
    for (GLsync& fence : m_terrainStreamFences) {
        if (fence) {
            glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
            glDeleteSync(fence);
            fence = nullptr;
        }
    }

    if (m_terrainMappedPtr && m_terrainVBO) {
        glBindBuffer(GL_ARRAY_BUFFER, m_terrainVBO);
        glUnmapBuffer(GL_ARRAY_BUFFER);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    m_terrainMappedPtr = nullptr;
    m_terrainStreamSliceVertices = 0;
    m_terrainStreamSliceBytes = 0;
    m_terrainStreamNextSlice = 0;
}

bool GLRenderer::InitializeTerrainPersistentMapping(size_t sliceVertices)
{
    if (!m_usePersistentTerrainVBO || !g_glBufferStorage || sliceVertices == 0) {
        return false;
    }

    ShutdownTerrainPersistentMapping();
    if (m_terrainVBO) {
        glDeleteBuffers(1, &m_terrainVBO);
        m_terrainVBO = 0;
    }

    glGenBuffers(1, &m_terrainVBO);
    glBindVertexArray(m_terrainVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_terrainVBO);
    ConfigureTerrainVertexAttributes();

    m_terrainStreamSliceVertices = sliceVertices;
    m_terrainStreamSliceBytes = sliceVertices * sizeof(TerrainVertex);
    const GLsizeiptr totalBytes = static_cast<GLsizeiptr>(m_terrainStreamSliceBytes * kTerrainStreamSlices);
    const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

    g_glBufferStorage(GL_ARRAY_BUFFER, totalBytes, nullptr, flags);
    m_terrainMappedPtr = glMapBufferRange(GL_ARRAY_BUFFER, 0, totalBytes, flags);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    if (!m_terrainMappedPtr) {
        LOG_WARN("Persistent terrain VBO map failed; falling back to stream upload");
        m_usePersistentTerrainVBO = false;
        m_terrainStreamSliceVertices = 0;
        m_terrainStreamSliceBytes = 0;
        glDeleteBuffers(1, &m_terrainVBO);
        glGenBuffers(1, &m_terrainVBO);
        glBindVertexArray(m_terrainVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_terrainVBO);
        ConfigureTerrainVertexAttributes();
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return false;
    }

    return true;
}

bool GLRenderer::EnsureTerrainStreamCapacity(size_t neededVertices)
{
    if (!m_usePersistentTerrainVBO) {
        return false;
    }
    if (neededVertices <= m_terrainStreamSliceVertices && m_terrainMappedPtr) {
        return true;
    }

    // Grow only. Use a small floor so the first tiny frame does not cause churn.
    const size_t floorVertices = 4096;
    const size_t doubledVertices = m_terrainStreamSliceVertices * 2;
    size_t newSliceVertices = neededVertices > floorVertices ? neededVertices : floorVertices;
    newSliceVertices = newSliceVertices > doubledVertices ? newSliceVertices : doubledVertices;
    return InitializeTerrainPersistentMapping(newSliceVertices);
}

void GLRenderer::BeginTerrainFrame()
{
    m_terrainVertexCount = 0;
    m_terrainFogColorValid.fill(0);
    // §5.4: Ensure worst-case capacity for the current view distance.
    // Worst case: every cell in the visible disk produces 2 triangles = 6 vertices.
    // The 2x safety margin absorbs per-frame variance without reallocation.
    const size_t maxTiles = static_cast<size_t>(2 * ctViewR + 1) * static_cast<size_t>(2 * ctViewR + 1);
    EnsureTerrainVertexCapacity(maxTiles * 6);
    // m_waterVertices is cleared in BeginWaterFrame (called by RenderWater).
    // Clearing here too was redundant — RenderGround never touches water.
}

void GLRenderer::EnsureTerrainTextureArray()
{
    if (m_terrainTextureArray) {
        return;
    }

    glGenTextures(1, &m_terrainTextureArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_terrainTextureArray);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BASE_LEVEL, 0);
    // The original D3D/3DFX terrain/water renderers only ever sampled
    // DataA (128x128) and DataB (64x64).  DataC/DataD were generated
    // for the software renderer's very-far fallback, but using them in
    // the GL path made distant LOD tiles look oddly blurry / differently
    // textured.  Cap the array at mip level 1 to match the hardware
    // renderers.
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 1);

    // Step 7: Enable anisotropic filtering for smoother texture LOD
    // transitions at oblique angles (one-time setup, zero per-frame cost)
    // GL_EXT_texture_filter_anisotropic constants (not in GLAD headers)
    constexpr GLint GL_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FE;
    constexpr GLint GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT = 0x84FF;
    GLint maxAniso = 0;
    glGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso >= 4) {
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4);
    }

    int size = 128;
    for (int level = 0; level < kTerrainMipLevels; ++level) {
        glTexImage3D(GL_TEXTURE_2D_ARRAY, level, GL_RGBA8, size, size, kMaxTerrainTextureLayers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        const int nextSize = size / 2;
        size = nextSize < 1 ? 1 : nextSize;
    }
}

void GLRenderer::UploadTerrainLayer(int layer, const TEXTURE& texture)
{
    if (layer < 0 || layer >= kMaxTerrainTextureLayers) {
        return;
    }

    EnsureTerrainTextureArray();
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_terrainTextureArray);

    const WORD* mipSources[kTerrainMipLevels] = {
        texture.DataA,
        texture.DataB,
        texture.DataC,
        texture.DataD
    };

    int mipSize = 128;
    for (int level = 0; level < kTerrainMipLevels; ++level) {
        std::vector<unsigned int> expanded(mipSize * mipSize);
        for (int i = 0; i < mipSize * mipSize; ++i) {
            expanded[i] = Expand1555to8888(mipSources[level][i]);
        }

        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, level, 0, 0, layer, mipSize, mipSize, 1, GL_RGBA, GL_UNSIGNED_BYTE, expanded.data());
        const int nextMipSize = mipSize / 2;
        mipSize = nextMipSize < 1 ? 1 : nextMipSize;
    }
}

void GLRenderer::AppendTerrainTriangle(const EPoint& v0,
                                       const EPoint& v1,
                                       const EPoint& v2,
                                       const Vector3d& fogColor0,
                                       const Vector3d& fogColor1,
                                       const Vector3d& fogColor2,
                                       int textureLayer,
                                       bool reverse,
                                       bool second,
                                       int direction,
                                       float alpha0,
                                       float alpha1,
                                       float alpha2)
{
    const auto& uv = TerrainUV::Get(reverse, second, direction);
    const float layer = static_cast<float>(textureLayer);

    TerrainVertex* dst = m_terrainVertices.get() + m_terrainVertexCount;
    dst[0] = {v0.v.x, v0.v.y, v0.v.z, uv[0].x, uv[0].y, layer,
              Light255ToByte(static_cast<float>(v0.Light)),
              Light255ToByte(v0.Fog), Float01ToByte(alpha0), 0,
              Float01ToByte(fogColor0.x), Float01ToByte(fogColor0.y),
              Float01ToByte(fogColor0.z), 0};
    dst[1] = {v1.v.x, v1.v.y, v1.v.z, uv[1].x, uv[1].y, layer,
              Light255ToByte(static_cast<float>(v1.Light)),
              Light255ToByte(v1.Fog), Float01ToByte(alpha1), 0,
              Float01ToByte(fogColor1.x), Float01ToByte(fogColor1.y),
              Float01ToByte(fogColor1.z), 0};
    dst[2] = {v2.v.x, v2.v.y, v2.v.z, uv[2].x, uv[2].y, layer,
              Light255ToByte(static_cast<float>(v2.Light)),
              Light255ToByte(v2.Fog), Float01ToByte(alpha2), 0,
              Float01ToByte(fogColor2.x), Float01ToByte(fogColor2.y),
              Float01ToByte(fogColor2.z), 0};
    m_terrainVertexCount += 3;
}


void GLRenderer::RenderTerrain()
{
    if (m_terrainVertexCount == 0) {
        return;
    }

    EnsureTerrainTextureArray();

    for (int layer = 0; layer < kMaxTerrainTextureLayers; ++layer) {
        if (!Textures[layer]) {
            continue;
        }
        if (m_uploadedTerrainTextures[layer] != Textures[layer].get()) {
            UploadTerrainLayer(layer, *Textures[layer]);
            m_uploadedTerrainTextures[layer] = Textures[layer].get();
        }
    }

    UpdatePerFrameUBO();
    SetWaterAlphaFade(0.0f, 0.0f, 0.0f, 765.0f);
    m_terrainShader.Use();

    // Night lighting belongs on world geometry, not in a fullscreen pass;
    // this leaves the separately-rendered sky and moon untouched.
    {
        static const GLint uNight = glGetUniformLocation(m_terrainShader.GetProgramID(), "uNightStrength");
        if (uNight >= 0) {
            glUniform1f(uNight, (OptDayNight == 2 && !NightVisionOn) ? 1.0f : 0.0f);
        }
    }

    // §3.5: per-pixel sun-fog forward-scatter.  Replaces the old per-tile
    // CPU glow (one value per tile, interpolated across its 4 vertices, which
    // looked blocky).  We only pass the sun direction in view space plus the
    // sun visibility here; terrain.frag evaluates dot(viewDir, sunDir)^8 per
    // fragment and scales by vFog, so the warm glow appears only inside fog
    // and only when looking toward the sun.
    {
        static const GLint uSunDir  = glGetUniformLocation(m_terrainShader.GetProgramID(), "uSunDirection");
        static const GLint uSunVis  = glGetUniformLocation(m_terrainShader.GetProgramID(), "uSunVisibility");
        static const GLint uScatter = glGetUniformLocation(m_terrainShader.GetProgramID(), "uFogScatter");
        if (uScatter >= 0) {
            if (m_isUnderwater || GetSunLight() < 0.1f) {
                glUniform1f(uScatter, 0.0f);
            } else {
                Vector3d sunDir = {-2048.0f, 4048.0f, -2048.0f};
                sunDir = RotateVector(sunDir);
                const float len = std::sqrt(sunDir.x * sunDir.x + sunDir.y * sunDir.y + sunDir.z * sunDir.z);
                if (len > 1e-3f && uSunDir >= 0 && uSunVis >= 0) {
                    glUniform3f(uSunDir, sunDir.x / len, sunDir.y / len, sunDir.z / len);
                    const float vis = std::clamp(GetSunLight() / kMaxSunLight, 0.0f, 1.0f);
                    glUniform1f(uSunVis, vis);
                    glUniform1f(uScatter, 0.25f);  // master scatter strength (was 0.25 in §3.5)
                } else {
                    glUniform1f(uScatter, 0.0f);
                }
            }
        }
    }

    // §3.10: camera-in-fog global envelope.  When the camera is submerged in
    // a tall pocket-fog volume, fog the whole scene by distance (not just
    // in-volume geometry).  Colour/amount are computed once per frame in
    // GLRenderer::UpdateCameraFogEnvelope() and consumed here + in the model
    // shaders.  (The sky horizon is handled separately by §3.8.)
    {
        static const GLint uCamFogCol = glGetUniformLocation(m_terrainShader.GetProgramID(), "uCamFogColor");
        static const GLint uCamFogAmt = glGetUniformLocation(m_terrainShader.GetProgramID(), "uCamFogAmount");
        if (uCamFogCol >= 0 && uCamFogAmt >= 0) {
            glUniform3f(uCamFogCol, m_camEnvelopeColor.x, m_camEnvelopeColor.y, m_camEnvelopeColor.z);
            glUniform1f(uCamFogAmt, m_camEnvelopeAmount);
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_terrainTextureArray);
#ifdef GL_PERF_HOOKS
    GL_PERF_TEXTURE_BIND(m_terrainTextureArray);
#endif
    glBindVertexArray(m_terrainVAO);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    DrawVertexBatch(m_terrainVertices.get(), m_terrainVertexCount, "Terrain_Draw");

    glBindVertexArray(0);
}

void GLRenderer::ResetTerrainTextureCache()
{
    m_uploadedTerrainTextures.fill(nullptr);
    // Phase 3: invalidate the water bitmask on level transition.
    // It will be rebuilt on the first frame that needs it, AFTER the
    // new level's FMap has been loaded (the old code rebuilt here, but
    // that ran before LoadResources() populated the new FMap).
    m_waterBlockBitsValid = false;
}

void GLRenderer::ClearLevelTextureCache()
{
    // Clear per-level model texture and mesh caches between levels.
    // Per-level TModels are heap-allocated (unique addresses across
    // levels), so address recycling is not a concern, but the cache
    // entries and GPU resources from the previous level are dead
    // weight. Clearing them here prevents unbounded VRAM growth.
    // Global models (ChInfo, SunModel, etc.) survive this clear;
    // their textures and meshes are re-uploaded on first use next
    // level (a one-time cost per level transition).

    for (const auto& item : m_modelTextureCache) {
        if (item.second) {
            glDeleteTextures(1, &item.second);
        }
    }
    m_modelTextureCache.clear();

    for (const auto& item : m_bmpTextureCache) {
        if (item.second) {
            glDeleteTextures(1, &item.second);
        }
    }
    m_bmpTextureCache.clear();

    // Static mesh cache entries hold VBO/IBO offsets that are no longer
    // valid for the new level's models. Clear the map so UploadStaticMesh
    // re-uploads fresh geometry. The VBO/IBO data is orphaned but will be
    // reused when EnsureStaticMeshCapacity regrows the buffers.
    m_staticMeshCache.clear();

    m_skyTextureDirty = true;
}

// ---------------------------------------------------------------------------
// Phase 3: Water bitmask — coarse "has water" bitmap for 8x8 cell blocks.
// Built once at level load; skips CollectWaterTileFast for dry blocks.
// ---------------------------------------------------------------------------

bool GLRenderer::BlockHasWater(int x, int y) const
{
    if (!m_waterBlockBitsValid) return true;  // safe fallback: assume water
    const int bx = x >> kWaterBlockShift;
    const int by = y >> kWaterBlockShift;
    if (bx < 0 || by < 0 || bx >= kWaterBlockDim || by >= kWaterBlockDim) return false;
    const size_t idx = static_cast<size_t>(by) * kWaterBlockDim + static_cast<size_t>(bx);
    return (m_waterBlockBits[idx >> 6] >> (idx & 63)) & 1ULL;
}

void GLRenderer::RebuildWaterBlockBits()
{
    std::fill(m_waterBlockBits.begin(), m_waterBlockBits.end(), 0ULL);
    for (int by = 0; by < kWaterBlockDim; ++by) {
        for (int bx = 0; bx < kWaterBlockDim; ++bx) {
            bool any = false;
            for (int dy = 0; dy < 8 && !any; ++dy) {
                for (int dx = 0; dx < 8 && !any; ++dx) {
                    const int cx = (bx << kWaterBlockShift) + dx;
                    const int cy = (by << kWaterBlockShift) + dy;
                    if (cx < ctMapSize && cy < ctMapSize && (FMap[cy][cx] & fmWaterA)) {
                        any = true;
                    }
                }
            }
            if (any) {
                const size_t idx = static_cast<size_t>(by) * kWaterBlockDim + static_cast<size_t>(bx);
                m_waterBlockBits[idx >> 6] |= (1ULL << (idx & 63));
            }
        }
    }
    m_waterBlockBitsValid = true;
}

// ---------------------------------------------------------------------------
// Phase 5: Overloaded CollectTerrainTile with hoisted fade constants.
// These are called from the frustum-bounded sweep in RenderGround.
// The original 3-arg version delegates to these for backward compat.
// ---------------------------------------------------------------------------

// §3.1: Smooth fog-volume transitions.  Blend the four per-corner fog
// colours 30% toward their tile average to soften hard fog boundaries.
// This is the "simpler alternative" from pocket-fog-improvements-c2.md —
// no additional FogsMap lookups, just averaging already-fetched colours.
static void SmoothFogColors(Vector3d& fog00, Vector3d& fog10,
                            Vector3d& fog01, Vector3d& fog11)
{
    constexpr float kBlend = 0.3f;
    constexpr float kInvBlend = 1.0f - kBlend;
    const float avgX = (fog00.x + fog10.x + fog01.x + fog11.x) * 0.25f;
    const float avgY = (fog00.y + fog10.y + fog01.y + fog11.y) * 0.25f;
    const float avgZ = (fog00.z + fog10.z + fog01.z + fog11.z) * 0.25f;
    fog00.x = fog00.x * kInvBlend + avgX * kBlend;
    fog00.y = fog00.y * kInvBlend + avgY * kBlend;
    fog00.z = fog00.z * kInvBlend + avgZ * kBlend;
    fog10.x = fog10.x * kInvBlend + avgX * kBlend;
    fog10.y = fog10.y * kInvBlend + avgY * kBlend;
    fog10.z = fog10.z * kInvBlend + avgZ * kBlend;
    fog01.x = fog01.x * kInvBlend + avgX * kBlend;
    fog01.y = fog01.y * kInvBlend + avgY * kBlend;
    fog01.z = fog01.z * kInvBlend + avgZ * kBlend;
    fog11.x = fog11.x * kInvBlend + avgX * kBlend;
    fog11.y = fog11.y * kInvBlend + avgY * kBlend;
    fog11.z = fog11.z * kInvBlend + avgZ * kBlend;
}

// §3.5+§3.6: Sun-fog forward-scatter glow and colour shift.
// §3.5: Sun-fog forward-scatter glow.
void GLRenderer::CollectTerrainTile(int x, int y, int r,
                                    float fadeStart, float fadeStartSq, float fadeEnd)
{
    (void)r;

    if (x >= ctMapSize - 1 || y >= ctMapSize - 1 || x < 0 || y < 0) {
        return;
    }

    float backR = BackViewR;
    if (OMap[y][x] != 255) {
        backR += MObjects[OMap[y][x]].info.BoundR;
    }

    const int localX = x - CCX + kViewGridCenter;
    const int localY = y - CCY + kViewGridCenter;
    if (localX < 0 || localY < 0 || localX + 1 >= kViewGridSize || localY + 1 >= kViewGridSize) {
        return;
    }
#ifdef GL_PERF_HOOKS
    ++m_terrainPerf.tileCandidates;
#endif

    // Coarse frustum pre-test — SAFEGUARD: only reject when cz < 0.
    // The lateral MUST be scaled by FOVK so this cone matches the precise
    // 4-corner check below (and the GPU projection).  Without FOVK the
    // bound assumes a ~90-degree frustum: at wide FOV (FOVK < 1) it is far
    // narrower than the true frustum and visibly clips the left/right of
    // the view.  The backR*2+2048 margin keeps it a safe superset of the
    // per-corner test at every FOV (it only absorbs tile-center-vs-corner
    // and HMapO-vs-VMap height slop, never real rejections).
    {
        const float wx = static_cast<float>(x * 256 + 128) - CameraX;
        const float wz = static_cast<float>(y * 256 + 128) - CameraZ;
        const float wy = static_cast<float>(HMapO[y][x]) * ctHScale - CameraY;
        const float cx  = wx * ca + wz * sa;
        const float cz1 = wz * ca - wx * sa;
        const float cz  = cz1 * cb + wy * sb;
        if (cz < 0.0f && std::fabs(cx * FOVK) > -cz + backR * 2.0f + 2048.0f) {
#ifdef GL_PERF_HOOKS
            ++m_terrainPerf.coarseCulled;
#endif
            return;
        }
    }

    // Fetch vertices
    EPoint v00 = VMap[localY][localX];
    EPoint v10 = VMap[localY][localX + 1];
    EPoint v01 = VMap[localY + 1][localX];
    EPoint v11 = VMap[localY + 1][localX + 1];

    // Compute per-corner fog + alpha (shared with the 2x2 chunk path),
    // then run the shared cull + emit. Fog cells cover 2x2 map points, so
    // adjacent corners often share the same FogsMap entry; cache by fog cell.
    const int fogCellX[2] = { ((x) & (ctMapSize - 1)) >> 1, ((x + 1) & (ctMapSize - 1)) >> 1 };
    const int fogCellY[2] = { ((y) & (ctMapSize - 1)) >> 1, ((y + 1) & (ctMapSize - 1)) >> 1 };
    int fogIdxGrid[2][2];
    fogIdxGrid[0][0] = FogsMap[fogCellY[0]][fogCellX[0]];
    fogIdxGrid[0][1] = (fogCellX[1] == fogCellX[0]) ? fogIdxGrid[0][0] : FogsMap[fogCellY[0]][fogCellX[1]];
    fogIdxGrid[1][0] = (fogCellY[1] == fogCellY[0]) ? fogIdxGrid[0][0] : FogsMap[fogCellY[1]][fogCellX[0]];
    fogIdxGrid[1][1] = (fogCellY[1] == fogCellY[0]) ? fogIdxGrid[0][1]
                      : (fogCellX[1] == fogCellX[0]) ? fogIdxGrid[1][0]
                      : FogsMap[fogCellY[1]][fogCellX[1]];
    const int fogIdx00 = fogIdxGrid[0][0];
    const int fogIdx10 = fogIdxGrid[0][1];
    const int fogIdx01 = fogIdxGrid[1][0];
    const int fogIdx11 = fogIdxGrid[1][1];

    v00.Fog = static_cast<int>(GetTerrainFogAmountForMapPoint(fogIdx00, v00.Fog, m_isUnderwater));
    v10.Fog = static_cast<int>(GetTerrainFogAmountForMapPoint(fogIdx10, v10.Fog, m_isUnderwater));
    v01.Fog = static_cast<int>(GetTerrainFogAmountForMapPoint(fogIdx01, v01.Fog, m_isUnderwater));
    v11.Fog = static_cast<int>(GetTerrainFogAmountForMapPoint(fogIdx11, v11.Fog, m_isUnderwater));

    Vector3d fog00 = GetCachedTerrainFogColor(fogIdx00);
    Vector3d fog10 = GetCachedTerrainFogColor(fogIdx10);
    Vector3d fog01 = GetCachedTerrainFogColor(fogIdx01);
    Vector3d fog11 = GetCachedTerrainFogColor(fogIdx11);

    // §3.1: Smooth fog-volume transitions — blend corners toward tile average
    SmoothFogColors(fog00, fog10, fog01, fog11);

    // Phase 5: use cached m_isUnderwater to skip the global load
    float alpha00 = CalcTerrainAlpha(VertexDistanceSq(v00.v), fadeStart, fadeStartSq, fadeEnd, m_isUnderwater);
    float alpha10 = CalcTerrainAlpha(VertexDistanceSq(v10.v), fadeStart, fadeStartSq, fadeEnd, m_isUnderwater);
    float alpha01 = CalcTerrainAlpha(VertexDistanceSq(v01.v), fadeStart, fadeStartSq, fadeEnd, m_isUnderwater);
    float alpha11 = CalcTerrainAlpha(VertexDistanceSq(v11.v), fadeStart, fadeStartSq, fadeEnd, m_isUnderwater);

    // Step 5: Fog-aware distance fade
    // When fog is dense, distance fade has less effect — fog already obscures the view
    if (!m_isUnderwater) {
        const float fog00f = static_cast<float>(v00.Fog) / 200.0f;
        const float fog10f = static_cast<float>(v10.Fog) / 200.0f;
        const float fog01f = static_cast<float>(v01.Fog) / 200.0f;
        const float fog11f = static_cast<float>(v11.Fog) / 200.0f;

        const float fogAdjust00 = 1.0f - fog00f * 0.7f;
        const float fogAdjust10 = 1.0f - fog10f * 0.7f;
        const float fogAdjust01 = 1.0f - fog01f * 0.7f;
        const float fogAdjust11 = 1.0f - fog11f * 0.7f;

        alpha00 = std::clamp(alpha00 * fogAdjust00 + fog00f * 0.7f, 0.0f, 1.0f);
        alpha10 = std::clamp(alpha10 * fogAdjust10 + fog10f * 0.7f, 0.0f, 1.0f);
        alpha01 = std::clamp(alpha01 * fogAdjust01 + fog01f * 0.7f, 0.0f, 1.0f);
        alpha11 = std::clamp(alpha11 * fogAdjust11 + fog11f * 0.7f, 0.0f, 1.0f);
    }

    EmitTerrainTile(x, y, backR, v00, v10, v01, v11,
                    fog00, fog10, fog01, fog11,
                    alpha00, alpha10, alpha01, alpha11);
}

// Phase 2: shared per-tile cull + emit.  See GLRenderer.h for the contract.
// This is the single source of truth for the back-plane / 4-corner frustum
// / distance / alpha-cull / texture-emit / RenderObject decisions, so the
// 1x1 (CollectTerrainTile) and 2x2 (CollectTerrainChunk2x2) paths produce
// byte-identical geometry and object queues for the same tile.
void GLRenderer::EmitTerrainTile(int x, int y, float backR,
                                 const EPoint& v00, const EPoint& v10,
                                 const EPoint& v01, const EPoint& v11,
                                 const Vector3d& fog00, const Vector3d& fog10,
                                 const Vector3d& fog01, const Vector3d& fog11,
                                 float alpha00, float alpha10,
                                 float alpha01, float alpha11)
{
    // Only reject if ALL corners are behind the back plane.
    if (v00.v.z > backR && v10.v.z > backR && v01.v.z > backR && v11.v.z > backR) {
#ifdef GL_PERF_HOOKS
        ++m_terrainPerf.backCulled;
#endif
        return;
    }

    // Precise frustum cull — conservative 4-corner check.
    // When looking up a slope, the tile center can be at shallower depth
    // than the elevated corners, causing the single-center test to cull
    // tiles that are still partially visible.  Only reject if ALL 4
    // corners are outside the same frustum side.
    {
        bool v00OutR = ( v00.v.x * FOVK > -v00.v.z + backR);
        bool v10OutR = ( v10.v.x * FOVK > -v10.v.z + backR);
        bool v01OutR = ( v01.v.x * FOVK > -v01.v.z + backR);
        bool v11OutR = ( v11.v.x * FOVK > -v11.v.z + backR);
        bool allOutR = v00OutR && v10OutR && v01OutR && v11OutR;

        bool v00OutL = (-v00.v.x * FOVK > -v00.v.z + backR);
        bool v10OutL = (-v10.v.x * FOVK > -v10.v.z + backR);
        bool v01OutL = (-v01.v.x * FOVK > -v01.v.z + backR);
        bool v11OutL = (-v11.v.x * FOVK > -v11.v.z + backR);
        bool allOutL = v00OutL && v10OutL && v01OutL && v11OutL;

        if (allOutR || allOutL) {
#ifdef GL_PERF_HOOKS
            ++m_terrainPerf.frustumCulled;
#endif
            return;
        }
    }

    // Distance cull
    const float xx = (v00.v.x + v11.v.x) * 0.5f;
    const float yy = (v00.v.y + v11.v.y) * 0.5f;
    const float zz = (v00.v.z + v11.v.z) * 0.5f;
    const float viewDistance = static_cast<float>(ctViewR * 256);
    const float viewDistanceSq = viewDistance * viewDistance;
    const float distanceSq = xx * xx + yy * yy + zz * zz;
    if (distanceSq > viewDistanceSq) {
#ifdef GL_PERF_HOOKS
        ++m_terrainPerf.distanceCulled;
#endif
        return;
    }

    // Alpha cull — skip tiles whose 4 vertex alphas are all below threshold
    if (alpha00 < kAlphaCullThreshold && alpha10 < kAlphaCullThreshold &&
        alpha01 < kAlphaCullThreshold && alpha11 < kAlphaCullThreshold) {
#ifdef GL_PERF_HOOKS
        ++m_terrainPerf.alphaCulled;
#endif
        RenderObject(x, y);
        return;
    }
#ifdef GL_PERF_HOOKS
    ++m_terrainPerf.emittedTiles;
#endif

    const bool reverse = (FMap[y][x] & fmReverse) != 0;
    const int direction = FMap[y][x] & 3;

    const int textureLayer = TMap1[y][x];
    if (textureLayer >= 0 && textureLayer < kMaxTerrainTextureLayers && Textures[textureLayer]) {
        if (reverse) {
            AppendTerrainTriangle(v00, v10, v01, fog00, fog10, fog01, textureLayer, reverse, false, direction, alpha00, alpha10, alpha01);
            AppendTerrainTriangle(v01, v10, v11, fog01, fog10, fog11, textureLayer, reverse, true, direction, alpha01, alpha10, alpha11);
        } else {
            AppendTerrainTriangle(v00, v10, v11, fog00, fog10, fog11, textureLayer, reverse, false, direction, alpha00, alpha10, alpha11);
            AppendTerrainTriangle(v00, v11, v01, fog00, fog11, fog01, textureLayer, reverse, true, direction, alpha00, alpha11, alpha01);
        }
    }

    RenderObject(x, y);
}

// Phase 2: 2x2 chunked collection.  See GLRenderer.h for the contract.
void GLRenderer::CollectTerrainChunk2x2(int x, int y,
                                         float fadeStart, float fadeStartSq, float fadeEnd)
{
    // The block covers tiles (x,y),(x+1,y),(x,y+1),(x+1,y+1).  Each tile
    // needs 0 <= c <= ctMapSize-2 (CollectTerrainTile's [A] bound) and a
    // 2x2 VMap footprint inside the view grid ([C] bound).  The 3x3 shared
    // corner grid needs localX+2 <= kViewGridSize-1, i.e. localX <=
    // kViewGridSize-3 — the same as the rightmost tile's [C] bound.  So the
    // block is valid iff all four tiles are valid.  Fall back otherwise.
    const bool mapOk = (x >= 0 && y >= 0 &&
                       x <= ctMapSize - 3 && y <= ctMapSize - 3);
    const int localX = x - CCX + kViewGridCenter;
    const int localY = y - CCY + kViewGridCenter;
    const bool gridOk = (localX >= 0 && localY >= 0 &&
                        localX <= kViewGridSize - 3 && localY <= kViewGridSize - 3);
    if (!mapOk || !gridOk) {
        CollectTerrainTile(x,     y,     0, fadeStart, fadeStartSq, fadeEnd);
        CollectTerrainTile(x + 1, y,     0, fadeStart, fadeStartSq, fadeEnd);
        CollectTerrainTile(x,     y + 1, 0, fadeStart, fadeStartSq, fadeEnd);
        CollectTerrainTile(x + 1, y + 1, 0, fadeStart, fadeStartSq, fadeEnd);
        return;
    }
#ifdef GL_PERF_HOOKS
    ++m_terrainPerf.chunkCandidates;
    m_terrainPerf.tileCandidates += 4;
#endif

    // Per-tile back radius + coarse (HMapO) frustum pre-test. These use
    // only OMap/HMapO, allowing a fully rejected block to avoid VMap/fog
    // preparation. The row sweep normally makes this a conservative guard.
    float backR[2][2];
    bool  coarsePass[2][2];
    bool  anyCoarse = false;
    for (int dj = 0; dj < 2; ++dj) {
        for (int di = 0; di < 2; ++di) {
            const int cx = x + di;
            const int cy = y + dj;
            float br = BackViewR;
            if (OMap[cy][cx] != 255) br += MObjects[OMap[cy][cx]].info.BoundR;
            backR[dj][di] = br;
            const float wx  = static_cast<float>(cx * 256 + 128) - CameraX;
            const float wz  = static_cast<float>(cy * 256 + 128) - CameraZ;
            const float wy  = static_cast<float>(HMapO[cy][cx]) * ctHScale - CameraY;
            const float cxc = wx * ca + wz * sa;
            const float cz1 = wz * ca - wx * sa;
            const float cz  = cz1 * cb + wy * sb;
            const bool pass = !(cz < 0.0f && std::fabs(cxc * FOVK) > -cz + br * 2.0f + 2048.0f);
            coarsePass[dj][di] = pass;
            if (pass) {
                anyCoarse = true;
            }
#ifdef GL_PERF_HOOKS
            else {
                ++m_terrainPerf.coarseCulled;
            }
#endif
        }
    }
    if (!anyCoarse) {
        return;
    }

    // Read the shared 3x3 grid of VMap corners (9 reads for 4 tiles vs 16).
    EPoint v[3][3];
    int      fogIdx[3][3];
    Vector3d fogCol[3][3];
    float    alpha[3][3];
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
            v[j][i] = VMap[localY + j][localX + i];

    // Per-corner fog index / fog amount / fog colour / alpha — each corner
    // is shared by up to 4 tiles, so compute once.
    const int fogCellX[3] = { ((x) & (ctMapSize - 1)) >> 1,
                              ((x + 1) & (ctMapSize - 1)) >> 1,
                              ((x + 2) & (ctMapSize - 1)) >> 1 };
    const int fogCellY[3] = { ((y) & (ctMapSize - 1)) >> 1,
                              ((y + 1) & (ctMapSize - 1)) >> 1,
                              ((y + 2) & (ctMapSize - 1)) >> 1 };
    for (int j = 0; j < 3; ++j) {
        const bool sameFogRow = (j > 0 && fogCellY[j] == fogCellY[j - 1]);
        for (int i = 0; i < 3; ++i) {
            if (sameFogRow) {
                fogIdx[j][i] = fogIdx[j - 1][i];
            } else if (i > 0 && fogCellX[i] == fogCellX[i - 1]) {
                fogIdx[j][i] = fogIdx[j][i - 1];
            } else {
                fogIdx[j][i] = FogsMap[fogCellY[j]][fogCellX[i]];
            }
            v[j][i].Fog = static_cast<int>(GetTerrainFogAmountForMapPoint(fogIdx[j][i], v[j][i].Fog, m_isUnderwater));
            fogCol[j][i] = GetCachedTerrainFogColor(fogIdx[j][i]);
            alpha[j][i] = CalcTerrainAlpha(VertexDistanceSq(v[j][i].v), fadeStart, fadeStartSq, fadeEnd, m_isUnderwater);

            // Step 5: Fog-aware distance fade
            if (!m_isUnderwater) {
                const float fogf = static_cast<float>(v[j][i].Fog) / 200.0f;
                const float fogAdjust = 1.0f - fogf * 0.7f;
                alpha[j][i] = std::clamp(alpha[j][i] * fogAdjust + fogf * 0.7f, 0.0f, 1.0f);
            }
        }
    }

    // Emit each coarse survivor through the shared precise cull path.
    for (int dj = 0; dj < 2; ++dj) {
        for (int di = 0; di < 2; ++di) {
            if (!coarsePass[dj][di]) continue;
            // §3.1: Smooth fog-volume transitions per tile
            SmoothFogColors(fogCol[dj][di], fogCol[dj][di + 1],
                            fogCol[dj + 1][di], fogCol[dj + 1][di + 1]);
            EmitTerrainTile(x + di, y + dj, backR[dj][di],
                            v[dj][di], v[dj][di + 1], v[dj + 1][di], v[dj + 1][di + 1],
                            fogCol[dj][di], fogCol[dj][di + 1], fogCol[dj + 1][di], fogCol[dj + 1][di + 1],
                            alpha[dj][di], alpha[dj][di + 1], alpha[dj + 1][di], alpha[dj + 1][di + 1]);
        }
    }
}

void GLRenderer::RenderGround()
{
#ifdef GL_PERF_HOOKS
    GL_PERF_SCOPE("RenderGround");
#endif
    {
    GL_PERF_CPU_SCOPE("Terrain_FrameSetup");
    BeginTerrainFrame();
#ifdef GL_PERF_HOOKS
    m_terrainPerf = {};
#endif
    m_worldModelItems.clear();
    m_transparentModelItems.clear();
    m_objectList.clear();
    }

    // Cache IsUnderwater() once per frame (Phase 5).
    m_isUnderwater = IsUnderwater();

    // Phase 3: lazily rebuild the water bitmask if it was invalidated
    // by a level transition (the bitmap must be built AFTER the new
    // level's FMap has been populated by LoadResources).
    if (NeedWater && !m_waterBlockBitsValid) {
        RebuildWaterBlockBits();
    }

    // If water is needed this frame, begin water collection here so the
    // water constants are ready for the ring walk.
    if (NeedWater) {
        BeginWaterFrame();
    }

    // Precompute water distance/fade constants once for the ring walk.
    float wViewDistSq = 0.0f, wFadeStart = 0.0f, wFadeStartSq = 0.0f;
    float wFadeEnd = 0.0f, wFadeEndSq = 0.0f;
    if (NeedWater) {
        const float vd = static_cast<float>(ctViewR * 256);
        wViewDistSq = vd * vd;
        wFadeStart = static_cast<float>((ctViewR - 8) << 8);
        wFadeStartSq = wFadeStart * wFadeStart;
        wFadeEnd = 256.0f * static_cast<float>(ctViewR - 4);
        wFadeEndSq = wFadeEnd * wFadeEnd;
    }

    // Hoist terrain fade constants once per frame (Phase 5) so that
    // CollectTerrainTile doesn't recompute them
    // per call.
    const float tFadeStart = static_cast<float>((ctViewR - 8) << 8);
    const float tFadeStartSq = tFadeStart * tFadeStart;
    const float tFadeEnd = 256.0f * static_cast<float>(ctViewR - 4);

    // Phase 1: Frustum-bounded row sweep (replaces the full-disk ring walk).
    //
    // Camera space (per RotateVector in Vector.cpp; this engine's sign
    // convention is FORWARD = cz < 0, BEHIND = cz > 0, confirmed by the
    // coarse test `cz<0 && |cx|>-cz+...` and the projection `v.x/v.z`):
    //   cx  = wx*ca + wz*sa           (lateral)
    //   cz1 = wz*ca - wx*sa           (forward, pre-pitch)
    //   cz  = cz1*cb + wy*sb          (forward, post-pitch)
    // For a cell at offset (dx,dy) from (CCX,CCY), ignoring the sub-cell
    // camera residual (< 1 cell, absorbed by kMargin below):
    //   cx  = 256*(dx*ca + dy*sa)
    //   cz1 = 256*(dy*ca - dx*sa)
    // The precise per-corner frustum test in CollectTerrainTile keeps a
    // corner when |cx|*FOVK <= -cz + backR.  Substituting cz = cz1*cb + wy*sb
    // and using the most permissive terrain height wyEff (a safe superset
    // over the global height range, so the bound never misses a visible
    // tile regardless of pitch) yields two half-planes in (dx,dy):
    //   right:  Ar*dx + Br*dy <= Cr
    //   left:   Al*dx + Bl*dy >= Cl
    //   Ar = ca*FOVK - sa*cb,  Br = sa*FOVK + ca*cb
    //   Al = ca*FOVK + sa*cb,  Bl = sa*FOVK - ca*cb
    //   Cr = (backR - wyEff*sb)/256,  Cl = -Cr
    // For each row dy the sign of Ar/Al decides whether the edge gives a
    // lower or upper dx bound; a zero coefficient means the edge is
    // parallel to the row and the whole row is either in that half-plane
    // or outside it (skip).  The dx range is intersected with the
    // view-distance disk dx^2+dy^2 <= ctViewR^2 (a superset of the per-tile
    // 3D distance cull, since 3D distance >= 2D distance), expanded by
    // kMargin for the tile-corner span + camera residual + rounding, then
    // clamped to the map.  The unchanged per-tile coarse/precise/distance
    // culls inside CollectTerrainTile still run and remove any slack, so
    // the emitted geometry is identical to the full-disk walk.
    {
#ifdef GL_PERF_HOOKS
        GLPerfScope scope_walk("RenderGround_Walk", false);
#endif
        // Per-frame frustum coefficients.
        const float fovk = FOVK;
        const float ca_  = ca, sa_ = sa, cb_ = cb, sb_ = sb;
        // Safe terrain-height bounds relative to the camera (global map
        // range 0..255; conservative superset so pitch never misses a tile).
        const float wyMin = 0.0f - CameraY;
        const float wyMax = 255.0f * static_cast<float>(ctHScale) - CameraY;
        // wyEff maximizes (-wy*sb): lowest terrain when looking down,
        // highest terrain when looking up, so the bound is a superset of
        // the true pitched frustum for every possible corner height.
        const float wyEff = (sb_ > 0.0f) ? wyMin : (sb_ < 0.0f ? wyMax : 0.0f);
        const float P  = BackViewR - wyEff * sb_;   // effective near offset (world units)
        const float Cr = P / 256.0f;                // in cells
        const float Cl = -Cr;
        const float Ar = ca_ * fovk - sa_ * cb_;
        const float Br = sa_ * fovk + ca_ * cb_;
        const float Al = ca_ * fovk + sa_ * cb_;
        const float Bl = sa_ * fovk - ca_ * cb_;

        const float ctViewRf = static_cast<float>(ctViewR);
        const float ctViewR2 = ctViewRf * ctViewRf;
        // Margin: tile corners span +/-0.5 cell from the center, the
        // sub-cell camera residual is < 1 cell, and floor/ceil rounding
        // can eat ~1 cell.  3 cells comfortably covers all of it.
        constexpr float kMargin = 3.0f;

        const int yLo = (std::max)(0, CCY - ctViewR);
        const int yHi = (std::min)(ctMapSize - 1, CCY + ctViewR);

        // Compute one row's frustum dx-range [lo,hi] (cells relative to
        // CCX): view disk  ∩  right half-plane  ∩  left half-plane, expanded
        // by kMargin and clamped to the view disk.  Returns false when the row
        // has no visible span (so the caller can skip / union it away).
        auto rowRange = [&](float dy, float& lo, float& hi) -> bool {
            const float d2 = ctViewR2 - dy * dy;
            if (d2 <= 0.0f) return false;
            const float D = std::sqrt(d2);
            lo = -D; hi = D;

            // Right half-plane: Ar*dx + Br*dy <= Cr
            const float rhsR = Cr - Br * dy;
            if      (Ar >  1e-12f) hi = (std::min)(hi, rhsR / Ar);
            else if (Ar < -1e-12f) lo = (std::max)(lo, rhsR / Ar);
            else if (Br * dy > Cr)  return false;   // edge parallel to row

            // Left half-plane: Al*dx + Bl*dy >= Cl
            const float rhsL = Cl - Bl * dy;   // Al*dx >= rhsL
            if      (Al >  1e-12f) lo = (std::max)(lo, rhsL / Al);
            else if (Al < -1e-12f) hi = (std::min)(hi, rhsL / Al);
            else if (Bl * dy < Cl)  return false;

            lo -= kMargin;
            hi += kMargin;
            if (lo < -ctViewRf) lo = -ctViewRf;
            if (hi >  ctViewRf) hi =  ctViewRf;
            return lo <= hi;
        };

        // Phase 2: process rows two at a time so the 2x2 chunk can share
        // VMap corners vertically as well as horizontally (9 reads for 4
        // tiles vs 16).  The block spans the UNION of the two rows' dx-ranges
        // (each row's range is a superset of its visible tiles, and the union
        // is a superset of both — so no row's visible tile is missed; the
        // per-tile culls in CollectTerrainChunk2x2 remove the slack).
        int y = yLo;
        for (; y + 1 <= yHi; y += 2) {
            const float dy0 = static_cast<float>(y - CCY);
            const float dy1 = static_cast<float>(y + 1 - CCY);
            float lo0, hi0, lo1, hi1;
            const bool ok0 = rowRange(dy0, lo0, hi0);
            const bool ok1 = rowRange(dy1, lo1, hi1);
            if (!ok0 && !ok1) continue;
            float lo = ok0 ? lo0 : lo1;
            float hi = ok0 ? hi0 : hi1;
            if (ok0 && ok1) {
                lo = (std::min)(lo, lo1);
                hi = (std::max)(hi, hi1);
            }
            int xLeft  = (std::max)(0,             CCX + static_cast<int>(std::floor(lo)));
            int xRight = (std::min)(ctMapSize - 1, CCX + static_cast<int>(std::ceil(hi)));
            if (xLeft > xRight) continue;

            for (int x = xLeft; x <= xRight; x += 2) {
                CollectTerrainChunk2x2(x, y, tFadeStart, tFadeStartSq, tFadeEnd);
                if (NeedWater) {
                    for (int dj = 0; dj < 2; ++dj) {
                        for (int di = 0; di < 2; ++di) {
                            const int wx = x + di;
                            const int wy = y + dj;
                            if (BlockHasWater(wx, wy)) {
                                CollectWaterTileFast(wx, wy, 0, wViewDistSq, wFadeStart, wFadeStartSq, wFadeEnd, wFadeEndSq);
                            }
                        }
                    }
                }
            }
        }
        // Leftover odd row — 1x1 (a 2x2 chunk would need a row beyond yHi).
        if (y <= yHi) {
            float lo, hi;
            if (rowRange(static_cast<float>(y - CCY), lo, hi)) {
                int xLeft  = (std::max)(0,             CCX + static_cast<int>(std::floor(lo)));
                int xRight = (std::min)(ctMapSize - 1, CCX + static_cast<int>(std::ceil(hi)));
                for (int x = xLeft; x <= xRight; ++x) {
                    CollectTerrainTile(x, y, 0, tFadeStart, tFadeStartSq, tFadeEnd);
                    if (NeedWater && BlockHasWater(x, y)) {
                        CollectWaterTileFast(x, y, 0, wViewDistSq, wFadeStart, wFadeStartSq, wFadeEnd, wFadeEndSq);
                    }
                }
            }
        }
    }

    {
        GL_PERF_SCOPE("Terrain_Batch");
        RenderTerrain();
    }
#ifdef GL_PERF_HOOKS
    GL_PERF_TERRAIN_WORKLOAD(m_terrainPerf.chunkCandidates,
                             m_terrainPerf.tileCandidates,
                             m_terrainPerf.coarseCulled,
                             m_terrainPerf.backCulled,
                             m_terrainPerf.frustumCulled,
                             m_terrainPerf.distanceCulled,
                             m_terrainPerf.alphaCulled,
                             m_terrainPerf.emittedTiles,
                             m_terrainVertexCount);
#endif
}
#endif // _gl
