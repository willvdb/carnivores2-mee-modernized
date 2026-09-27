// ==========================================================================
// GLShadow.cpp � Projected shadow rendering
// ==========================================================================

#include "Hunt.h"
#include "GLRenderer.h"
#include "Renderer/GLUtils.h"

#ifdef _gl

#include "glad/glad.h"
#include <cmath>

void GLRenderer::BuildCharacterShadowVertices(const TCharacter& character, float alpha,
                                              std::vector<ModelVertex>& outVerts)
{
    if (!character.pinfo || !character.pinfo->mptr || alpha <= 0.0f) {
        return;
    }

    TModel* mptr = character.pinfo->mptr.get();
    if (!mptr->gVertex || !mptr->gFace || mptr->VCount <= 0 || mptr->FCount <= 0) {
        return;
    }

    const float cal = pi / 2.0f - character.alpha;
    const float cla = std::cos(cal);
    const float sla = std::sin(cal);
    const float caCam = std::cos(CameraAlpha);
    const float saCam = std::sin(CameraAlpha);
    const float cbCam = std::cos(CameraBeta);
    const float sbCam = std::sin(CameraBeta);

    std::vector<Vector3d> projected(mptr->VCount);
    std::vector<FogSample> projectedFog(mptr->VCount);
    bool anyVisible = false;

    for (int s = 0; s < mptr->VCount; ++s) {
        const TPoint3d& source = mptr->gVertex[s];
        const float mrx = source.x * cla + source.z * sla;
        const float mrz = source.z * cla - source.x * sla;

        const float shx = mrx + source.y * SunShadowK;
        const float shz = mrz + source.y * SunShadowK;
        const float shy = GetLandH(shx + character.pos.x, shz + character.pos.z) - character.pos.y;

        // Sample at the ground receiver, NOT the character's elevated vertex
        // or its view-space/depth-biased projection. One sample per unique
        // vertex is shared by all faces, in both batched and fallback draws.
        const Vector3d groundPoint = {
            shx + character.pos.x - CameraX,
            shy + character.pos.y - CameraY,
            shz + character.pos.z - CameraZ
        };
        projectedFog[s] = SamplePocketFogAtPoint(groundPoint, false);

        Vector3d out;
        out.x = (shx * caCam + shz * saCam) + character.rpos.x;
        const float vz = shz * caCam - shx * saCam;
        out.y = (shy * cbCam - vz * sbCam) + character.rpos.y;
        out.z = (vz * cbCam + shy * sbCam) + character.rpos.z + 8.0f;
        projected[s] = out;

        if (out.z < kModelNearClip) {
            anyVisible = true;
        }
    }

    if (!anyVisible) {
        return;
    }

    outVerts.reserve(static_cast<size_t>(mptr->FCount) * 3);

    const uint8_t alphaByte = Float01ToByte(alpha);
    auto appendVertex = [&](int index) {
        const Vector3d& p = projected[index];
        const FogSample& fog = projectedFog[index];
        // Fog the black shadow colour, retaining its authored alpha. Blending
        // fogged black over fogged terrain leaves the fog veil intact; merely
        // fading a black shadow's alpha would still darken that veil.
        outVerts.push_back({p.x, p.y, p.z, 0.0f, 0.0f,
            0, Float01ToByte(fog.amount), alphaByte, 0,
            Float01ToByte(fog.color.x), Float01ToByte(fog.color.y), Float01ToByte(fog.color.z),
            {0,0,0,0,0}});
    };

    for (int f = 0; f < mptr->FCount; ++f) {
        const TFace& face = mptr->gFace[f];
        const Vector3d& p0 = projected[face.v1];
        const Vector3d& p1 = projected[face.v2];
        const Vector3d& p2 = projected[face.v3];

        if (ShouldCullModelFace(face.Flags, p0, p1, p2)) {
            continue;
        }

        appendVertex(face.v1);
        appendVertex(face.v2);
        appendVertex(face.v3);
    }
}

void GLRenderer::RenderProjectedCharacterShadow(const TCharacter& character, float alpha)
{
    std::vector<ModelVertex> shadowVertices;
    BuildCharacterShadowVertices(character, alpha, shadowVertices);
    if (shadowVertices.empty()) {
        return;
    }

    DrawModelVertices(m_whiteTexture, shadowVertices, BuildLegacyProjection(), true, true, false);
}

void GLRenderer::RenderProjectedShadows()
{
#ifdef GL_PERF_HOOKS
    GL_PERF_SCOPE("RenderProjectedShadows");
#endif
    if (!SHADOWS3D || IsUnderwater()) {
        return;
    }

    std::vector<std::pair<float, const TCharacter*>> sortedCharacters;
    sortedCharacters.reserve(ChCount);

    for (int i = 0; i < ChCount; ++i) {
        const TCharacter& character = Characters[i];
        if (!character.pinfo) {
            continue;
        }

    const float distanceSq = VectorLengthSq(character.rpos);
    const float visibilityRadius = static_cast<float>(ctViewR * 256);
    const float shadowCullRadius = static_cast<float>(256 * (ctViewR - 8));
    const float visibilityRadiusSq = visibilityRadius * visibilityRadius;
    const float shadowCullRadiusSq = shadowCullRadius * shadowCullRadius;

        if (distanceSq > visibilityRadiusSq || distanceSq > shadowCullRadiusSq) continue;
        // Render3DHardwarePosts has rotated every character inside this distance.
        // Use the same extent-aware planes so a visible body does not lose its
        // projected shadow as its origin crosses the edge of the viewport.
        if (SphereOutsideView(character.rpos, CharacterCullRadius(character))) continue;

        float alpha = 0x60 / 255.0f;
        if (character.Health == 0) {
            if (Tranq || character.CType == 11) {
                continue;
            }

            const int aniTime = character.pinfo->Animation[character.Phase].AniTime;
            if (aniTime <= 0 || character.FTime >= aniTime - 1) {
                continue;
            }

            alpha *= static_cast<float>(aniTime - character.FTime) / static_cast<float>(aniTime);
        }

        if (alpha <= 0.0f) {
            continue;
        }

        sortedCharacters.emplace_back(distanceSq, &character);
    }

    std::sort(sortedCharacters.begin(), sortedCharacters.end(),
              [](const auto& a, const auto& b) {
                  return a.first > b.first;
              });

    // Step 6: Shadow fade with distance
    const float shadowFadeStart = static_cast<float>((ctViewR - 8) << 8);
    const float shadowFadeEnd = 256.0f * static_cast<float>(ctViewR - 4);

    if (GpuFeatureEnabled(GPUF_SHADOWS_INSTANCING)) {
        std::vector<ModelVertex> shadowBatch;
        shadowBatch.reserve(sortedCharacters.size() * static_cast<size_t>(384));
        for (const auto& entry : sortedCharacters) {
            const TCharacter& character = *entry.second;
            float alpha = 0x60 / 255.0f;
            if (character.Health == 0) {
                const int aniTime = character.pinfo->Animation[character.Phase].AniTime;
                if (aniTime > 0) {
                    alpha *= static_cast<float>(aniTime - character.FTime) / static_cast<float>(aniTime);
                }
            }

            // Apply distance-based fade to shadow alpha
            const float distanceSq = VectorLengthSq(character.rpos);
            if (distanceSq > shadowFadeStart * shadowFadeStart) {
                float shadowFade = CalcTerrainAlpha(distanceSq, shadowFadeStart,
                                                    shadowFadeStart * shadowFadeStart,
                                                    shadowFadeEnd, IsUnderwater());
                alpha *= shadowFade;
            }

            if (alpha <= 0.005f) continue;

            BuildCharacterShadowVertices(character, alpha, shadowBatch);
        }
        if (!shadowBatch.empty()) {
            DrawModelVertices(m_whiteTexture, shadowBatch, BuildLegacyProjection(), true, true, false);
        }
    } else {
        for (const auto& entry : sortedCharacters) {
            const TCharacter& character = *entry.second;
            float alpha = 0x60 / 255.0f;
            if (character.Health == 0) {
                const int aniTime = character.pinfo->Animation[character.Phase].AniTime;
                if (aniTime > 0) {
                    alpha *= static_cast<float>(aniTime - character.FTime) / static_cast<float>(aniTime);
                }
            }

            // Apply distance-based fade to shadow alpha
            const float distanceSq = VectorLengthSq(character.rpos);
            if (distanceSq > shadowFadeStart * shadowFadeStart) {
                float shadowFade = CalcTerrainAlpha(distanceSq, shadowFadeStart,
                                                    shadowFadeStart * shadowFadeStart,
                                                    shadowFadeEnd, IsUnderwater());
                alpha *= shadowFade;
            }

            if (alpha <= 0.005f) continue;

            RenderProjectedCharacterShadow(character, alpha);
        }
    }
}

#endif // _gl
