// ==========================================================================
// GLRenderer.h — OpenGL 3.3 Core Profile renderer for Carnivores 2 ME
// ==========================================================================

#ifndef GLRENDERER_H
#define GLRENDERER_H

#pragma once

#include "Renderer/IRenderer.h"
#include "glad/glad.h"
#include "Renderer/GLShader.h"
#include "Renderer/GLPerf.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class GLRenderer : public IRenderer {
public:
    GLRenderer();
    ~GLRenderer() override;

    bool Initialize() override;
    void Shutdown() override;

    bool CreateContext();
    void DestroyContext();

    void DrawFrame(const RenderFrameContext& ctx) override;
    void DrawScene() override;
    void DrawPostObjects() override;

    void RegisterTexture(TEXTURE* tptr) override;
    void RegisterPicture(TPicture* pptr) override;
    void ReleaseModelTextures(const TModel* mptr) override;
    void ResetTerrainTextureCache() override;
    void ClearLevelTextureCache() override;

    void ClearVideoBuf() override;
    void WaitRetrace() override;
    void PostProcess() override;

    void DrawTPlane(bool clip) override;
    void DrawTPlaneClip(bool clip) override;
    void DrawHMap() override;

    void RenderModel(TModel* mptr, float x0, float y0, float z0,
                     int light, int vt, float al, float bt) override;
    void RenderModelClip(TModel* mptr, float x0, float y0, float z0,
                         int light, int vt, float al, float bt) override;
    void RenderModelClipWater(TModel* mptr, float x0, float y0, float z0,
                              int light, int vt, float al, float bt) override;
    void RenderModelClipPhongMap(TModel* mptr, float x0, float y0, float z0,
                                 float al, float bt);
    void RenderModelClipEnvMap(TModel* mptr, float x0, float y0, float z0,
                               float al, float bt);
    void RenderNearModel(TModel* mptr, float x0, float y0, float z0,
                         int light, int vt, float al, float bt) override;

    void RenderCharacter(TCharacter* cptr) override;
    void RenderExplosion(int index) override;
    void RenderShip() override;
    void RenderPlayer(int index) override;
    void RenderSkyPlane() override;
    void RenderWCircles() override;

    void DrawPicture(int x, int y, TPicture& pic) override;
    void DrawScaledPicture(int x, int y, int w, int h, TPicture& pic) override;
    void DrawTrophyText(int x, int y) override;
    void RenderHealthBar() override;
    void Render_Cross(int x, int y) override;
    void Render_LifeInfo(int index) override;

    void SetVideoMode(int w, int h) override;
    void SetFullScreen() override;
    bool IsSoftwareStyle() const override;

    void RenderGround();
    void RenderProjectedShadows();
    void RenderWater();
    void RenderObject(int x, int y);
    void RenderMappedObject(int x, int y);
    void RenderModelsList();
    void RenderBMPModel(TBMPModel* mptr, float x0, float y0, float z0, int light);
    void Render3DHardwarePosts();
    void RenderCircle(float cx, float cy, float z, float R, uint32_t RGBA, uint32_t RGBA2);
    void RenderElements();

private:
    // Packed terrain vertex (Phase 1.5). 32 bytes total (was 48).
    //   offset  0: vec3  aPos                    (12 bytes)  -- attribute 0, float
    //   offset 12: vec2  aTexCoord                ( 8 bytes)  -- attribute 1, float
    //   offset 20: float aLayer                   ( 4 bytes)  -- attribute 2, float
    //   offset 24: vec4  aLightFogAlpha           ( 4 bytes)  -- attribute 3, uint8 normalized
    //                 .x = light, .y = fog, .z = alpha, .w = water fade enable for water verts
    //   offset 28: vec3  aFogColor                ( 3 bytes)  -- attribute 4, uint8 normalized
    //   offset 31: 1 byte explicit padding to round the vertex up to 32 bytes
    struct TerrainVertex {
        float    x, y, z;          // 12
        float    u, v;             //  8
        float    layer;            //  4
        uint8_t  light;            //  1 (vec4.x)
        uint8_t  fog;              //  1 (vec4.y)
        uint8_t  alpha;            //  1 (vec4.z)
        uint8_t  _pad1;            //  1 (vec4.w)
        uint8_t  fogR;             //  1 (vec3.x)
        uint8_t  fogG;             //  1 (vec3.y)
        uint8_t  fogB;             //  1 (vec3.z)
        uint8_t  _pad2;            //  1  (pad to 32)
    };
    static_assert(sizeof(TerrainVertex) == 32, "TerrainVertex must stay 32 bytes (Phase 1.5)");

    // Packed model vertex (Phase 1.4). 32 bytes total (was 48).
    //   offset  0: vec3  aPos                    (12 bytes)  -- attribute 0, float
    //   offset 12: vec2  aTexCoord                ( 8 bytes)  -- attribute 1, float
    //   offset 20: vec4  aLightFogAlphaCutout     ( 4 bytes)  -- attribute 2, uint8 normalized
    //                 .x = light, .y = fog, .z = alpha, .w = cutout
    //   offset 24: vec3  aFogColor                ( 3 bytes)  -- attribute 3, uint8 normalized
    //   offset 27: 5 bytes explicit padding to round the vertex up to 32 bytes
    struct ModelVertex {
        float    x, y, z;          // 12
        float    u, v;             //  8
        uint8_t  light;            //  1 (vec4.x)
        uint8_t  fog;              //  1 (vec4.y)
        uint8_t  alpha;            //  1 (vec4.z)
        uint8_t  cutout;           //  1 (vec4.w)
        uint8_t  fogR;             //  1 (vec3.x)
        uint8_t  fogG;             //  1 (vec3.y)
        uint8_t  fogB;             //  1 (vec3.z)
        uint8_t  _pad[5];          //  5  (pad to 32)
    };
    static_assert(sizeof(ModelVertex) == 32, "ModelVertex must stay 32 bytes (Phase 1.4)");

    // 2.17: draws a batched set of element octagons in one call (see GLRenderer.cpp).
    void DrawElementBatch(const std::vector<ModelVertex>& batch);
    // 2.17: appends one element octagon to `out` using the same geometry/colour/depth
    // math as RenderCircle(); shared by DrawElementBatch and the per-element fallback.
    static void BuildElementOctagon(std::vector<ModelVertex>& out, float cx, float cy, float z,
                                    float R, uint32_t RGBA, uint32_t RGBA2);

    // Phase 1.4 conversion helpers: float [0,1] / float [0,255] -> uint8.
    // Drivers normalize the uint8 attribute back to [0,1] in the vertex
    // shader, so the shader sees the same values as the old float layout.
    static inline uint8_t Float01ToByte(float v) {
        return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    static inline uint8_t Light255ToByte(float v) {
        return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f);
    }
    static inline uint8_t CutoutToByte(bool on) {
        return on ? 255 : 0;  // normalized to 1.0 or 0.0; shader alpha-tests when vCutout > 0.5
    }

    struct ModelDrawItem {
        GLuint texture;
        float distance;
        bool additive;  // true => draw with GL_BLEND_FUNC(SRC_ALPHA, ONE), no depth write
        std::vector<ModelVertex> opaqueVertices;
        std::vector<ModelVertex> cutoutVertices;
        std::vector<ModelVertex> transparentVertices;
    };

    // Phase 2.1+2.3: per-instance data layout for geometry instancing.
    // GLSL mat4(col0,col1,col2,col3) takes COLUMN vectors, so the four
    // vec4 attributes must be the columns of the view-from-model matrix.
    //   attribute 4: vec4 aWorldCol0    (offset  0)  -- matrix column 0
    //   attribute 5: vec4 aWorldCol1    (offset 16)  -- matrix column 1
    //   attribute 6: vec4 aWorldCol2    (offset 32)  -- matrix column 2
    //   attribute 7: vec4 aWorldCol3    (offset 48)  -- matrix column 3
    //   attribute 8: vec4 aInstanceLight (offset 64) -- .x = base light; .yzw = pocket fog colour
    //   attribute 9: vec4 aInstanceFlags (offset 80) -- .x = orientation; .y = fogGrad; .z = fogBase; .w = alpha
    //   attributes 10-13: four vec4 ground-light rows (offset 96) -- 4x4 VMap samples
    //   attribute 14: vec4 aGroundParams (offset 160) -- .x = enabled; .yz = centre from sample-grid origin
    // Total: 176 bytes per instance, 16-byte aligned.
    //
    // Vertex shader usage:
    //   mat4 iWorld = mat4(aWorldCol0, aWorldCol1, aWorldCol2, aWorldCol3);
    //   gl_Position = uProjection * iWorld * vec4(aPos, 1.0);
    struct ModelInstance {
        float worldCol0[4];     // 16
        float worldCol1[4];     // 16
        float worldCol2[4];     // 16
        float worldCol3[4];     // 16
        float instanceLight[4]; // 16 -- .x = base light; .yzw = pocket fog colour
        float instanceFlags[4]; // 16 -- .x = orientation; .yzw = fog gradient/base + alpha
        float groundLight[16];  // 64 -- normalized 4x4 VMap samples, row-major
        float groundParams[4];  // 16 -- .x = enabled; .yz = object centre from sample-grid origin
    };
    static_assert(sizeof(ModelInstance) == 176, "ModelInstance must stay 176 bytes");

    // Phase 2.2: static mesh vertex (data that's truly static per-model —
    // positions in model space, pre-baked UVs, a face-baked normal, and the
    // four legacy model-orientation light variants generated by CalcLights).
    //   attribute 0: vec3 aPos             (12 bytes) -- model space
    //   attribute 1: vec3 aNormal          (12 bytes) -- face normal, baked at upload
    //   attribute 2: vec2 aTexCoord        ( 8 bytes) -- pre-baked UV
    //   attribute 3: vec4 aVertexLight     (16 bytes) -- signed 0..3 VLight offsets in legacy 0..255 units
    //   attribute 15: float aCutout         ( 4 bytes) -- per-face sfOpacity classification
    // Total: 52 bytes per vertex. The normal is the face
    // normal (flat-shaded); per-vertex smooth shading requires sharing
    // vertices between faces, which is a later optimization.
    struct StaticMeshVertex {
        float x, y, z;      // 12
        float nx, ny, nz;   // 12
        float u, v;         //  8
        float light[4];     // 16
        float cutout;       //  4 -- 1.0 for sfOpacity faces, otherwise 0.0
    };
    static_assert(sizeof(StaticMeshVertex) == 52, "StaticMeshVertex must stay 52 bytes");

    // Phase 2.2: per-model location in the global static VBO/IBO.
    // The static VBO and IBO hold ALL uploaded models concatenated.
    // The model's vertices occupy VBO[baseVertex .. baseVertex+vertexCount).
    // The model's indices occupy IBO[baseIndex .. baseIndex+indexCount)
    // and the index values reference the VBO from the global origin
    // (i.e. index k references VBO[k], so model B's first index is
    //  (A's vertex count), not 0).
    struct StaticMeshEntry {
        uint32_t baseVertex = 0;  // VBO offset of first vertex (in vertices)
        uint32_t baseIndex = 0;   // IBO offset of first index (in indices)
        uint32_t vertexCount = 0; // number of vertices in the VBO
        uint32_t indexCount = 0;  // number of indices in the IBO
        bool hasTransparent = false; // Phase 2.3: model has sfTransparent blended faces
        float minX = 0.0f, maxX = 0.0f; // model-space footprint used by bounded ground-light instancing
        float minZ = 0.0f, maxZ = 0.0f;
        unsigned int lastVertexUploadTime = 0; // animated scenery updates its shared mesh once per frame
    };

    bool InitGLState();
    void LoadGLExtensions();
    bool InitializeTerrainPipeline();
    void ShutdownTerrainPipeline();
    bool InitializeModelPipeline();
    void ShutdownModelPipeline();
    bool InitializeInstancingPipeline();
    void ShutdownInstancingPipeline();
    bool InitializeStaticMeshPipeline();
    void ShutdownStaticMeshPipeline();
    StaticMeshEntry UploadStaticMesh(TModel* mptr);
    const StaticMeshEntry* GetStaticMeshEntry(const TModel* mptr) const;
    void BuildStaticMeshVertices(std::vector<StaticMeshVertex>& vertices, const TModel* mptr) const;
    void UpdateAnimatedStaticMesh(TModel* mptr);
    // Two RGBA8 texels per expanded vertex: light/fog/r/g, b/visible/0/0.
    struct ExactShade { uint8_t light, fog, r, g, b, visible, pad0, pad1; };
    static_assert(sizeof(ExactShade) == 8, "Exact shading must occupy two RGBA8 texels");
    std::vector<ExactShade> m_exactShades;
    // Single-threaded placement scratch; contents never escape preparation.
    std::vector<Vector3d> m_exactPositionScratch;
    std::vector<ExactShade> m_exactVertexScratch;
#ifdef GL_PERF_HOOKS
    std::map<const TModel*, unsigned> m_exactValidated;
#endif
    GLuint m_exactShadeBuffer = 0, m_exactShadeTexture = 0;
    GLint m_exactShadeLimit = 0;
    bool PopulateExactShadeInstance(ModelInstance& instance, const StaticMeshEntry& mesh,
                                    TModel* model, const Vector3d& pos, float fi);
    bool PopulateGroundLightInstance(ModelInstance& instance,
                                     const StaticMeshEntry& mesh,
                                     int worldCenterX,
                                     int worldCenterZ,
                                     int orientation) const;
    void EnsureStaticMeshCapacity(size_t vertexBytes, size_t indexBytes);
    void UpdatePerFrameUBO();
    void UpdatePerFrameUBO(const std::array<float, 16>& projection,
                           float waterAlphaEnabled = 0.0f,
                           float waterAlphaFadeStart = 0.0f,
                           float waterAlphaFadeEnd = 0.0f,
                           float waterAlphaFadeStep = 765.0f);
    void EnsurePerFrameUBO();
    void SetWaterAlphaFade(float enabled, float fadeStart, float fadeEnd, float fadeStep);
    void BeginTerrainFrame();
    void BeginWaterFrame();
    void RenderTerrain();
    void RenderWaterSurface();
    void RenderWorldModels();
    // Step 2: Apply model distance fade using CalcTerrainAlpha (smoothstep).
    // Sets m_modelDistanceAlpha and clears GlassL for legacy compatibility.
    void ApplyModelDistanceFade(const Vector3d& rpos);
    // 2.19: builds a character's projected shadow triangles into `outVerts`
    // (no draw); shared by RenderProjectedCharacterShadow (fallback) and the
    // batched path in RenderProjectedShadows.
    static void BuildCharacterShadowVertices(const TCharacter& character, float alpha,
                                            std::vector<ModelVertex>& outVerts);
    void RenderProjectedCharacterShadow(const TCharacter& character, float alpha);
    void DrawVertexBatch(const TerrainVertex* vertices, size_t count, const char* drawScopeName);
    GLuint UploadModelTexture(TModel* mptr);
    GLuint UploadBMPModelTexture(TBMPModel* mptr);
    GLuint UploadPictureTexture(const TPicture& pic);
    bool BuildModelEffectVertices(std::vector<ModelVertex>& outVertices,
                                  TModel* mptr,
                                  float x0,
                                  float y0,
                                  float z0,
                                  float al,
                                  float bt,
                                  int flagMask,
                                  const Vector3d& fogColor) const;
    bool BuildModelDrawItem(ModelDrawItem& outItem,
                            TModel* mptr,
                            float x0,
                            float y0,
                            float z0,
                            int light,
                            int vt,
                            float al,
                            float bt,
                            bool waterClipped,
                            bool disableFog,
                            bool clippedVariant,
                            bool additive) const;
    void DrawModelVertices(GLuint texture,
                           const std::vector<ModelVertex>& vertices,
                           const std::array<float, 16>& projection,
                           bool depthTest,
                           bool enableBlend,
                           bool additive,
                           bool tintByFogColor = false);
    void EnsureTerrainTextureArray();
    void UploadTerrainLayer(int layer, const TEXTURE& texture);
    void CollectTerrainTile(int x, int y, int r,
                            float fadeStart, float fadeStartSq, float fadeEnd);
    // Phase 2: 2x2 chunked collection.  Reads a shared 3x3 grid of VMap
    // corners (9 reads for 4 tiles vs 16) and shared per-corner fog/alpha,
    // then emits each child tile via EmitTerrainTile.  Falls back to four
    // CollectTerrainTile calls when the block crosses a map/view-grid edge.
    void CollectTerrainChunk2x2(int x, int y,
                                float fadeStart, float fadeStartSq, float fadeEnd);
    // Phase 2: shared per-tile cull + emit (back-plane / 4-corner frustum /
    // distance / alpha-cull / texture emit / RenderObject).  The 4 EPoint
    // corners must already have .Fog finalised; fog colours and alphas must
    // already be computed by the caller.  Used by both the 1x1 and 2x2
    // paths so their cull decisions stay byte-identical.
    void EmitTerrainTile(int x, int y, float backR,
                         const EPoint& v00, const EPoint& v10,
                         const EPoint& v01, const EPoint& v11,
                         const Vector3d& fog00, const Vector3d& fog10,
                         const Vector3d& fog01, const Vector3d& fog11,
                         float alpha00, float alpha10,
                         float alpha01, float alpha11);
    // Fast water tile collection: precomputed constants + squared-distance
    // alpha ramp + single FogsMap lookup per tile (~1ms saved at max dist).
    void CollectWaterTileFast(int x, int y, int r,
                              float viewDistanceSq,
                              float fadeStart, float fadeStartSq,
                              float fadeEnd, float fadeEndSq);
    void AppendTerrainTriangle(const EPoint& v0,
                               const EPoint& v1,
                               const EPoint& v2,
                               const Vector3d& fogColor0,
                               const Vector3d& fogColor1,
                               const Vector3d& fogColor2,
                               int textureLayer,
                               bool reverse,
                               bool second,
                               int direction,
                               float alpha0 = 1.0f,
                               float alpha1 = 1.0f,
                               float alpha2 = 1.0f);
    void AppendWaterTriangle(const EPoint& v0,
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
                             float alpha2,
                             float fadeEnabled = 0.0f);
    static std::array<float, 16> BuildLegacyProjection();
    static Vector3d GetCurrentFogColor();
    static Vector3d GetFogColorForMapPoint(int mapX, int mapY);
    static Vector3d GetFogColorForMapPoint(int fogIndex);  // Phase 2.x: fogIndex overload avoids duplicate FogsMap lookup
    // Terrain and water collection repeatedly request the same small set of
    // fog-volume colours during one frame. Cache the resolved colour by fog
    // index after the first lookup; the cache is reset in BeginTerrainFrame.
    Vector3d GetCachedTerrainFogColor(int fogIndex);
    static Vector3d DecodeFogColor(int rgb);
    static bool IsWaterTriangleValid(const EPoint& v0, const EPoint& v1, const EPoint& v2, float backR);
    static float CalcWaterAlpha(const EPoint& vertex, float centerDistanceSq, float fadeStart, float fadeStartSq, float fadeEnd);
    static float Clamp01(float value);
    static unsigned int Expand1555to8888(unsigned short c);

    bool m_hasContext = false;
    bool m_Initialized = false;

    GLShader m_terrainShader;
    unsigned int m_terrainVAO = 0;
    unsigned int m_terrainVBO = 0;
    unsigned int m_terrainTextureArray = 0;

    // P-G1.4: optional persistent-mapped stream buffer for terrain/water.
    // Falls back to orphan + subdata when GL 4.4/ARB_buffer_storage is absent.
    bool m_usePersistentTerrainVBO = false;
    void* m_terrainMappedPtr = nullptr;
    size_t m_terrainStreamSliceVertices = 0;
    size_t m_terrainStreamSliceBytes = 0;
    static constexpr size_t kTerrainStreamSlices = 6; // terrain+water over a 3-frame ring
    std::array<GLsync, kTerrainStreamSlices> m_terrainStreamFences{};
    size_t m_terrainStreamNextSlice = 0;
    bool InitializeTerrainPersistentMapping(size_t sliceVertices);
    void ShutdownTerrainPersistentMapping();
    bool EnsureTerrainStreamCapacity(size_t neededVertices);
    void ConfigureTerrainVertexAttributes();

    GLShader m_modelShader;
    unsigned int m_modelVAO = 0;
    unsigned int m_modelVBO = 0;

    // Phase 2.3: instanced model shader for geometry instancing.
    // The instanced shader takes per-vertex position/UV from the static
    // mesh VBO and per-instance world matrix + light + flags from the
    // instance VBO. Synchronized animated scenery updates each shared mesh
    // once per frame, then all of its placements remain instanced. One
    // glDrawElementsInstanced per (mesh, texture, pass)
    // replaces hundreds of per-object draw calls.
    // The VAO used for instanced draws is m_instanceVAO (created in
    // InitializeInstancingPipeline, which pairs the static VBO attributes
    // with the per-instance VBO).
    GLShader m_instancedModelShader;

    // Phase 2.3: per-instance tracking for instanced draws.
    // Each entry corresponds to one ModelInstance in m_instanceData.
    struct InstanceInfo {
        const TModel* model;   // key for static mesh lookup
        GLuint texture;        // GL texture handle
    };
    std::vector<InstanceInfo> m_instanceInfo;

    // Scratch records used to reorder opaque/cutout instances without ever
    // separating the per-instance data from its (model, texture) key.
    struct InstanceSortRecord {
        ModelInstance data;
        InstanceInfo info;
    };
    std::vector<InstanceSortRecord> m_instanceSortScratch;

    // Phase 2.3: instanced model rendering function.
    void RenderInstancedModels();

    // Phase 2.1: per-instance data plumbing for geometry instancing.
    // Phase 2.3: now actively used. The instance VBO holds per-instance
    // world matrix + light + flags. The instance VAO binds the static
    // mesh VBO (per-vertex) and instance VBO (per-instance, divisor=1).
    unsigned int m_instanceVBO = 0;
    unsigned int m_instanceVAO = 0;

    // Per-frame instance array. Filled in 2.3; declared here so the
    // vector's backing storage lives for the whole program (avoid
    // per-frame heap churn once 2.3 starts using it).
    std::vector<ModelInstance> m_instanceData;

    // Phase 2.2: static mesh infrastructure. The static VBO and IBO
    // hold every unique TModel*'s data concatenated. Uploaded lazily
    // by the per-call hook in the RenderModel* family; cached in
    // m_staticMeshCache by TModel* pointer. The m_staticMeshNext*Offset
    // cursors track the next free slot in each buffer; they reset to
    // 0 when EnsureStaticMeshCapacity grows the buffer (which also
    // clears the cache, forcing all models to re-upload).
    unsigned int m_staticMeshVBO = 0;
    unsigned int m_staticMeshIBO = 0;
    size_t m_staticMeshVBOCapacity = 0;
    size_t m_staticMeshIBOCapacity = 0;
    uint32_t m_staticMeshNextVertexOffset = 0;
    uint32_t m_staticMeshNextIndexOffset = 0;
    std::map<const TModel*, StaticMeshEntry> m_staticMeshCache;
    std::vector<StaticMeshVertex> m_animatedMeshScratch; // reused by once-per-model animation uploads
    // Phase 1.11: last-bound model texture, used to skip redundant
    // glBindTexture calls when consecutive buckets share a texture
    // (which is the common case after Phase 1.7's bucket sort).
    GLuint m_lastBoundModelTexture = 0;
    unsigned int m_whiteTexture = 0;  // 1x1 white texture for flat-color rendering
    unsigned int m_phongTexture = 0;
    unsigned int m_envTexture = 0;

    // Cached uniform locations (Phase 1.6, 1.9): set once at Initialize();
    // eliminates per-draw glGetUniformLocation lookups.
    int m_locModelTexture = -1;        // model shader: uModelTexture sampler
    int m_locModelTint = -1;           // model shader: uTintByFogColor
    int m_locSkyTexture = -1;          // sky shader: uSkyTexture
    int m_locSkyViewport = -1;         // sky shader: uViewport
    int m_locSkyVideoCenter = -1;      // sky shader: uVideoCenter
    int m_locSkyQ = -1;                // sky shader: uQ
    int m_locSkyP = -1;                // sky shader: uP
    int m_locSkyR = -1;                // sky shader: uR
    int m_locSkyTime = -1;             // sky shader: uSkyTime
    int m_locSkyFogBase = -1;          // sky shader: uFogBase
    int m_locSkyUnderwaterDepth = -1;  // sky shader: uUnderwaterDepth
    int m_locSkyWaterLineY = -1;       // sky shader: uWaterLineY (screen Y from top)
    int m_locSkySunScreenPos = -1;     // sky shader: uSunScreenPos (§3.6)
    int m_locSkySunVisibility = -1;    // sky shader: uSunVisibility (§3.6)
    int m_locSkySunGlow = -1;          // sky shader: uSunGlow (§3.6)
    int m_locSkyBodyIsMoon = -1;     // sky shader: uBodyIsMoon (§3.6)
    int m_locSkyPocketFog = -1;        // sky shader: uPocketFog (§3.5)
    int m_locSkyPocketFogColor = -1;   // sky shader: uPocketFogColor (§3.5)
    int m_locSkyCamRight = -1;        // sky shader: uCamRight (world-space gradient)
    int m_locSkyCamUp = -1;           // sky shader: uCamUp (world-space gradient)
    int m_locSkyCamForward = -1;      // sky shader: uCamForward (world-space gradient)

    // PerFrame UBO (Phase 1.1): binding 0, shared by terrain and model shaders.
    // std140 layout: mat4 uProjection + vec2 uFogRange + vec3 uDistanceFogColor
    // + float uForceFog + vec3 uFogColor + mat4 uView + vec4 uWaterAlphaFade.
    // The cached fields are repacked into the UBO on every UpdatePerFrameUBO()
    // call. The projection is recomputed each call because near-model draws
    // (wind indicator, compass, weapon viewmodels) change VideoCX/VideoCY/
    // CameraW/CameraH between the main scene and the HUD overlay, so a
    // per-frame cache would feed a stale matrix to the near-model path.
    unsigned int m_perFrameUBO = 0;
    bool m_perFrameUBOInitialized = false;
    std::array<float, 16> m_cachedProjection{};
    float m_cachedFogStart = 0.0f;
    float m_cachedFogDistance = 0.0f;
    float m_cachedForceFog = 0.0f;
    float m_cachedDistanceFogColor[3] = {0.0f, 0.0f, 0.0f};
    float m_cachedFogColor[3] = {0.0f, 0.0f, 0.0f};
    bool m_hasLastNearModelProjection = false;
    std::array<float, 16> m_lastNearModelProjection{};
    std::map<const TModel*, GLuint> m_modelTextureCache;
    std::map<const TBMPModel*, GLuint> m_bmpTextureCache;
    std::vector<ModelDrawItem> m_worldModelItems;
    std::vector<const ModelDrawItem*> m_transparentModelItems;
    std::vector<Vector2di> m_objectList;

    static const int kTerrainMipLevels = 4;
    static const int kMaxTerrainTextureLayers = 1024;
    // Phase 2.x: global pocket-fog density multiplier. 1.0 matches the
    // D3D/3DFX look.  Increase to make fog pockets denser / more opaque
    // (e.g. 1.5 = 50% thicker fog, 2.0 = double).  Values above 1.0 are
    // per-vertex clamped so fog saturates at 1.0.
    static inline constexpr float kFogDensity = 1.0f;
    // Phase 2.1: initial capacity for the per-frame instance array.
    // The dense custom map Phase 0 baseline measured ~2,400 visible model
    // objects per frame; reserve 4,096 to absorb the high end with
    // headroom. The vector grows automatically if a frame exceeds this.
    static const size_t kInitialInstanceCapacity = 4096;
    // Phase 2.2: initial capacities for the static VBO/IBO. The VBO
    // holds 52 B/vertex after adding four legacy VLight variants and the
    // per-face cutout marker; 8 MB holds about 161K expanded vertices. The IBO holds 4 B/index
    // (uint32_t); 4 MB = 1M indices = 333K faces. Both grow by
    // doubling when a model doesn't fit (clearing the cache). For a
    // typical custom map these are enough; large maps will trigger
    // one growth, which is fine — the cache rebuilds on next access.
    static const size_t kInitialStaticMeshVBOCapacity = 8 * 1024 * 1024;
    static const size_t kInitialStaticMeshIBOCapacity = 4 * 1024 * 1024;

    // §5.4: Flat vertex arrays to eliminate per-frame std::vector
    // reallocations.  The capacity is sized to the worst-case vertex
    // count for the current ctViewR and only grows (never shrinks
    // within a session).  The count resets to 0 each frame.
    std::array<Vector3d, 256> m_terrainFogColorCache{};
    std::array<uint8_t, 256> m_terrainFogColorValid{};
    std::unique_ptr<TerrainVertex[]> m_terrainVertices;
    size_t m_terrainVertexCapacity = 0;
    size_t m_terrainVertexCount    = 0;
#ifdef GL_PERF_HOOKS
    struct TerrainPerfCounters {
        uint32_t chunkCandidates = 0;
        uint32_t tileCandidates = 0;
        uint32_t coarseCulled = 0;
        uint32_t backCulled = 0;
        uint32_t frustumCulled = 0;
        uint32_t distanceCulled = 0;
        uint32_t alphaCulled = 0;
        uint32_t emittedTiles = 0;
    } m_terrainPerf;
#endif
    std::unique_ptr<TerrainVertex[]> m_waterVertices;
    size_t m_waterVertexCapacity = 0;
    size_t m_waterVertexCount    = 0;
    void EnsureTerrainVertexCapacity(size_t needed);
    void EnsureWaterVertexCapacity(size_t needed);

    // Non-owning cache: tracks which terrain textures are currently
    // uploaded to the GPU. Raw pointer (not unique_obj_ptr) because
    // ownership stays with the global Textures[] array. A previous
    // attempt to use unique_obj_ptr here caused release() to steal
    // ownership from Textures[layer], leaving it null on the next
    // frame and crashing the terrain renderer.
    std::array<TEXTURE*, kMaxTerrainTextureLayers> m_uploadedTerrainTextures{};

    // Per-frame mask of water texture layers actually referenced by
    // collected water vertices. Maintained incrementally by
    // AppendWaterTriangle so RenderWaterSurface can skip its
    // O(m_waterVertices) layer scan.
    std::array<bool, kMaxTerrainTextureLayers> m_waterUsedLayers{};

    // Phase 5: Cache IsUnderwater() once per frame to avoid ~500K
    // redundant global loads during the terrain walk.
    bool m_isUnderwater = false;

    // Step 2: Model distance fade alpha (smoothstep curve).
    // Set before each model render call; used by BuildModelDrawItem
    // instead of the legacy GlassL global.
    float m_modelDistanceAlpha = 1.0f;

    // Phase 3: Coarse water bitmask. Built once at level load.
    // Each bit represents an 8x8 cell block; set if any cell in the
    // block has the fmWaterA flag. Allows skipping CollectWaterTileFast
    // for dry blocks without reading FMap[4] per position.
    static constexpr int kWaterBlockShift = 3;  // 8x8 cell blocks
    static constexpr int kWaterBlockDim   = ctMapSize >> kWaterBlockShift;  // 128
    // 128*128 = 16384 bits = 2048 bytes = 256 uint64_t
    static constexpr int kWaterBlockWords = (kWaterBlockDim * kWaterBlockDim + 63) / 64;
    std::array<uint64_t, kWaterBlockWords> m_waterBlockBits{};
    bool m_waterBlockBitsValid = false;
    void RebuildWaterBlockBits();
    bool BlockHasWater(int x, int y) const;

    // Sky pipeline
    GLShader m_skyShader;
    unsigned int m_skyVAO = 0;
    unsigned int m_skyTexture = 0;
    bool m_skyTextureDirty = true;

    // Smoothed sky fog color (temporal filter to absorb day/night sky
    // color changes without frame-to-frame popping). The target color is
    // the global distance-fog color, not the current fixed fog volume.
    Vector3d m_smoothedSkyFogColor = {0.0f, 0.0f, 0.0f};
    bool m_smoothedSkyFogColorInit = false;

    // §3.10: camera-in-fog global envelope parameters.  When the camera is
    // submerged in a tall pocket-fog volume, the fog it is embedded in
    // attenuates the whole scene (terrain, models, sky) by view distance —
    // not just geometry that sits inside the volume.  Computed once per frame
    // in UpdateCameraFogEnvelope() and consumed by the terrain/model shaders.
    float    m_camEnvelopeAmount = 0.0f;
    Vector3d m_camEnvelopeColor  = {0.0f, 0.0f, 0.0f};
    float    m_camEnvFlbSmooth   = 0.0f;   // §3.10 low-passed flb (kills head-bob flicker)

    void InitializeSkyPipeline();
    void ShutdownSkyPipeline();
    void UploadSkyTexture();

    // Sun rendering
    float m_sunLight = 0.0f;
    float m_skyTraceK = 1.0f;
    float m_traceK = 1.0f;
    int m_sunScrX = 0;
    int m_sunScrY = 0;
    int m_lastSunVisibilityScrX = 0;
    int m_lastSunVisibilityScrY = 0;
    unsigned int m_lastSunVisibilityUpdate = 0;
    int m_lastSunTraceScrX = -1;
    int m_lastSunTraceScrY = -1;
    unsigned int m_lastSunTraceFrame = 0;
    float m_lastSunTraceK = 1.0f;
    // Hysteresis latch for uniform-overcast detection (§2.2 workaround).
    // When a large cloud covers both detection and reference rings, the
    // ring-vs-ring detector reports "clear" (dev ≈ 0).  The latch prevents
    // the glow from re-brightening inside such a cloud: once k drops below
    // the threshold, it stays clamped until the sky is confirmed clear for
    // several consecutive frames.
    bool m_cloudLatched = false;
    int m_cloudLatchFrames = 0;
    // Double-buffered PBO for the cloud-occlusion readback (GetSkyK): the GPU
    // copies the sky block into a PBO in the background and we process the
    // previous frame's PBO, eliminating the synchronous GPU->CPU stall.
    GLuint m_skyReadPBO[2] = { 0, 0 };
    int m_skyReadPBOIdx = 0;
    bool m_skyReadPBOReady = false;
    int m_skyReadPBOX[2] = { 0, 0 };   // sun X captured into each PBO
    int m_skyReadPBOY[2] = { 0, 0 };   // sun Y captured into each PBO
    std::vector<ModelVertex> m_sunModelVertices;

    void RenderSun(float x, float y, float z);
    void RenderModelSun(TModel* mptr, float x0, float y0, float z0, int alpha);
    float GetSkyK(int x, int y);
    float GetTraceK(int x, int y);

    // HUD/UI overlay pipeline — uploads lpVideoBuf as a fullscreen quad on top of the 3D scene
    GLShader m_uiShader;
    GLuint m_uiVAO = 0;
    GLuint m_uiVBO = 0;
    GLuint m_uiTexture = 0;
    int m_uiTextureWidth = 0;
    int m_uiTextureHeight = 0;
    void InitializeHudPipeline();
    void ShutdownHudPipeline();
    void EnsureUITexture();
    void UpdateUIPixels();

    // Dirty-rectangle HUD upload tracking — avoids uploading the entire
    // lpVideoBuf every frame when only small regions changed.
    static constexpr int kMaxDirtyRects = 64;
    struct DirtyRect { int x, y, w, h; };
    DirtyRect m_dirtyRects[kMaxDirtyRects];       // rects drawn this frame
    DirtyRect m_prevDirtyRects[kMaxDirtyRects];   // rects from previous frame
    int  m_dirtyRectCount = 0;
    int  m_prevDirtyRectCount = 0;
    bool m_hudNeedsFullUpload = true;  // set after texture (re)creation
    bool m_hudNeedsFullClear  = false; // set after CopyHARDToDIB screenshot

public:
    float GetSunLight() const { return m_sunLight; }

    // §3.10: camera-in-fog global envelope accessors (see members above).
    float    GetCamEnvelopeAmount() const { return m_camEnvelopeAmount; }
    Vector3d GetCamEnvelopeColor()  const { return m_camEnvelopeColor; }
    void     UpdateCameraFogEnvelope();
    void RenderFSRect(uint32_t color, bool additive = true);
    void ApplySunDepthOcclusion();
    void DrawHUDOverlay();

    // Called by functions that write to lpVideoBuf to mark the affected
    // screen region (in lpVideoBuf pixel coordinates, clamped).
    void MarkDirtyRect(int x, int y, int w, int h);

    // Called at frame start instead of full-buffer memset.
    // Clears only the regions that were uploaded last frame, so the
    // lpVideoBuf is zeroed precisely where stale content may remain.
    void ClearStaleHUDRegions();

    // Called after a full-DIB write (e.g., CopyHARDToDIB screenshot)
    // to force the next frame to do a full clear + full upload.
    void InvalidateHUDOverlay();

    // Night desaturation + darkness overlay pipeline
    void InitializeNightDesaturation();
    void ShutdownNightDesaturation();
    void RenderSceneDesaturated();   // desaturate 3D scene only (before HUD)
    void RenderNightDarkness();      // dark overlay only (after HUD)

    // §3.1: Underwater full-screen colour overlay (dynamic per water body)
    void DrawUnderwaterOverlay();

private:
    // Night overlay resources
    GLShader m_nightDesatProgram;
    GLuint m_nightSceneTex = 0;
    GLint  m_locNightDesatTexture = -1;
    GLint  m_locNightDesatStrength = -1;
    int    m_nightTexWidth = 0;
    int    m_nightTexHeight = 0;
};

extern GLRenderer* g_GLRenderer;

#endif // GLRENDERER_H
