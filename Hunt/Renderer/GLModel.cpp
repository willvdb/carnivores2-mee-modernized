// ==========================================================================
// GLModel.cpp � 3D model rendering pipeline
// ==========================================================================

#include "Hunt.h"
#include "Core/BillboardMath.h"
#include "Core/WaterObjectVisibility.h"
#include "GLRenderer.h"
#include "Renderer/GLUtils.h"

#ifdef _gl

#include "glad/glad.h"
#include <cmath>
#include <limits>

void GLRenderer::RenderPlayer(int index)
{
    (void)index;
}

void GLRenderer::RenderShip()
{
}

void GLRenderer::RenderExplosion(int index)
{
    (void)index;
}

void GLRenderer::RenderCharacter(TCharacter* cptr)
{
    (void)cptr;
}

void GLRenderer::ReleaseModelTextures(const TModel* mptr)
{
    if (!mptr) {
        return;
    }

    const auto it = m_modelTextureCache.find(mptr);
    if (it == m_modelTextureCache.end()) {
        return;
    }

    const GLuint texture = it->second;
    m_modelTextureCache.erase(it);

    if (texture && m_hasContext) {
        glDeleteTextures(1, &texture);
    }
}

void GLRenderer::RegisterTexture(TEXTURE* tptr)
{
    (void)tptr;
}

void GLRenderer::RenderModelClipEnvMap(TModel* mptr, float x0, float y0, float z0,
                                       float al, float bt)
{
    // Phase 2.2: ensure the static mesh is uploaded (cache hit after first call).
    UploadStaticMesh(mptr);

    if (!m_modelShader.IsValid() || !m_modelVAO || !m_modelVBO) {
        return;
    }

    GLuint texture = m_envTexture;
    if (!texture) {
        texture = UploadPictureTexture(TFX_ENVMAP);
        if (!texture) {
            return;
        }
        m_envTexture = texture;
    }

    std::vector<ModelVertex> vertices;
    const Vector3d white = {1.0f, 1.0f, 1.0f};
    if (!BuildModelEffectVertices(vertices, mptr, x0, y0, z0, al, bt, sfEnvMap, white)) {
        return;
    }

    const auto projection = m_hasLastNearModelProjection
        ? m_lastNearModelProjection
        : BuildLegacyProjection();
    // RenderNearModel draws the weapon body inside glDepthRange(0, 0.05), so
    // its depth sits in the near slice. This overlay re-draws the same
    // triangles at the same depth, so it must use the same range: at (0, 1)
    // its window depth is ~20x the stored value and GL_LEQUAL discards every
    // fragment, which silently killed the env-map reflection.
    glDepthRange(0.0, kViewmodelDepthRangeMax);
    DrawModelVertices(texture, vertices, projection, true, true, true, true);
    glDepthRange(0.0, 1.0);
}

void GLRenderer::RenderModelClipPhongMap(TModel* mptr, float x0, float y0, float z0,
                                         float al, float bt)
{
    // Phase 2.2: ensure the static mesh is uploaded (cache hit after first call).
    UploadStaticMesh(mptr);

    if (!m_modelShader.IsValid() || !m_modelVAO || !m_modelVBO) {
        return;
    }

    GLuint texture = m_phongTexture;
    if (!texture) {
        texture = UploadPictureTexture(TFX_SPECULAR);
        if (!texture) {
            return;
        }
        m_phongTexture = texture;
    }

    std::vector<ModelVertex> vertices;
    const auto clampSkyChannel = [](int value) -> float {
        return static_cast<float>(value > 255 ? 255 : value) / 255.0f;
    };
    const Vector3d color = {
        clampSkyChannel(SkyR + 64),
        clampSkyChannel(SkyG + 64),
        clampSkyChannel(SkyB + 64)
    };
    if (!BuildModelEffectVertices(vertices, mptr, x0, y0, z0, al, bt, sfPhong, color)) {
        return;
    }

    const auto projection = m_hasLastNearModelProjection
        ? m_lastNearModelProjection
        : BuildLegacyProjection();
    // Same near-slice requirement as the env-map overlay above: the body's
    // depth lives in the viewmodel depth range, so drawing the specular pass
    // at (0, 1) put it behind the body it belongs to and GL_LEQUAL dropped it.
    glDepthRange(0.0, kViewmodelDepthRangeMax);
    DrawModelVertices(texture, vertices, projection, true, true, true, true);
    glDepthRange(0.0, 1.0);
}

void GLRenderer::RenderNearModel(TModel* mptr, float x0, float y0, float z0,
                                 int light, int vt, float al, float bt)
{
    // Phase 2.2: ensure the static mesh is uploaded (cache hit after first call).
    UploadStaticMesh(mptr);

    // Viewmodels (weapon/wind/compass/binocular) are anchored to the camera
    // and must never be distance-faded.  m_modelDistanceAlpha is set per
    // world-model during the scene render and is NOT reset afterward, so
    // without this the weapon would inherit the LAST world model's fade
    // alpha: a far last model makes forceDistanceBlend true and routes every
    // weapon face into the transparent (blended, no-depth-write) pass, leaving
    // the weapon half-transparent or invisible while its Phong/EnvMap overlays
    // (drawn separately) stay visible.
    m_modelDistanceAlpha = 1.0f;

    ModelDrawItem item;
    if (!BuildModelDrawItem(item, mptr, x0, y0, z0, light, vt, al, bt, false, true, true, false)) {
        return;
    }
    item.texture = UploadModelTexture(mptr);
    if (!item.texture) {
        return;
    }

    const float ca = std::cos(al);
    const float sa = std::sin(al);
    const float cb = std::cos(bt);
    const float sb = std::sin(bt);
    for (int s = 0; s < mptr->VCount; ++s) {
        rVertex[s].x = (mptr->gVertex[s].x * ca + mptr->gVertex[s].z * sa) + x0;
        const float vz = mptr->gVertex[s].z * ca - mptr->gVertex[s].x * sa;
        rVertex[s].y = (mptr->gVertex[s].y * cb - vz * sb) + y0;
        rVertex[s].z = (vz * cb + mptr->gVertex[s].y * sb) + z0;
    }

    const auto projection = BuildLegacyProjection();
    m_lastNearModelProjection = projection;
    m_hasLastNearModelProjection = true;

    // Depth-coherent viewmodels: compress the overlay into the near slice
    // of the SAME depth buffer instead of clearing it. The old glClear
    // erased world depth mid-frame, so anything reading depth at present
    // time (ReShade, GetTraceK sun occlusion) saw the gun only. With the
    // range sandwich the gun still depth-tests nearer than any world
    // fragment (always-on-top preserved, no wall clipping), world depth
    // survives to SwapBuffers, and gun-vs-gun overlap keeps resolving in
    // relative order. Restoring (0,1) is exact. Slice 0.05 leaves 24-bit
    // precision far beyond what a 200-triangle viewmodel needs.
    //
    // The weapon's specular/env-map overlays (RenderModelClipPhongMap /
    // RenderModelClipEnvMap) re-draw these exact triangles, so they must
    // reuse kViewmodelDepthRangeMax too -- see GLUtils.h.
    glDepthRange(0.0, kViewmodelDepthRangeMax);
    DrawModelVertices(item.texture, item.opaqueVertices, projection, true, false, false);
    if (!item.cutoutVertices.empty()) {
        DrawModelVertices(item.texture, item.cutoutVertices, projection, true, false, false);
    }
    if (!item.transparentVertices.empty()) {
        DrawModelVertices(item.texture, item.transparentVertices, projection, true, true, false);
    }
    glDepthRange(0.0, 1.0);
}

void GLRenderer::RenderModelClipWater(TModel* mptr, float x0, float y0, float z0,
                                      int light, int vt, float al, float bt)
{
    // 3dfx parity: do NOT CPU-clip the model against the water plane.
    // The 3dfx renderer's RenderModelClipWater is a no-op and falls
    // through to RenderModelClip (which only does frustum clipping).
    // The water surface itself is alpha-blended on top of the model
    // in RenderWaterSurface() (drawn later in DrawScene), so the
    // underwater portion of the model remains visible through the
    // water -- this matches the reference 3dfx screenshot.
    //
    // The previous GL implementation called ClipTriangleAgainstWater
    // for every face, which dropped entire triangles below the water
    // plane. On models that intersect the water surface (e.g. rocks
    // sticking out of a lake) this produced visible "missing
    // triangle" holes, because the CPU clipper removed faces that
    // the original 3dfx path kept.
    //
    // Phase 2.10 was previously expected to move the water plane cut
    // into the fragment shader for a softer look, but until that
    // lands, matching 3dfx by leaving the mesh intact is the
    // visually-correct behaviour.
    RenderModelClip(mptr, x0, y0, z0, light, vt, al, bt);
}

void GLRenderer::RenderModelClip(TModel* mptr, float x0, float y0, float z0,
                                 int light, int vt, float al, float bt)
{
    // Phase 2.2: ensure the static mesh is uploaded (cache hit after first call).
    UploadStaticMesh(mptr);

    ModelDrawItem item;
    if (!BuildModelDrawItem(item, mptr, x0, y0, z0, light, vt, al, bt, false, false, true, false)) {
        return;
    }
    item.texture = UploadModelTexture(mptr);
    if (!item.texture) {
        return;
    }
    m_worldModelItems.push_back(std::move(item));
}

void GLRenderer::RenderModel(TModel* mptr, float x0, float y0, float z0,
                             int light, int vt, float al, float bt)
{
    // Phase 2.2: ensure the static mesh is uploaded (cache hit after first call).
    UploadStaticMesh(mptr);

    ModelDrawItem item;
    if (!BuildModelDrawItem(item, mptr, x0, y0, z0, light, vt, al, bt, false, false, false, false)) {
        return;
    }
    item.texture = UploadModelTexture(mptr);
    if (!item.texture) {
        return;
    }
    m_worldModelItems.push_back(std::move(item));
}

void GLRenderer::RenderBMPModel(TBMPModel* mptr, float x0, float y0, float z0, int light)
{
    if (!mptr) {
        return;
    }

    const GLuint texture = UploadBMPModelTexture(mptr);
    if (!texture) {
        return;
    }

    // Queue into m_worldModelItems instead of drawing immediately.
    // This ensures BMP models participate in the same depth-sorted
    // rendering pipeline as regular models. Previously they were
    // drawn with enableBlend=true (no depth writes), which let
    // water and other models overdraw them.
    ModelDrawItem item;
    item.texture = texture;
    const Vector3d center = {x0, y0, z0};
    item.distance = VectorLengthSq(center);
    item.additive = false;

    const float baseLight = std::clamp(static_cast<float>(light), 0.0f, 255.0f);
    const float alpha = m_modelDistanceAlpha;

    // Unrotate view-space (x0,y0,z0) back to world-relative
    // coordinates for fog sampling. RotateVector was applied in
    // RenderMappedObject — we invert it here so SampleFogAtPoint
    // sees a stable world-space position independent of camera
    // angle. Regular models do this in BuildModelDrawItem; BMP
    // models must do it here since they bypass that path.
    const float ucY = ::cb * y0 + ::sb * z0;
    const float ucZ = ::cb * z0 - ::sb * y0;
    const Vector3d unrotatedCenter = {
        ::ca * x0 - ::sa * ucZ,
        ucY,
        ::sa * x0 + ::ca * ucZ
    };
    // Phase 2.x: 3DFX-style height-graded fog for billboards.
    // CalcFogLevel at the object centre gives the base fog level
    // (FogYBase) and the pocket colour (stored in global CurFogColor).
    // Sampling 800 units higher gives the Y-gradient (FogYGrad).
    // Each vertex then gets: fog = FogYBase + localY * FogYGrad.
    // This produces the same per-vertex gradient the 3DFX / D3D
    // renderers produce, with more fog at the top of tall sprites
    // (e.g. tree-tops) and less at the base.
    const float fogBase = CalcFogLevel(unrotatedCenter);
    const Vector3d fogColor3dfx =
        IsUnderwater() ? DecodeFogColorBGR(FogsList[127].fogRGB) : DecodeFogColor(CurFogColor);

    float fogGrad = 0.0f;
    if (fogBase > 0.0f) {
        Vector3d highPoint = unrotatedCenter;
        highPoint.y += 800.0f;
        const float fogHigh = CalcFogLevel(highPoint);
        fogGrad = (fogHigh - fogBase) / 800.0f;
    }

    const bool hasFade = alpha < 0.999f;

    // Match the original D3D/3dfx cylindrical billboard: the horizontal
    // axis faces the camera, while the vertical axis remains fixed in world
    // space and is transformed by camera pitch. This prevents distant scenery
    // from tilting with the screen when the player looks up or down.
    BillboardViewOffset offsets[4];
    for (int index = 0; index < 4; ++index) {
        offsets[index] = CalculateCylindricalBillboardViewOffset(
            mptr->gVertex[index].x, mptr->gVertex[index].y, ::cb, ::sb);
        if (z0 + offsets[index].z >= -256.0f) {
            return;
        }
    }

    // Build two triangles (0-1-2, 0-2-3) for the billboard quad.
    auto makeVertex = [&](int index, float u, float v) -> ModelVertex {
        const float vertexFog = fogBase + mptr->gVertex[index].y * fogGrad;
        const float fogAmount = std::clamp((vertexFog / 255.0f) * kFogDensity, 0.0f, 1.0f);
        return {
            offsets[index].x + x0,
            offsets[index].y + y0,
            offsets[index].z + z0,
            u, v,
            Light255ToByte(baseLight),
            Float01ToByte(fogAmount),
            Float01ToByte(alpha),
            CutoutToByte(!hasFade),  // cutout when no fade, opaque when fading
            Float01ToByte(fogColor3dfx.x),
            Float01ToByte(fogColor3dfx.y),
            Float01ToByte(fogColor3dfx.z),
            {0, 0, 0, 0, 0}
        };
    };

    const ModelVertex v0 = makeVertex(0, 0.0f, 0.0f);
    const ModelVertex v1 = makeVertex(1, 1.0f, 0.0f);
    const ModelVertex v2 = makeVertex(2, 1.0f, 1.0f);
    const ModelVertex v3 = makeVertex(3, 0.0f, 1.0f);

    // When GlassL==0 (no distance fade): use cutoutVertices so the
    // billboard writes depth (discarding black pixels). This prevents
    // water and other models from overdraw.
    // When GlassL>0 (distance fade): use transparentVertices for
    // alpha blending, sorted back-to-front with other transparent items.
    auto& target = hasFade ? item.transparentVertices : item.cutoutVertices;
    target.reserve(6);
    target.push_back(v0); target.push_back(v1); target.push_back(v2);
    target.push_back(v0); target.push_back(v2); target.push_back(v3);

    m_worldModelItems.push_back(std::move(item));
}

void GLRenderer::RenderModelsList()
{
#ifdef GL_PERF_HOOKS
    GL_PERF_SCOPE("RenderModelsList");
#endif
    // Phase 1.11: reset the last-bound model texture tracker at the
    // start of each frame's model-draw session.
    m_lastBoundModelTexture = 0;

    // Phase 2.3: populate instance data for instanced objects.
    // RenderMappedObject now adds to m_instanceData for non-BMP,
    // non-water-clip objects.
    m_instanceData.clear();
    m_exactShades.clear();
    m_instanceInfo.clear();
    {
        GL_PERF_CPU_SCOPE("Models_Prepare");
        for (const Vector2di& object : m_objectList) {
            RenderMappedObject(object.x, object.y);
        }
        m_objectList.clear();
    }

    // Phase 2.3: render instanced models (non-BMP, non-water-clip).
    if (!m_instanceData.empty()) {
        GL_PERF_CPU_SCOPE("Models_Instanced");
        RenderInstancedModels();
    }

    // Phase 2.3: render legacy path models (water-clip, BMP).
    {
        GL_PERF_CPU_SCOPE("Models_Legacy");
        RenderWorldModels();
    }
}

void GLRenderer::RenderInstancedModels()
{
    // Phase 2.3: render instanced models with one glDrawElementsInstanced
    // per (model, texture) group. The instance data was populated by
    // RenderMappedObject calls above.

    if (m_instanceData.empty() || m_instanceInfo.empty() ||
        m_instanceData.size() != m_instanceInfo.size() ||
        !m_instancedModelShader.IsValid() || !m_instanceVAO) {
        return;
    }

    // Phase 2.11: bucket the opaque/cutout records globally before building
    // draw groups. RenderMappedObject routes transparent, BMP, and
    // water-clipped geometry to the legacy queues before it reaches these
    // arrays, so this does not reorder transparent geometry. Cutout records
    // remain depth-tested in the same instanced shader; sorting them with
    // their opaque peers is order-independent and preserves the cutout flag.
    const auto instanceKeyLess = [](const InstanceInfo& lhs,
                                    const InstanceInfo& rhs) {
        const std::uintptr_t lhsModel =
            reinterpret_cast<std::uintptr_t>(lhs.model);
        const std::uintptr_t rhsModel =
            reinterpret_cast<std::uintptr_t>(rhs.model);
        if (lhsModel != rhsModel) {
            return lhsModel < rhsModel;
        }
        return lhs.texture < rhs.texture;
    };

    {
        GL_PERF_CPU_SCOPE("Models_InstanceSort");
        bool alreadyBucketed = true;
        for (size_t i = 1; i < m_instanceInfo.size(); ++i) {
            if (instanceKeyLess(m_instanceInfo[i], m_instanceInfo[i - 1])) {
                alreadyBucketed = false;
                break;
            }
        }

        if (!alreadyBucketed) {
            m_instanceSortScratch.resize(m_instanceData.size());
            for (size_t i = 0; i < m_instanceData.size(); ++i) {
                m_instanceSortScratch[i].data = m_instanceData[i];
                m_instanceSortScratch[i].info = m_instanceInfo[i];
            }

            std::sort(m_instanceSortScratch.begin(), m_instanceSortScratch.end(),
                      [&instanceKeyLess](const InstanceSortRecord& lhs,
                                          const InstanceSortRecord& rhs) {
                          return instanceKeyLess(lhs.info, rhs.info);
                      });

            for (size_t i = 0; i < m_instanceData.size(); ++i) {
                m_instanceData[i] = m_instanceSortScratch[i].data;
                m_instanceInfo[i] = m_instanceSortScratch[i].info;
            }
        }
    }

    // Phase 2.9: removed wasteful full-array upload.  The per-group
    // loop below uploads only the current group's slice to VBO offset 0
    // (GL 3.3 workaround for missing glDrawElementsInstancedBaseInstance).
    // The full-array upload was always overwritten by the first group.

    // Group instances by (model, texture) for instanced draws.
    struct InstanceGroup {
        const TModel* model;
        GLuint texture;
        uint32_t instanceStart;
        uint32_t instanceCount;
    };
    std::vector<InstanceGroup> groups;

    // Build groups by detecting transitions in the instance list.
    // Instances are added in object-list order, so objects with the
    // same model/texture are often adjacent.
    {
        GL_PERF_CPU_SCOPE("Models_InstanceGroup");
        const TModel* curModel = m_instanceInfo[0].model;
        GLuint curTexture = m_instanceInfo[0].texture;
        uint32_t groupStart = 0;

        for (size_t i = 1; i < m_instanceInfo.size(); ++i) {
            if (m_instanceInfo[i].model != curModel ||
                m_instanceInfo[i].texture != curTexture) {
                // End of current group.
                groups.push_back({curModel, curTexture, groupStart,
                                  static_cast<uint32_t>(i - groupStart)});
                curModel = m_instanceInfo[i].model;
                curTexture = m_instanceInfo[i].texture;
                groupStart = static_cast<uint32_t>(i);
            }
        }
        // Add the last group.
        groups.push_back({curModel, curTexture, groupStart,
                          static_cast<uint32_t>(m_instanceInfo.size() - groupStart)});
    }

    // Measure CPU submission/driver cost separately from placement
    // preparation, sorting, and group construction. The outer
    // RenderModelsList scope remains the sole GPU timer for this pass.
    GL_PERF_CPU_SCOPE("Models_InstanceSubmit");

    // Set up rendering state.
    const auto projection = BuildLegacyProjection();
    UpdatePerFrameUBO(projection);

    m_instancedModelShader.Use();
    if (!m_exactShades.empty()) {
        GL_PERF_CPU_SCOPE("Models_ExactUpload");
        glBindBuffer(GL_TEXTURE_BUFFER, m_exactShadeBuffer);
        glBufferData(GL_TEXTURE_BUFFER, m_exactShades.size() * sizeof(ExactShade),
                     m_exactShades.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_BUFFER, m_exactShadeTexture);
    glUniform1i(glGetUniformLocation(m_instancedModelShader.GetProgramID(), "uExactShade"), 1);
    glActiveTexture(GL_TEXTURE0);
    {
        static const GLint uNight = glGetUniformLocation(m_instancedModelShader.GetProgramID(), "uNightStrength");
        if (uNight >= 0) {
            glUniform1f(uNight, (OptDayNight == 2 && !NightVisionOn) ? 1.0f : 0.0f);
        }
    }
    glBindVertexArray(m_instanceVAO);

    // §3.10: camera-in-fog global envelope (see GLRenderer::UpdateCameraFogEnvelope).
    {
        static const GLint uCamFogCol = glGetUniformLocation(m_instancedModelShader.GetProgramID(), "uCamFogColor");
        static const GLint uCamFogAmt = glGetUniformLocation(m_instancedModelShader.GetProgramID(), "uCamFogAmount");
        if (uCamFogCol >= 0 && uCamFogAmt >= 0 && g_GLRenderer) {
            const Vector3d c = g_GLRenderer->GetCamEnvelopeColor();
            glUniform3f(uCamFogCol, c.x, c.y, c.z);
            glUniform1f(uCamFogAmt, g_GLRenderer->GetCamEnvelopeAmount());
        }
    }

    // Phase 2.9: orphan the instance VBO once (glBufferData with
    // nullptr) to avoid per-group stalls.  Size to the largest group
    // in bytes.  The per-group loop then uses glBufferSubData without
    // further orphans — the buffer was just orphaned so the driver
    // knows all content is being replaced.
    GLsizeiptr maxGroupBytes = 0;
    for (const auto& g : groups) {
        const GLsizeiptr gb = static_cast<GLsizeiptr>(g.instanceCount) * sizeof(ModelInstance);
        if (gb > maxGroupBytes) maxGroupBytes = gb;
    }
    if (maxGroupBytes < static_cast<GLsizeiptr>(sizeof(ModelInstance)))
        maxGroupBytes = sizeof(ModelInstance);

    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, maxGroupBytes, nullptr, GL_STREAM_DRAW);
    // Keep m_instanceVBO bound — the VAO references it for attributes
    // 4-14. The per-group loop glBufferSubData's into this same buffer.

    // Bind the static IBO for indexed drawing.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_staticMeshIBO);

    // Enable depth test, disable blend (opaque pass).
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // Resolve the mesh base at submission: static-cache growth can rebase
    // meshes after a placement's shading record was prepared.
    const GLint exactBaseLocation = glGetUniformLocation(m_instancedModelShader.GetProgramID(), "uExactBaseVertex");
    // Draw each group with instanced rendering.
    uint32_t totalDrawCalls = 0;
    uint32_t totalInstances = 0;

    for (const auto& group : groups) {
        const StaticMeshEntry* meshEntry = GetStaticMeshEntry(group.model);
        if (!meshEntry || meshEntry->indexCount == 0) {
            continue;
        }

        glUniform1i(exactBaseLocation, static_cast<GLint>(meshEntry->baseVertex));

        // Phase 2.9: m_instanceVBO was already bound + orphaned above.
        // glBufferSubData writes the group's slice to offset 0 without
        // an extra bind/unbind round-trip.
        const GLsizeiptr groupSliceBytes =
            static_cast<GLsizeiptr>(group.instanceCount) * sizeof(ModelInstance);
        glBufferSubData(GL_ARRAY_BUFFER, 0, groupSliceBytes,
                        &m_instanceData[group.instanceStart]);

        // Bind the texture for this group.
        if (group.texture != m_lastBoundModelTexture) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, group.texture);
            m_lastBoundModelTexture = group.texture;
#ifdef GL_PERF_HOOKS
            GL_PERF_TEXTURE_BIND(group.texture);
#endif
        }

        // Issue the instanced draw call.
        // The indices in the static IBO reference the static VBO directly.
        glDrawElementsInstanced(
            GL_TRIANGLES,
            static_cast<GLsizei>(meshEntry->indexCount),
            GL_UNSIGNED_INT,
            reinterpret_cast<void*>(
                static_cast<uintptr_t>(meshEntry->baseIndex * sizeof(uint32_t))),
            static_cast<GLsizei>(group.instanceCount));

#ifdef GL_PERF_HOOKS
        GL_PERF_DRAW((meshEntry->indexCount / 3u) * group.instanceCount);
#endif
        totalDrawCalls++;
        totalInstances += group.instanceCount;
    }

    // Phase 2.9: unbind the instance VBO (was left bound for the
    // per-group glBufferSubData loop).
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Phase 2.3: unbind the VAO to avoid leaking instance-attribute
    // state into subsequent draws (e.g., the legacy model path in
    // RenderWorldModels).  Do NOT reset glVertexAttribDivisor on
    // m_instanceVAO — VAO state is persistent and we need divisor=1
    // for the next frame's instanced draws.
    glBindVertexArray(0);
    glUseProgram(0);

    (void)totalDrawCalls;
    (void)totalInstances;
}

void GLRenderer::RenderMappedObject(int x, int y)
{
    GL_PERF_CPU_SCOPE("Models_Placement");
    const int ob = OMap[y][x];
    if (!MObjects[ob].model) {
        return;
    }

    const int flags = MObjects[ob].info.flags;
    const int FI = (FMap[y][x] >> 2) & 3;
    const float fi = CameraAlpha + static_cast<float>(FI) * 2.0f * pi / 4.0f;
    const bool groundLighting = !(flags & ofDEFLIGHT) && (flags & ofGRNDLIGHT);
    const bool animated = (flags & ofANIMATED) != 0;

    int mlight;
    if (flags & ofDEFLIGHT) {
        mlight = MObjects[ob].info.DefLight;
    } else if (groundLighting) {
        // The instanced shader normally replaces the legacy 128 +
        // (GetLandLt2 - 128) expression with the sampled VMap value itself.
        mlight = 128;
    } else {
        mlight = -(RandomMap[y & 31][x & 31] >> 5) + (LMap[y][x] >> 1) + 96;
    }

    mlight = std::clamp(mlight, 64, 192);

    Vector3d pos;
    pos.x = x * 256 + 128 - CameraX;
    pos.z = y * 256 + 128 - CameraZ;
    pos.y = static_cast<float>(HMapO[y][x]) * ctHScale - CameraY;

    const float distanceSq = VectorLengthSq(pos);
    // Do not reject an entire model just because its origin cell's terrain is
    // above its authored YHi. The original D3D/3DFX renderers deliberately
    // left this test disabled: large user-placed structures can extend over
    // lower terrain even when their origin is buried. Manya's Paradise uses
    // that layout for its cave roof, so the origin-cell test removed whole
    // roof sections and exposed the sky from inside the cave.

    waterclip = false;
    if (!IsUnderwater() && (FMap[y][x] & fmWaterA)) {
        const float objectBaseY = static_cast<float>(HMapO[y][x]) * ctHScale;
        const float objectTopY = objectBaseY + MObjects[ob].info.YHi;
        const float waterSurfaceY = static_cast<float>(WaterList[WMap[y][x]].wlevel) * ctHScale;

        // Fully submerged scenery must remain renderable through the translucent
        // water post-pass. Only objects crossing the plane need the legacy
        // water-intersection path; submerged static objects retain instancing.
        if (ObjectIntersectsWaterSurface(objectBaseY, objectTopY, waterSurfaceY)) {
            waterclipbase = pos;
            waterclipbase.y = waterSurfaceY - CameraY;
            waterclipbase = RotateVector(waterclipbase);
            waterclip = true;
        }
    }

    // Phase 2.x: 3DFX-style height-graded pocket fog for instanced models.
    // CalcFogLevel at the object centre gives the base fog level and the
    // pocket colour (via global CurFogColor).  Sampling 800 units higher
    // gives the Y-gradient.  The vertex shader computes per-vertex fog as:
    //   fog = fogBase + modelSpaceY * fogGrad
    // This matches the D3D / 3DFX look: tall objects fade more at the top.
    const Vector3d unrotatedFogPos = pos;  // before RotateVector
    const float fogBase = CalcFogLevel(unrotatedFogPos);
    const Vector3d fogPocketColor =
        IsUnderwater() ? DecodeFogColorBGR(FogsList[127].fogRGB) : DecodeFogColor(CurFogColor);

    float fogGrad = 0.0f;
    if (fogBase > 0.0f) {
        Vector3d highPoint = unrotatedFogPos;
        highPoint.y += 800.0f;
        const float fogHigh = CalcFogLevel(highPoint);
        fogGrad = (fogHigh - fogBase) / 800.0f;
    }

    pos = RotateVector(pos);
    float zs = 0.0f;
    // Use CalcTerrainAlpha for smoothstep model fade (same range as terrain)
    const float modelFadeStart = static_cast<float>((ctViewR - 8) << 8);
    const float modelFadeEnd = 256.0f * static_cast<float>(ctViewR - 4);
    m_modelDistanceAlpha = CalcTerrainAlpha(distanceSq, modelFadeStart,
                                            modelFadeStart * modelFadeStart,
                                            modelFadeEnd, m_isUnderwater);
    GlassL = 0;  // Legacy compatibility

    if (m_modelDistanceAlpha <= 0.005f) {
        return;
    }

    // Keep zs for BMP distance check below
    if (distanceSq > modelFadeStart * modelFadeStart) {
        zs = static_cast<float>(std::sqrt(distanceSq));
    }

    int legacyLightVariant = FI;
    bool legacyGroundLightReady = false;
    auto prepareLegacyGroundLight = [&]() {
        if (groundLighting && !legacyGroundLightReady) {
            GL_PERF_CPU_SCOPE("Exact_GroundLight");
            CalcModelGroundLight(MObjects[ob].model.get(), x * 256 + 128, y * 256 + 128, FI);
            legacyLightVariant = 0; // D3D/3DFX selected the generated VLight[0].
            legacyGroundLightReady = true;
        }
    };

    // Preserve the old ground-light/morph ordering for the uncommon combined
    // flag case. Static ground-lit models use the exact instanced path below.
    if (groundLighting && animated) {
        prepareLegacyGroundLight();
    }
    if (animated && MObjects[ob].info.LastAniTime != RealTime) {
        GL_PERF_CPU_SCOPE("Models_Morph");
        MObjects[ob].info.LastAniTime = RealTime;
        CreateMorphedObject(MObjects[ob].model.get(), MObjects[ob].vtl, RealTime % MObjects[ob].vtl.AniTime);
    }

    bool renderAsBMP = false;
    if (!(flags & ofNOBMP)) {
        const float bmpDistanceLimit = ctViewRM * 256.0f;
        const float bmpDistanceLimitSq = bmpDistanceLimit * bmpDistanceLimit;
        if (distanceSq > bmpDistanceLimitSq) {
            if (m_modelDistanceAlpha < 1.0f) {
                renderAsBMP = zs > bmpDistanceLimit;
            } else {
                const float distance = static_cast<float>(std::sqrt(distanceSq));
                renderAsBMP = distance > bmpDistanceLimit;
            }
        }
    }

    if (renderAsBMP) {
        // Phase 2.3: BMP fallback path unchanged.
        GL_PERF_CPU_SCOPE("Models_BMP");
        RenderBMPModel(&MObjects[ob].bmpmodel, pos.x, pos.y, pos.z, mlight - 16);
    } else if (waterclip) {
        GL_PERF_CPU_SCOPE("Models_WaterFallback");
        // Water-clipped objects use the legacy non-instanced path so
        // the model mesh is not consumed by the instanced bucket.
        // The mesh is drawn without CPU-side water plane clipping
        // (see RenderModelClipWater); the water surface is
        // alpha-blended on top in RenderWaterSurface() to produce
        // the underwater appearance, matching the 3dfx renderer.
        prepareLegacyGroundLight();
        UploadStaticMesh(MObjects[ob].model.get());
        RenderModelClipWater(MObjects[ob].model.get(), pos.x, pos.y, pos.z,
                             mlight, legacyLightVariant, fi, CameraBeta);
    } else {
        // Phase 2.3: instanced path for non-BMP, non-water-clip objects.
        // Compute world matrix from position and rotation.
        const StaticMeshEntry meshEntry = UploadStaticMesh(MObjects[ob].model.get());

        // Phase 2.3: route models with sfTransparent faces through the
        // legacy path (they need blend which the instanced opaque pass
        // does not set up).  The proper instanced transparent pass is
        // deferred to a follow-up task.
        if (meshEntry.hasTransparent || (groundLighting && animated) ||
            (animated && !GpuFeatureEnabled(GPUF_ANIMATED_SCENERY))) {
            GL_PERF_CPU_SCOPE("Models_BlendAnimFallback");
            prepareLegacyGroundLight();
            RenderModelClip(MObjects[ob].model.get(), pos.x, pos.y, pos.z,
                            mlight, legacyLightVariant, fi, CameraBeta);
            return;
        }

        // All placements of a map-object animation share the same RealTime
        // phase. Refresh that model's expanded vertex range once, then retain
        // one instanced draw for every placement (dense swaying vegetation).
        if (animated) {
            GL_PERF_CPU_SCOPE("Models_AnimUpload");
            UpdateAnimatedStaticMesh(MObjects[ob].model.get());
        }

        const float ca = std::cos(fi);
        const float sa = std::sin(fi);
        const float cb = std::cos(CameraBeta);
        const float sb = std::sin(CameraBeta);

        ModelInstance instance{};
        // Phase 2.3: populate the view-from-model matrix COLUMNS.
        // GLSL mat4(col0,col1,col2,col3) takes column vectors, so
        // we fill worldCol0-3 as the four columns of:
        //   | ca       0        sa       pos.x |
        //   | sa*sb    cb       -ca*sb   pos.y |
        //   | -sa*cb   sb       ca*cb    pos.z |
        //   | 0        0        0        1     |
        instance.worldCol0[0] = ca;
        instance.worldCol0[1] = sa * sb;
        instance.worldCol0[2] = -sa * cb;
        instance.worldCol0[3] = 0.0f;
        instance.worldCol1[0] = 0.0f;
        instance.worldCol1[1] = cb;
        instance.worldCol1[2] = sb;
        instance.worldCol1[3] = 0.0f;
        instance.worldCol2[0] = sa;
        instance.worldCol2[1] = -ca * sb;
        instance.worldCol2[2] = ca * cb;
        instance.worldCol2[3] = 0.0f;
        instance.worldCol3[0] = pos.x;
        instance.worldCol3[1] = pos.y;
        instance.worldCol3[2] = pos.z;
        instance.worldCol3[3] = 1.0f;

        // Instance light: normalize to [0,1] range (current mlight is 64-192).
        // Phase 2.x: .yzw carry the per-object pocket-fog colour (3DFX-style).
        instance.instanceLight[0] = static_cast<float>(mlight) / 255.0f;
        instance.instanceLight[1] = fogPocketColor.x;
        instance.instanceLight[2] = fogPocketColor.y;
        instance.instanceLight[3] = fogPocketColor.z;

        // sfOpacity is carried by each expanded face vertex in the static
        // mesh. Only those faces are alpha-tested; mixed solid/cutout models
        // therefore retain the D3D/3DFX semantics without losing instancing.
        // Phase 2.x: .y = fogGrad (Y-gradient, was tintByFog=0).
        //            .z = fogBase (pocket-fog amount at object centre).
        const float alpha = m_modelDistanceAlpha;
        instance.instanceFlags[0] = static_cast<float>(FI);
        instance.instanceFlags[1] = (fogGrad / 255.0f) * kFogDensity; // Phase 2.x: fog Y-gradient
        instance.instanceFlags[2] = (fogBase / 255.0f) * kFogDensity; // Phase 2.x: fog base amount
        instance.instanceFlags[3] = alpha; // alpha

        if (groundLighting &&
            !PopulateGroundLightInstance(instance, meshEntry,
                                         x * 256 + 128, y * 256 + 128, FI)) {
            GL_PERF_CPU_SCOPE("Models_GroundFallback");
#ifdef GL_PERF_HOOKS
            // One diagnostic record per model/orientation/reason, not per placement.
            // The helper can reject view-grid coverage as well as footprint size.
            static bool reported[256][4][3] = {};
            float loX = meshEntry.minX, hiX = meshEntry.maxX;
            float loZ = meshEntry.minZ, hiZ = meshEntry.maxZ;
            if (FI == 1) { loX = meshEntry.minZ; hiX = meshEntry.maxZ; loZ = -meshEntry.maxX; hiZ = -meshEntry.minX; }
            if (FI == 2) { loX = -meshEntry.maxX; hiX = -meshEntry.minX; loZ = -meshEntry.maxZ; hiZ = -meshEntry.minZ; }
            if (FI == 3) { loX = -meshEntry.maxZ; hiX = -meshEntry.minZ; loZ = meshEntry.minX; hiZ = meshEntry.maxX; }
            const float wx = x * 256 + 128.0f, wz = y * 256 + 128.0f;
            const bool negative = wx + loX < 0 || wz + loZ < 0;
            const int spanX = static_cast<int>(wx + hiX) / 512 - static_cast<int>(wx + loX) / 512;
            const int spanZ = static_cast<int>(wz + hiZ) / 512 - static_cast<int>(wz + loZ) / 512;
            const int reason = negative ? 0 : (spanX > 2 || spanZ > 2) ? 1 : 2;
            if (!reported[ob][FI][reason]) {
                reported[ob][FI][reason] = true;
                LOG_INFO("GroundFallback model=%d orientation=%d reason=%d span=%d,%d bounds=%.1f,%.1f,%.1f,%.1f flags=%d vertices=%d faces=%d",
                         ob, FI, reason, spanX, spanZ, loX, hiX, loZ, hiZ,
                         flags, MObjects[ob].model->VCount, MObjects[ob].model->FCount);
            }
#endif
            // A footprint spanning more than the 4x4 VMap sample grid can
            // represent keeps the exact CPU path rather than an approximation.
            prepareLegacyGroundLight();
            if (!PopulateExactShadeInstance(instance, meshEntry,
                                            MObjects[ob].model.get(), pos, fi)) {
                RenderModelClip(MObjects[ob].model.get(), pos.x, pos.y, pos.z,
                                mlight, legacyLightVariant, fi, CameraBeta);
                return;
            }
        }

        // Ensure capacity and add instance.
        m_instanceData.push_back(instance);

        // Track model and texture for instanced draw grouping.
        InstanceInfo info;
        info.model = MObjects[ob].model.get();
        info.texture = UploadModelTexture(MObjects[ob].model.get());
        m_instanceInfo.push_back(info);
    }
}

void GLRenderer::RenderObject(int x, int y)
{
    if (x < 0 || y < 0 || x >= ctMapSize || y >= ctMapSize) {
        return;
    }
    if (OMap[y][x] == 255 || !MODELS) {
        return;
    }
    // Safety cap.  Each cell is visited at most once per frame by the
    // 1x1 ring walk in CollectTerrainTile's caller.  Dense custom maps
    // at max view distance may still push beyond 8K unique objects —
    // 32K is a generous upper bound (~256 KB in m_objectList, ~6 MB in
    // m_instanceData).
    if (m_objectList.size() >= 32768) {
        static int hitCount = 0;
        ++hitCount;
        if (hitCount <= 20 || hitCount % 100 == 0) {
            LOG_WARN("m_objectList hit 32768 cap (%zu entries); objects dropped (hit #%d)",
                     m_objectList.size(), hitCount);
        }
        return;
    }

    m_objectList.push_back({x, y});
}

void GLRenderer::RenderWorldModels()
{
    if (m_worldModelItems.empty()) {
        return;
    }

    const auto projection = BuildLegacyProjection();

    // Phase 1.7: bucket items by (pass, texture, additive) and merge their
    // vertex lists into one draw call per bucket. One draw call per (texture,
    // pass) group instead of one per item, which collapses the per-item
    // glBindTexture + glBufferData(orphan) + glBufferSubData round-trips
    // into a single round-trip per group.
    //
    //   pass:     0 = opaque, 1 = cutout, 2 = transparent
    //   texture:  GL texture handle
    //   additive: true for transparent items with GL_BLEND_FUNC(SRC_ALPHA, ONE)
    //
    // Within the transparent pass, items are appended in their original
    // dispatch order (which is back-to-front for transparent), so the
    // within-texture order is still back-to-front. Across textures the order
    // becomes texture-handle order, which can put a far item of texture A
    // before a near item of texture B -- the same tie-breaking the original
    // dispatch order had for items of equal distance, just made explicit.
    struct BucketKey {
        int pass;
        GLuint texture;
        bool additive;
        bool operator<(const BucketKey& o) const {
            if (pass != o.pass) return pass < o.pass;
            if (texture != o.texture) return texture < o.texture;
            // Within transparent, additive draws after alpha-blend so the
            // additive pass can layer on top.
            if (pass == 2) return !additive && o.additive;
            return false;
        }
    };

    std::map<BucketKey, std::vector<ModelVertex>> buckets;

    {
        GL_PERF_CPU_SCOPE("Legacy_BucketMerge");
        // Size each bucket before copying. Repeated insert growth otherwise
        // reallocates and recopies large expanded legacy meshes. Preserve
        // both key ordering and the original per-item append order.
        std::map<BucketKey, size_t> counts;
        for (const ModelDrawItem& item : m_worldModelItems) {
            if (!item.opaqueVertices.empty())
                counts[{0, item.texture, false}] += item.opaqueVertices.size();
            if (!item.cutoutVertices.empty())
                counts[{1, item.texture, false}] += item.cutoutVertices.size();
            if (!item.transparentVertices.empty())
                counts[{2, item.texture, item.additive}] += item.transparentVertices.size();
        }
        for (const auto& [key, count] : counts) {
            buckets[key].reserve(count);
        }
        for (const ModelDrawItem& item : m_worldModelItems) {
            if (!item.opaqueVertices.empty()) {
                auto& v = buckets[{0, item.texture, false}];
                v.insert(v.end(), item.opaqueVertices.begin(), item.opaqueVertices.end());
            }
            if (!item.cutoutVertices.empty()) {
                auto& v = buckets[{1, item.texture, false}];
                v.insert(v.end(), item.cutoutVertices.begin(), item.cutoutVertices.end());
            }
            if (!item.transparentVertices.empty()) {
                auto& v = buckets[{2, item.texture, item.additive}];
                v.insert(v.end(), item.transparentVertices.begin(), item.transparentVertices.end());
            }
        }
    }

    for (const auto& [key, verts] : buckets) {
        GL_PERF_CPU_SCOPE("Legacy_Submit");
        if (verts.empty()) {
            continue;
        }

        const bool enableBlend = (key.pass == 2);
        const bool additive     = (key.pass == 2) && key.additive;
        DrawModelVertices(key.texture, verts, projection, true, enableBlend, additive);
    }

    m_worldModelItems.clear();
    m_transparentModelItems.clear();
}

void GLRenderer::DrawModelVertices(GLuint texture,
                                   const std::vector<ModelVertex>& vertices,
                                   const std::array<float, 16>& projection,
                                   bool depthTest,
                                   bool enableBlend,
                                   bool additive,
                                   bool tintByFogColor)
{
    if (!m_modelShader.IsValid() || texture == 0 || vertices.empty()) {
        return;
    }

    UpdatePerFrameUBO(projection);
    m_modelShader.Use();
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    // Night lighting is applied to world-model fragments here. The sun/moon
    // draw sets this uniform back to zero, so it is never darkened by night.
    {
        static const GLint uNight = glGetUniformLocation(m_modelShader.GetProgramID(), "uNightStrength");
        if (uNight >= 0) {
            glUniform1f(uNight, (OptDayNight == 2 && !NightVisionOn) ? 1.0f : 0.0f);
        }
    }
    // uProjection is now in the PerFrame UBO (Phase 1.1). uTintByFogColor
    // stays a per-draw uniform; location cached at Initialize() (1.6).
    glUniform1f(m_locModelTint, tintByFogColor ? 1.0f : 0.0f);

    // §3.10: camera-in-fog global envelope (see GLRenderer::UpdateCameraFogEnvelope).
    {
        static const GLint uCamFogCol = glGetUniformLocation(m_modelShader.GetProgramID(), "uCamFogColor");
        static const GLint uCamFogAmt = glGetUniformLocation(m_modelShader.GetProgramID(), "uCamFogAmount");
        if (uCamFogCol >= 0 && uCamFogAmt >= 0 && g_GLRenderer) {
            const Vector3d c = g_GLRenderer->GetCamEnvelopeColor();
            glUniform3f(uCamFogCol, c.x, c.y, c.z);
            glUniform1f(uCamFogAmt, g_GLRenderer->GetCamEnvelopeAmount());
        }
    }

    if (depthTest) {
        glEnable(GL_DEPTH_TEST);
#ifdef GL_PERF_HOOKS
        GL_PERF_STATE_CHANGE();
#endif
        if (!enableBlend) {
            glDepthMask(GL_TRUE);
#ifdef GL_PERF_HOOKS
            GL_PERF_STATE_CHANGE();
#endif
        }
    } else {
        glDisable(GL_DEPTH_TEST);
#ifdef GL_PERF_HOOKS
        GL_PERF_STATE_CHANGE();
#endif
        glDepthMask(GL_FALSE);
#ifdef GL_PERF_HOOKS
        GL_PERF_STATE_CHANGE();
#endif
    }

    if (enableBlend) {
        glEnable(GL_BLEND);
#ifdef GL_PERF_HOOKS
        GL_PERF_STATE_CHANGE();
#endif
        if (additive) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive — used by water circles
        } else {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
#ifdef GL_PERF_HOOKS
        GL_PERF_STATE_CHANGE();
#endif
        glDepthMask(GL_FALSE);
#ifdef GL_PERF_HOOKS
        GL_PERF_STATE_CHANGE();
#endif
    } else {
        glDisable(GL_BLEND);
#ifdef GL_PERF_HOOKS
        GL_PERF_STATE_CHANGE();
#endif
    }

    glActiveTexture(GL_TEXTURE0);
    if (texture != m_lastBoundModelTexture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        m_lastBoundModelTexture = texture;
#ifdef GL_PERF_HOOKS
        GL_PERF_TEXTURE_BIND(texture);
#endif
    }
    glBindVertexArray(m_modelVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_modelVBO);
    const GLsizeiptr vertexSize = static_cast<GLsizeiptr>(vertices.size() * sizeof(ModelVertex));
    {
        GL_PERF_CPU_SCOPE("Legacy_Upload");
        glBufferData(GL_ARRAY_BUFFER, vertexSize, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, vertexSize, vertices.data());
    }
    {
        GL_PERF_CPU_SCOPE("Legacy_Draw");
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    }
#ifdef GL_PERF_HOOKS
    GL_PERF_DRAW(static_cast<uint32_t>(vertices.size()) / 3);
#endif
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    if (!depthTest) {
        glEnable(GL_DEPTH_TEST);
#ifdef GL_PERF_HOOKS
        GL_PERF_STATE_CHANGE();
#endif
    }
    if (enableBlend) {
        glDisable(GL_BLEND);
#ifdef GL_PERF_HOOKS
        GL_PERF_STATE_CHANGE();
#endif
    }
}

bool GLRenderer::BuildModelDrawItem(ModelDrawItem& outItem,
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
                                    bool additive) const
{
    GL_PERF_CPU_SCOPE("Model_BuildGeometry");
    if (!mptr || !mptr->lpTexture || !mptr->gVertex || !mptr->gFace) {
        return false;
    }

    const float ca = std::cos(al);
    const float sa = std::sin(al);
    const float cb = std::cos(bt);
    const float sb = std::sin(bt);
    // Phase 1.13: local vectors (were static thread_local). The
    // renderer is single-threaded; heap traffic at ~100 calls/frame
    // is trivial.
    std::vector<Vector3d> transformed;
    std::vector<Vector3d> unrotated;
    transformed.reserve(mptr->VCount);
    unrotated.reserve(mptr->VCount);

    // Fog volumes are defined in world space, while model vertices are now in
    // view space. Undo the camera pitch and yaw on each final rendered vertex
    // so fog sampling includes the model's rotation and current morphed pose.
    // The old centre+raw-vertex approximation could sample a large dinosaur
    // outside a fog cell even while its rendered geometry was inside it.
    auto unrotateCamera = [](const Vector3d& viewPosition) -> Vector3d {
        const float worldY = ::cb * viewPosition.y + ::sb * viewPosition.z;
        const float yawZ = ::cb * viewPosition.z - ::sb * viewPosition.y;
        return {
            ::ca * viewPosition.x - ::sa * yawZ,
            worldY,
            ::sa * viewPosition.x + ::ca * yawZ
        };
    };

    bool anyVisible = false;
    for (int i = 0; i < mptr->VCount; ++i) {
        // View-space position of the vertex, including model rotation and the
        // current animated/morphed vertex position.
        transformed.push_back(TransformModelVertex(mptr->gVertex[i], x0, y0, z0, ca, sa, cb, sb));
        unrotated.push_back(unrotateCamera(transformed.back()));

        if (transformed.back().z < kModelNearClip) {
            anyVisible = true;
        }
    }

    if (!anyVisible) {
        return false;
    }

    // Cache the shared pocket calculation once per unique morphed vertex,
    // rather than repeating it for every face that references that vertex.
    std::vector<FogSample> fogSamples;
    fogSamples.reserve(unrotated.size());
    {
        GL_PERF_CPU_SCOPE("Model_VertexFog");
        for (const Vector3d& point : unrotated) {
            fogSamples.push_back(SampleFogAtPointInline<false>(point, disableFog));
        }
    }

    outItem = ModelDrawItem();
    outItem.texture = 0;
    outItem.additive = additive;
    const Vector3d center = {x0, y0, z0};
    outItem.distance = VectorLengthSq(center);
    const size_t reserveCount = static_cast<size_t>(mptr->FCount) * 3;
    outItem.opaqueVertices.reserve(reserveCount);
    outItem.cutoutVertices.reserve(reserveCount);
    outItem.transparentVertices.reserve(reserveCount);

    const int lightIndex = std::clamp(vt, 0, 3);
    const float baseLight = static_cast<float>(light);
    const float baseAlpha = m_modelDistanceAlpha;
    const float transparentScale = clippedVariant ? (0x70 / 255.0f) : (0x80 / 255.0f);
    const bool forceDistanceBlend = baseAlpha < 0.999f;

    auto appendTriangle = [&](const ModelClipVertex& a,
                              const ModelClipVertex& b,
                              const ModelClipVertex& c,
                              const FogSample& fogA,
                              const FogSample& fogB,
                              const FogSample& fogC,
                              bool transparent,
                              bool cutout) {
        // Reuse the original face vertices' fog samples, preserving the
        // existing clipping behaviour (fog is not re-sampled at clip cuts).
        const float alpha = transparent ? baseAlpha * transparentScale : baseAlpha;
        const float cutoutValue = cutout ? 1.0f : 0.0f;
        const bool blended = transparent || forceDistanceBlend || additive;
        std::vector<ModelVertex>& target = blended ? outItem.transparentVertices : (cutout ? outItem.cutoutVertices : outItem.opaqueVertices);
        // Phase 1.4: pack light/fog/alpha/cutout as uint8 (driver normalizes
        // them back to [0,1] in the vertex shader) and the per-vertex fog
        // color as a vec3 of uint8. The float->uint8 conversion is the only
        // CPU cost of the new layout; it's a single clamp+multiply+cast.
        // Each of a, b, c gets its own light byte -- using a single
        // lightByte for all three causes flat shading (every triangle
        // would inherit vertex a's lighting).
        const uint8_t lightAByte = Light255ToByte(a.light);
        const uint8_t lightBByte = Light255ToByte(b.light);
        const uint8_t lightCByte = Light255ToByte(c.light);
        const uint8_t fogAByte   = Float01ToByte(fogA.amount);
        const uint8_t fogAR      = Float01ToByte(fogA.color.x);
        const uint8_t fogAG      = Float01ToByte(fogA.color.y);
        const uint8_t fogAB      = Float01ToByte(fogA.color.z);
        const uint8_t alphaByte  = Float01ToByte(alpha);
        const uint8_t cutoutByte = CutoutToByte(cutout);
        const uint8_t fogBByte   = Float01ToByte(fogB.amount);
        const uint8_t fogBR      = Float01ToByte(fogB.color.x);
        const uint8_t fogBG      = Float01ToByte(fogB.color.y);
        const uint8_t fogBB      = Float01ToByte(fogB.color.z);
        const uint8_t fogCByte   = Float01ToByte(fogC.amount);
        const uint8_t fogCR      = Float01ToByte(fogC.color.x);
        const uint8_t fogCG      = Float01ToByte(fogC.color.y);
        const uint8_t fogCB      = Float01ToByte(fogC.color.z);
        target.push_back({a.position.x, a.position.y, a.position.z, a.uv.x, a.uv.y, lightAByte, fogAByte, alphaByte, cutoutByte, fogAR, fogAG, fogAB, {0,0,0,0,0}});
        target.push_back({b.position.x, b.position.y, b.position.z, b.uv.x, b.uv.y, lightBByte, fogBByte, alphaByte, cutoutByte, fogBR, fogBG, fogBB, {0,0,0,0,0}});
        target.push_back({c.position.x, c.position.y, c.position.z, c.uv.x, c.uv.y, lightCByte, fogCByte, alphaByte, cutoutByte, fogCR, fogCG, fogCB, {0,0,0,0,0}});
    };
    // Phase 1.13: local vector (was static thread_local).
    std::vector<ModelClipVertex> polygon;
    polygon.reserve(4);
    polygon.clear();

    for (int f = 0; f < mptr->FCount; ++f) {
        const TFace& face = mptr->gFace[f];
        const Vector3d& p0 = transformed[face.v1];
        const Vector3d& p1 = transformed[face.v2];
        const Vector3d& p2 = transformed[face.v3];

        if (ShouldCullModelFace(face.Flags, p0, p1, p2)) {
            continue;
        }

        const float l0 = std::clamp(baseLight + mptr->VLight[lightIndex][face.v1], 0.0f, 255.0f);
        const float l1 = std::clamp(baseLight + mptr->VLight[lightIndex][face.v2], 0.0f, 255.0f);
        const float l2 = std::clamp(baseLight + mptr->VLight[lightIndex][face.v3], 0.0f, 255.0f);

        const int texHeight = (mptr->TextureHeight > 1) ? mptr->TextureHeight : 1;
        // fp_conv() in CorrectModel already converted int UVs to float pixel coords.
        // ModelClipVertex carries only position/uv/light. Fog is sampled
        // at the original face positions, not interpolated by the clipper.
        ModelClipVertex v0{p0, DecodeLegacyFaceUV(face.tax, face.tay, texHeight), l0};
        ModelClipVertex v1{p1, DecodeLegacyFaceUV(face.tbx, face.tby, texHeight), l1};
        ModelClipVertex v2{p2, DecodeLegacyFaceUV(face.tcx, face.tcy, texHeight), l2};

        if (waterClipped) {
            ClipTriangleAgainstWater(v0, v1, v2, polygon);
            if (polygon.size() < 3) {
                continue;
            }
        } else {
            polygon.clear();
            polygon.reserve(3);
            polygon.push_back(v0);
            polygon.push_back(v1);
            polygon.push_back(v2);
        }

        const bool isFaceTransparent = (face.Flags & sfTransparent) != 0;
        const bool isFaceAlphaTest = (face.Flags & sfOpacity) != 0;
        const bool cutout = isFaceAlphaTest;
        for (size_t i = 1; i + 1 < polygon.size(); ++i) {
            appendTriangle(polygon[0], polygon[i], polygon[i + 1],
                           fogSamples[face.v1], fogSamples[face.v2], fogSamples[face.v3],
                           isFaceTransparent, cutout);
        }
    }

    return !outItem.opaqueVertices.empty() || !outItem.cutoutVertices.empty() || !outItem.transparentVertices.empty();
}

bool GLRenderer::BuildModelEffectVertices(std::vector<ModelVertex>& outVertices,
                                          TModel* mptr,
                                          float x0,
                                          float y0,
                                          float z0,
                                          float al,
                                          float bt,
                                          int flagMask,
                                          const Vector3d& fogColor) const
{
    if (!mptr || !mptr->gVertex || !mptr->gFace || !PhongMapping) {
        return false;
    }

    const float ca = std::cos(al);
    const float sa = std::sin(al);
    const float cb = std::cos(bt);
    const float sb = std::sin(bt);
    // Phase 1.13: local vector (was static thread_local). The renderer
    // is single-threaded so the static thread_local was misleading; the
    // heap traffic at ~100 calls/frame is trivial.
    std::vector<Vector3d> transformed;
    transformed.reserve(mptr->VCount);

    bool anyVisible = false;
    for (int i = 0; i < mptr->VCount; ++i) {
        const Vector3d position = TransformModelVertex(mptr->gVertex[i], x0, y0, z0, ca, sa, cb, sb);
        transformed.push_back(position);
        if (position.z < kModelNearClip) {
            anyVisible = true;
        }
    }

    if (!anyVisible) {
        return false;
    }

    outVertices.clear();
    outVertices.reserve(static_cast<size_t>(mptr->FCount) * 3);

    for (int i = 0; i < mptr->FCount; ++i) {
        const TFace& face = mptr->gFace[i];
        if (!(face.Flags & flagMask)) {
            continue;
        }

        if (ShouldCullModelFace(face.Flags, transformed[face.v1], transformed[face.v2], transformed[face.v3])) {
            continue;
        }

        const ModelClipVertex v0 = {
            transformed[face.v1],
            { PhongMapping[face.v1].x / 256.0f, PhongMapping[face.v1].y / 256.0f },
            255
        };
        const ModelClipVertex v1 = {
            transformed[face.v2],
            { PhongMapping[face.v2].x / 256.0f, PhongMapping[face.v2].y / 256.0f },
            255
        };
        const ModelClipVertex v2 = {
            transformed[face.v3],
            { PhongMapping[face.v3].x / 256.0f, PhongMapping[face.v3].y / 256.0f },
            255
        };

        // The overlay must feed the GPU bit-identical triangles to the base
        // pass (which relies on GPU clipping for near models): the overlay
        // used to CPU-clip here while the base did not, so the two paths
        // rounded to ~ulp-different depths and fought per-pixel at grazing
        // view angles (striped shimmer on barrels when sighting down them).
        // The anyVisible gate above still skips fully-hidden models; the GPU
        // clips both passes at the same projection near.
        std::vector<ModelClipVertex> clipped;
        clipped.reserve(3);
        clipped.push_back(v0);
        clipped.push_back(v1);
        clipped.push_back(v2);
        for (size_t j = 1; j + 1 < clipped.size(); ++j) {
            // Phase 1.4: pack the float fields (light, fog, alpha, cutout)
            // and the fog color vec3 to uint8. The water surface always
            // uses light=255, fog.amount=0, alpha=1, cutout=0, so the
            // packed bytes are constant and can be computed once.
            const uint8_t lightByte  = 255;
            const uint8_t fog0Byte   = 0;
            const uint8_t alphaByte  = 255;
            const uint8_t cutoutByte = 0;
            const uint8_t fogRByte   = Float01ToByte(fogColor.x);
            const uint8_t fogGByte   = Float01ToByte(fogColor.y);
            const uint8_t fogBByte   = Float01ToByte(fogColor.z);
            const uint8_t pad[5] = {0, 0, 0, 0, 0};
            const ModelVertex out0 = {
                clipped[0].position.x,
                clipped[0].position.y,
                clipped[0].position.z,
                clipped[0].uv.x,
                clipped[0].uv.y,
                lightByte, fog0Byte, alphaByte, cutoutByte,
                fogRByte, fogGByte, fogBByte,
                {0, 0, 0, 0, 0}
            };
            const ModelVertex out1 = {
                clipped[j].position.x,
                clipped[j].position.y,
                clipped[j].position.z,
                clipped[j].uv.x,
                clipped[j].uv.y,
                lightByte, fog0Byte, alphaByte, cutoutByte,
                fogRByte, fogGByte, fogBByte,
                {0, 0, 0, 0, 0}
            };
            const ModelVertex out2 = {
                clipped[j + 1].position.x,
                clipped[j + 1].position.y,
                clipped[j + 1].position.z,
                clipped[j + 1].uv.x,
                clipped[j + 1].uv.y,
                lightByte, fog0Byte, alphaByte, cutoutByte,
                fogRByte, fogGByte, fogBByte,
                {0, 0, 0, 0, 0}
            };
            outVertices.push_back(out0);
            outVertices.push_back(out1);
            outVertices.push_back(out2);
        }
    }

    return !outVertices.empty();
}

GLuint GLRenderer::UploadPictureTexture(const TPicture& pic)
{
    if (!pic.lpImage || pic.W <= 0 || pic.H <= 0) {
        return 0;
    }

    const size_t texelCount = static_cast<size_t>(pic.W) * static_cast<size_t>(pic.H);
    std::vector<uint8_t> rgba(texelCount * 4);
    for (size_t i = 0; i < texelCount; ++i) {
        const unsigned short c = pic.lpImage[i];
        rgba[i * 4 + 0] = static_cast<uint8_t>(((c >> 10) & 0x1F) * 255 / 31);
        rgba[i * 4 + 1] = static_cast<uint8_t>(((c >> 5) & 0x1F) * 255 / 31);
        rgba[i * 4 + 2] = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
        rgba[i * 4 + 3] = 255;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, pic.W, pic.H, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    return texture;
}

GLuint GLRenderer::UploadBMPModelTexture(TBMPModel* mptr)
{
    if (!mptr || !mptr->lpTexture) {
        return 0;
    }

    const auto cached = m_bmpTextureCache.find(mptr);
    if (cached != m_bmpTextureCache.end()) {
        return cached->second;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    std::vector<uint32_t> expanded(128 * 128);
    for (size_t i = 0; i < expanded.size(); ++i) {
        expanded[i] = Expand1555to8888(mptr->lpTexture[i]);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, expanded.data());
    m_bmpTextureCache[mptr] = texture;
    return texture;
}

GLuint GLRenderer::UploadModelTexture(TModel* mptr)
{
    if (!mptr || !mptr->lpTexture) {
        return 0;
    }

    const auto cached = m_modelTextureCache.find(mptr);
    if (cached != m_modelTextureCache.end()) {
        return cached->second;
    }

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    const int height = mptr->TextureHeight > 1 ? mptr->TextureHeight : 256;
    const int width = 256;
    const size_t availableTexels = static_cast<size_t>(mptr->TextureSize) / sizeof(std::uint16_t);
    const size_t texelCount = static_cast<size_t>(width) * height;
    if (availableTexels < texelCount) {
        return 0;
    }

    std::vector<uint32_t> expanded(texelCount);
    for (size_t i = 0; i < texelCount; ++i) {
        expanded[i] = Expand1555to8888(mptr->lpTexture[i]);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, expanded.data());
    m_modelTextureCache[mptr] = texture;
    return texture;
}

const GLRenderer::StaticMeshEntry* GLRenderer::GetStaticMeshEntry(const TModel* mptr) const
{
    if (!mptr) {
        return nullptr;
    }
    auto it = m_staticMeshCache.find(mptr);
    if (it == m_staticMeshCache.end()) {
        return nullptr;
    }
    return &it->second;
}

void GLRenderer::BuildStaticMeshVertices(std::vector<StaticMeshVertex>& vertices,
                                         const TModel* mptr) const
{
    vertices.clear();
    if (!mptr || !mptr->gVertex || !mptr->gFace) {
        return;
    }

    vertices.reserve(static_cast<size_t>(mptr->FCount) * 3);
    const int texHeight = (mptr->TextureHeight > 1) ? mptr->TextureHeight : 1;

    auto appendVertex = [&](int vertexIndex, const Vector2df& uv,
                            float nx, float ny, float nz, float cutout) {
        const TPoint3d& p = mptr->gVertex[vertexIndex];
        StaticMeshVertex vertex{};
        vertex.x = p.x;
        vertex.y = p.y;
        vertex.z = p.z;
        vertex.nx = nx;
        vertex.ny = ny;
        vertex.nz = nz;
        vertex.u = uv.x;
        vertex.v = uv.y;
        for (int orientation = 0; orientation < 4; ++orientation) {
            vertex.light[orientation] = mptr->VLight[orientation]
                ? mptr->VLight[orientation][vertexIndex]
                : 0.0f;
        }
        vertex.cutout = cutout;
        vertices.push_back(vertex);
    };

    for (int f = 0; f < mptr->FCount; ++f) {
        const TFace& face = mptr->gFace[f];
        const TPoint3d& p0 = mptr->gVertex[face.v1];
        const TPoint3d& p1 = mptr->gVertex[face.v2];
        const TPoint3d& p2 = mptr->gVertex[face.v3];

        const float e1x = p1.x - p0.x;
        const float e1y = p1.y - p0.y;
        const float e1z = p1.z - p0.z;
        const float e2x = p2.x - p0.x;
        const float e2y = p2.y - p0.y;
        const float e2z = p2.z - p0.z;
        const float nx = e1y * e2z - e1z * e2y;
        const float ny = e1z * e2x - e1x * e2z;
        const float nz = e1x * e2y - e1y * e2x;

        const float cutout = (face.Flags & sfOpacity) != 0 ? 1.0f : 0.0f;
        appendVertex(face.v1, DecodeLegacyFaceUV(face.tax, face.tay, texHeight), nx, ny, nz, cutout);
        appendVertex(face.v2, DecodeLegacyFaceUV(face.tbx, face.tby, texHeight), nx, ny, nz, cutout);
        appendVertex(face.v3, DecodeLegacyFaceUV(face.tcx, face.tcy, texHeight), nx, ny, nz, cutout);
    }
}

void GLRenderer::UpdateAnimatedStaticMesh(TModel* mptr)
{
    auto it = m_staticMeshCache.find(mptr);
    if (it == m_staticMeshCache.end() || it->second.lastVertexUploadTime == RealTime) {
        return;
    }

    BuildStaticMeshVertices(m_animatedMeshScratch, mptr);
    if (m_animatedMeshScratch.size() != it->second.vertexCount) {
        return;
    }

    glBindBuffer(GL_ARRAY_BUFFER, m_staticMeshVBO);
    glBufferSubData(GL_ARRAY_BUFFER,
                    static_cast<GLintptr>(it->second.baseVertex) * sizeof(StaticMeshVertex),
                    static_cast<GLsizeiptr>(m_animatedMeshScratch.size() * sizeof(StaticMeshVertex)),
                    m_animatedMeshScratch.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    it->second.lastVertexUploadTime = RealTime;
}

bool GLRenderer::PopulateExactShadeInstance(ModelInstance& instance,
                                             const StaticMeshEntry& mesh,
                                             TModel* model, const Vector3d& pos, float fi)
{
    GL_PERF_CPU_SCOPE("Models_ExactShade");
    // No blending, animation or clipping is introduced into the opaque pass.
    // Float instance indices remain exact; buffer limits are measured in texels.
    const size_t count = static_cast<size_t>(model->FCount) * 3;
    if (m_modelDistanceAlpha < 0.999f || !m_exactShadeTexture ||
        mesh.vertexCount != count || m_exactShadeLimit <= 0 ||
        m_exactShades.size() + count > static_cast<size_t>(m_exactShadeLimit) / 2 ||
        m_exactShades.size() + count > 8000000 || mesh.baseVertex > 8000000)
        return false;

    const float ca = std::cos(fi), sa = std::sin(fi);
    const float cb = std::cos(CameraBeta), sb = std::sin(CameraBeta);
    auto& positions = m_exactPositionScratch;
    auto& shades = m_exactVertexScratch;
    positions.clear();
    shades.clear();
    positions.reserve(model->VCount);
    shades.reserve(model->VCount);
    bool anyVisible = false;
    {
    GL_PERF_CPU_SCOPE("Exact_Transform");
    for (int i = 0; i < model->VCount; ++i) {
        const auto p = TransformModelVertex(model->gVertex[i], pos.x, pos.y, pos.z, ca, sa, cb, sb);
        positions.push_back(p);
        anyVisible |= p.z < kModelNearClip;
    }
    }
    {
    GL_PERF_CPU_SCOPE("Exact_FogPack");
    for (int i = 0; i < model->VCount; ++i) {
        const auto& p = positions[i];
        const float worldY = ::cb * p.y + ::sb * p.z;
        const float yawZ = ::cb * p.z - ::sb * p.y;
        const Vector3d world = {::ca * p.x - ::sa * yawZ, worldY, ::sa * p.x + ::ca * yawZ};
        const FogSample fog = SampleFogAtPointInline<false>(world, false);
        shades.push_back({Light255ToByte(std::clamp(128.0f + model->VLight[0][i], 0.0f, 255.0f)),
                          Float01ToByte(fog.amount), Float01ToByte(fog.color.x),
                          Float01ToByte(fog.color.y), Float01ToByte(fog.color.z), 255, 0, 0});
    }
    }
    instance.groundParams[0] = 2.0f;
    instance.groundParams[1] = static_cast<float>(m_exactShades.size());
    instance.groundParams[2] = static_cast<float>(mesh.baseVertex);
    instance.instanceFlags[3] = Float01ToByte(m_modelDistanceAlpha) / 255.0f;
    {
    GL_PERF_CPU_SCOPE("Exact_ExpandCull");
    for (int f = 0; f < model->FCount; ++f) {
        const auto& face = model->gFace[f];
        const bool visible = anyVisible && !ShouldCullModelFace(face.Flags,
            positions[face.v1], positions[face.v2], positions[face.v3]);
        for (int index : {face.v1, face.v2, face.v3}) {
            auto shade = shades[index];
            shade.visible = visible ? 255 : 0;
            m_exactShades.push_back(shade);
        }
    }
    }
#ifdef GL_PERF_HOOKS
    // Validate packed bytes and face selection against the retained legacy
    // builder once per model/orientation, during warm-up rather than capture.
    const unsigned orientationBit = 1u << static_cast<unsigned>(instance.instanceFlags[0]);
    if (!(m_exactValidated[model] & orientationBit)) {
        ModelDrawItem reference;
        BuildModelDrawItem(reference, model, pos.x, pos.y, pos.z, 128, 0,
                           fi, CameraBeta, false, false, true, false);
        size_t opaque = 0, cutout = 0;
        bool equal = reference.transparentVertices.empty();
        const size_t start = static_cast<size_t>(instance.groundParams[1]);
        for (int f = 0; f < model->FCount; ++f) {
            const bool isCutout = (model->gFace[f].Flags & sfOpacity) != 0;
            const auto& vertices = isCutout ? reference.cutoutVertices : reference.opaqueVertices;
            size_t& cursor = isCutout ? cutout : opaque;
            for (int v = 0; v < 3; ++v) {
                const auto& shade = m_exactShades[start + f * 3 + v];
                if (!shade.visible) continue;
                if (cursor >= vertices.size()) { equal = false; continue; }
                const auto& expected = vertices[cursor++];
                equal &= shade.light == expected.light && shade.fog == expected.fog &&
                         shade.r == expected.fogR && shade.g == expected.fogG &&
                         shade.b == expected.fogB && expected.alpha == Float01ToByte(m_modelDistanceAlpha);
            }
        }
        equal &= opaque == reference.opaqueVertices.size() && cutout == reference.cutoutVertices.size();
        if (!equal) {
            LOG_ERROR("ExactShade legacy parity FAILED");
            m_exactShades.resize(start);
            return false;
        }
        m_exactValidated[model] |= orientationBit;
        LOG_INFO("ExactShade legacy parity passed: vertices=%d faces=%d orientation=%d",
                 model->VCount, model->FCount, static_cast<int>(instance.instanceFlags[0]));
    }
#endif
    return true;
}

bool GLRenderer::PopulateGroundLightInstance(ModelInstance& instance,
                                             const StaticMeshEntry& mesh,
                                             int worldCenterX,
                                             int worldCenterZ,
                                             int orientation) const
{
    float minX = mesh.minX;
    float maxX = mesh.maxX;
    float minZ = mesh.minZ;
    float maxZ = mesh.maxZ;
    switch (orientation & 3) {
    case 1:
        minX = mesh.minZ;  maxX = mesh.maxZ;
        minZ = -mesh.maxX; maxZ = -mesh.minX;
        break;
    case 2:
        minX = -mesh.maxX; maxX = -mesh.minX;
        minZ = -mesh.maxZ; maxZ = -mesh.minZ;
        break;
    case 3:
        minX = -mesh.maxZ; maxX = -mesh.minZ;
        minZ = mesh.minX;  maxZ = mesh.maxX;
        break;
    default:
        break;
    }

    const float worldMinX = static_cast<float>(worldCenterX) + minX;
    const float worldMaxX = static_cast<float>(worldCenterX) + maxX;
    const float worldMinZ = static_cast<float>(worldCenterZ) + minZ;
    const float worldMaxZ = static_cast<float>(worldCenterZ) + maxZ;
    if (worldMinX < 0.0f || worldMinZ < 0.0f) {
        return false;
    }

    // A 4x4 sample grid exactly covers a footprint spanning at most three
    // 512-unit interpolation cells on each axis. Still-larger models retain
    // the legacy CPU path rather than approximating their lighting.
    const int firstCellX = static_cast<int>(worldMinX) / 512;
    const int firstCellZ = static_cast<int>(worldMinZ) / 512;
    const int lastCellX = static_cast<int>(worldMaxX) / 512;
    const int lastCellZ = static_cast<int>(worldMaxZ) / 512;
    if (lastCellX - firstCellX > 2 || lastCellZ - firstCellZ > 2) {
        return false;
    }

    const int gridX = firstCellX * 2 - CCX + kViewGridCenter;
    const int gridZ = firstCellZ * 2 - CCY + kViewGridCenter;
    if (gridX < 0 || gridZ < 0 || gridX + 6 >= kViewGridSize || gridZ + 6 >= kViewGridSize) {
        return false;
    }

    const auto normalizedLight = [](int light) {
        return static_cast<float>(std::clamp(light, 0, 255)) / 255.0f;
    };
    for (int sampleZ = 0; sampleZ < 4; ++sampleZ) {
        for (int sampleX = 0; sampleX < 4; ++sampleX) {
            instance.groundLight[sampleZ * 4 + sampleX] =
                normalizedLight(VMap[gridZ + sampleZ * 2][gridX + sampleX * 2].Light);
        }
    }
    instance.groundParams[0] = 1.0f;
    instance.groundParams[1] = static_cast<float>(worldCenterX - firstCellX * 512);
    instance.groundParams[2] = static_cast<float>(worldCenterZ - firstCellZ * 512);
    instance.groundParams[3] = 0.0f;
    return true;
}

GLRenderer::StaticMeshEntry GLRenderer::UploadStaticMesh(TModel* mptr)
{
    if (!mptr || !mptr->gVertex || !mptr->gFace) {
        return {};
    }

    // Cache hit: return existing entry.
    auto it = m_staticMeshCache.find(mptr);
    if (it != m_staticMeshCache.end()) {
        return it->second;
    }

    // Compute upload size: triangle list, 3 vertices + 3 indices per face.
    const size_t faceCount = static_cast<size_t>(mptr->FCount);
    const size_t vertexCount = faceCount * 3;
    const size_t indexCount = faceCount * 3;
    const size_t vertexBytes = vertexCount * sizeof(StaticMeshVertex);
    const size_t indexBytes = indexCount * sizeof(uint32_t);

    // Grow buffers if needed. This may clear the cache if growth happens.
    EnsureStaticMeshCapacity(vertexBytes, indexBytes);

    // Re-check cache after a possible growth-induced clear.
    it = m_staticMeshCache.find(mptr);
    if (it != m_staticMeshCache.end()) {
        return it->second;
    }

    // Build the upload data on the CPU. Faces are expanded to a triangle
    // list because each corner has face-specific UVs, while the four VLight
    // values retain the original model-vertex Gouraud semantics.
    std::vector<StaticMeshVertex> vertices;
    vertices.reserve(vertexCount);
    BuildStaticMeshVertices(vertices, mptr);
    std::vector<uint32_t> indices;
    indices.reserve(indexCount);

    // sfOpacity is encoded per expanded face vertex above. sfTransparent
    // remains a model-level fallback because blended faces require a sorted
    // pass which the current instanced opaque path does not provide.
    bool hasTransparent = false;
    for (int f = 0; f < mptr->FCount; ++f) {
        const TFace& face = mptr->gFace[f];
        hasTransparent = hasTransparent || (face.Flags & sfTransparent) != 0;

        const uint32_t baseIdx = m_staticMeshNextVertexOffset + static_cast<uint32_t>(f * 3);
        indices.push_back(baseIdx);
        indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 2);
    }

    // Upload to the static VBO.
    const uint32_t vboOffset = m_staticMeshNextVertexOffset;
    glBindBuffer(GL_ARRAY_BUFFER, m_staticMeshVBO);
    glBufferSubData(GL_ARRAY_BUFFER, vboOffset * sizeof(StaticMeshVertex), vertexBytes, vertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Upload to the static IBO.
    const uint32_t iboOffset = m_staticMeshNextIndexOffset;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_staticMeshIBO);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, iboOffset * sizeof(uint32_t), indexBytes, indices.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // Cache the entry.
    StaticMeshEntry entry;
    entry.baseVertex = vboOffset;
    entry.baseIndex = iboOffset;
    entry.vertexCount = static_cast<uint32_t>(vertexCount);
    entry.indexCount = static_cast<uint32_t>(indexCount);
    entry.hasTransparent = hasTransparent;
    entry.lastVertexUploadTime = RealTime;
    entry.minX = entry.minZ = std::numeric_limits<float>::max();
    entry.maxX = entry.maxZ = std::numeric_limits<float>::lowest();
    for (int v = 0; v < mptr->VCount; ++v) {
        entry.minX = (std::min)(entry.minX, mptr->gVertex[v].x);
        entry.maxX = (std::max)(entry.maxX, mptr->gVertex[v].x);
        entry.minZ = (std::min)(entry.minZ, mptr->gVertex[v].z);
        entry.maxZ = (std::max)(entry.maxZ, mptr->gVertex[v].z);
    }
    m_staticMeshCache[mptr] = entry;

    // Advance the next-offset cursors.
    m_staticMeshNextVertexOffset += static_cast<uint32_t>(vertexCount);
    m_staticMeshNextIndexOffset += static_cast<uint32_t>(indexCount);

    return entry;
}

void GLRenderer::EnsureStaticMeshCapacity(size_t vertexBytes, size_t indexBytes)
{
    // Account for the models already concatenated into each global buffer.
    // This matters more now that VLight expands each static vertex from 32 to
    // 52 bytes; comparing only the incoming model size could write past the
    // allocation once several individually-small models filled the buffer.
    const size_t requiredVertexBytes =
        static_cast<size_t>(m_staticMeshNextVertexOffset) * sizeof(StaticMeshVertex) + vertexBytes;
    const size_t requiredIndexBytes =
        static_cast<size_t>(m_staticMeshNextIndexOffset) * sizeof(uint32_t) + indexBytes;
    const bool needGrowVBO = requiredVertexBytes > m_staticMeshVBOCapacity;
    const bool needGrowIBO = requiredIndexBytes > m_staticMeshIBOCapacity;

    if (!needGrowVBO && !needGrowIBO) {
        return;
    }

    if (needGrowVBO) {
        size_t newCapacity = m_staticMeshVBOCapacity;
        while (newCapacity < requiredVertexBytes) newCapacity *= 2;
        m_staticMeshVBOCapacity = newCapacity;
        glBindBuffer(GL_ARRAY_BUFFER, m_staticMeshVBO);
        glBufferData(GL_ARRAY_BUFFER, m_staticMeshVBOCapacity, nullptr, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (needGrowIBO) {
        size_t newCapacity = m_staticMeshIBOCapacity;
        while (newCapacity < requiredIndexBytes) newCapacity *= 2;
        m_staticMeshIBOCapacity = newCapacity;
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_staticMeshIBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_staticMeshIBOCapacity, nullptr, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    // Both VBO and IBO data was orphaned; clear the cache so models
    // get re-uploaded on next access. The next-offset cursors also
    // reset to 0 so the re-upload starts from the new buffer start.
    m_staticMeshCache.clear();
    m_staticMeshNextVertexOffset = 0;
    m_staticMeshNextIndexOffset = 0;
}

void GLRenderer::ShutdownStaticMeshPipeline()
{
    if (m_staticMeshVBO) {
        glDeleteBuffers(1, &m_staticMeshVBO);
        m_staticMeshVBO = 0;
    }
    if (m_staticMeshIBO) {
        glDeleteBuffers(1, &m_staticMeshIBO);
        m_staticMeshIBO = 0;
    }
    m_staticMeshVBOCapacity = 0;
    m_staticMeshIBOCapacity = 0;
    m_staticMeshNextVertexOffset = 0;
    m_staticMeshNextIndexOffset = 0;
    m_staticMeshCache.clear();
    m_animatedMeshScratch.clear();
    m_animatedMeshScratch.shrink_to_fit();
}

bool GLRenderer::InitializeStaticMeshPipeline()
{
    // Phase 2.2: allocate the static VBO/IBO pair that holds every
    // unique TModel*'s geometry. Both buffers remain GL_STATIC_DRAW because
    // almost all ranges are immutable; the few animated scenery ranges are
    // updated explicitly with glBufferSubData once per model per frame.
    // Initial capacities cover a
    // typical custom map; growth is handled in EnsureStaticMeshCapacity.

    glGenBuffers(1, &m_staticMeshVBO);
    glGenBuffers(1, &m_staticMeshIBO);

    m_staticMeshVBOCapacity = kInitialStaticMeshVBOCapacity;
    m_staticMeshIBOCapacity = kInitialStaticMeshIBOCapacity;

    glBindBuffer(GL_ARRAY_BUFFER, m_staticMeshVBO);
    glBufferData(GL_ARRAY_BUFFER, m_staticMeshVBOCapacity, nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_staticMeshIBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_staticMeshIBOCapacity, nullptr, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    m_staticMeshNextVertexOffset = 0;
    m_staticMeshNextIndexOffset = 0;
    m_staticMeshCache.clear();

    return true;
}

void GLRenderer::ShutdownInstancingPipeline()
{
    if (m_exactShadeTexture) glDeleteTextures(1, &m_exactShadeTexture);
    if (m_exactShadeBuffer) glDeleteBuffers(1, &m_exactShadeBuffer);
    m_exactShadeTexture = m_exactShadeBuffer = 0;
    m_exactShadeLimit = 0;
    m_exactShades.clear();
    m_exactShades.shrink_to_fit();
    m_exactPositionScratch.clear();
    m_exactPositionScratch.shrink_to_fit();
    m_exactVertexScratch.clear();
    m_exactVertexScratch.shrink_to_fit();
#ifdef GL_PERF_HOOKS
    m_exactValidated.clear();
#endif
    if (m_instanceVBO) {
        glDeleteBuffers(1, &m_instanceVBO);
        m_instanceVBO = 0;
    }
    if (m_instanceVAO) {
        glDeleteVertexArrays(1, &m_instanceVAO);
        m_instanceVAO = 0;
    }
    m_instanceData.clear();
    m_instanceData.shrink_to_fit();
    m_instanceInfo.clear();
    m_instanceInfo.shrink_to_fit();
    m_instanceSortScratch.clear();
    m_instanceSortScratch.shrink_to_fit();
}

bool GLRenderer::InitializeInstancingPipeline()
{
    glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &m_exactShadeLimit);
    glGenBuffers(1, &m_exactShadeBuffer);
    glBindBuffer(GL_TEXTURE_BUFFER, m_exactShadeBuffer);
    glBufferData(GL_TEXTURE_BUFFER, sizeof(ExactShade), nullptr, GL_STREAM_DRAW);
    glGenTextures(1, &m_exactShadeTexture);
    glBindTexture(GL_TEXTURE_BUFFER, m_exactShadeTexture);
    glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA8, m_exactShadeBuffer);
    glBindTexture(GL_TEXTURE_BUFFER, 0);
    glBindBuffer(GL_TEXTURE_BUFFER, 0);
    // Phase 2.1 + 2.3: allocate and configure the instance VBO and VAO.
    //
    // The instance VAO combines:
    //   - Static mesh VBO (per-vertex: position, normal, UV, VLight[0..3])
    //   - Instance VBO (per-instance: world matrix, light, flags, ground light)
    //
    // Per-vertex attributes (from static mesh VBO):
    //   attribute 0: vec3 aPos       (offset  0, 12 bytes)
    //   attribute 1: vec3 aNormal    (offset 12, 12 bytes)
    //   attribute 2: vec2 aTexCoord  (offset 24,  8 bytes)
    //   attribute 3: vec4 aVertexLight (offset 32, 16 bytes)
    //   attribute 15: float aCutout      (offset 48,  4 bytes)
    //
    // Per-instance attributes (from instance VBO, divisor=1):
    //   attribute 4: vec4 aWorldRow0      (offset  0)
    //   attribute 5: vec4 aWorldRow1      (offset 16)
    //   attribute 6: vec4 aWorldRow2      (offset 32)
    //   attribute 7: vec4 aWorldRow3      (offset 48)
    //   attribute 8: vec4 aInstanceLight  (offset 64)
    //   attribute 9: vec4 aInstanceFlags  (offset 80)
    //   attributes 10-13: vec4 aGroundLightRow[0..3] (offset 96)
    //   attribute 14: vec4 aGroundParams  (offset 160)

    glGenBuffers(1, &m_instanceVBO);
    glGenVertexArrays(1, &m_instanceVAO);

    glBindVertexArray(m_instanceVAO);

    // Per-vertex attributes from static mesh VBO.
    // Bind the static mesh VBO and set up per-vertex attributes.
    glBindBuffer(GL_ARRAY_BUFFER, m_staticMeshVBO);

    glEnableVertexAttribArray(0); // aPos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertex),
                          reinterpret_cast<void*>(offsetof(StaticMeshVertex, x)));

    glEnableVertexAttribArray(1); // aNormal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertex),
                          reinterpret_cast<void*>(offsetof(StaticMeshVertex, nx)));

    glEnableVertexAttribArray(2); // aTexCoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertex),
                          reinterpret_cast<void*>(offsetof(StaticMeshVertex, u)));

    glEnableVertexAttribArray(3); // aVertexLight
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertex),
                          reinterpret_cast<void*>(offsetof(StaticMeshVertex, light)));

    glEnableVertexAttribArray(15); // aCutout (per expanded face vertex)
    glVertexAttribPointer(15, 1, GL_FLOAT, GL_FALSE, sizeof(StaticMeshVertex),
                          reinterpret_cast<void*>(offsetof(StaticMeshVertex, cutout)));

    // Per-instance attributes from instance VBO.
    // Bind the instance VBO and set up per-instance attributes with divisor=1.
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);

    // Each instance attribute is a vec4 (4 floats).
    // The attributes are at locations 4-14 and are contiguous vec4 arrays.
    for (GLuint loc = 4; loc <= 14; ++loc) {
        glEnableVertexAttribArray(loc);
        glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(ModelInstance),
                              reinterpret_cast<void*>(
                                  static_cast<uintptr_t>((loc - 4) * 4 * sizeof(float))));
        glVertexAttribDivisor(loc, 1); // advance once per instance
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_instanceData.clear();
    m_instanceData.reserve(kInitialInstanceCapacity);
    m_instanceInfo.clear();
    m_instanceSortScratch.clear();

    return true;
}

void GLRenderer::ShutdownModelPipeline()
{
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

    if (m_modelVBO) {
        glDeleteBuffers(1, &m_modelVBO);
        m_modelVBO = 0;
    }
    if (m_modelVAO) {
        glDeleteVertexArrays(1, &m_modelVAO);
        m_modelVAO = 0;
    }

    // Phase 2.3: clean up instanced model shader.

    if (m_whiteTexture) {
        glDeleteTextures(1, &m_whiteTexture);
        m_whiteTexture = 0;
    }
    if (m_phongTexture) {
        glDeleteTextures(1, &m_phongTexture);
        m_phongTexture = 0;
    }
    if (m_envTexture) {
        glDeleteTextures(1, &m_envTexture);
        m_envTexture = 0;
    }

    m_worldModelItems.clear();
    m_transparentModelItems.clear();
    m_objectList.clear();
}

bool GLRenderer::InitializeModelPipeline()
{
    glGenVertexArrays(1, &m_modelVAO);
    glGenBuffers(1, &m_modelVBO);

    glBindVertexArray(m_modelVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_modelVBO);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);

    // Phase 1.4: packed ModelVertex layout (32 bytes).
    //   attribute 0: vec3  aPos                 (12 bytes, float)
    //   attribute 1: vec2  aTexCoord             ( 8 bytes, float)
    //   attribute 2: vec4  light/fog/alpha/cutout ( 4 bytes, uint8 normalized)
    //   attribute 3: vec3  fogR/fogG/fogB        ( 3 bytes, uint8 normalized)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT,         GL_FALSE, sizeof(ModelVertex), reinterpret_cast<void*>(offsetof(ModelVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT,         GL_FALSE, sizeof(ModelVertex), reinterpret_cast<void*>(offsetof(ModelVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(ModelVertex), reinterpret_cast<void*>(offsetof(ModelVertex, light)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_UNSIGNED_BYTE, GL_TRUE,  sizeof(ModelVertex), reinterpret_cast<void*>(offsetof(ModelVertex, fogR)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Create 1x1 white texture for flat-color rendering (circles, overlays)
    glGenTextures(1, &m_whiteTexture);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
    const uint32_t white = 0xFFFFFFFF;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

#endif // _gl
