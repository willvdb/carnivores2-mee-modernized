// ==========================================================================
// GLSky.cpp � Sky plane, sun, and atmospheric rendering
// ==========================================================================

#include "Hunt.h"
#include "GLRenderer.h"
#include "Renderer/GLUtils.h"
#include "Renderer/SkyFogProjection.h"

#ifdef _gl

#include "glad/glad.h"
#include <algorithm>
#include <cmath>
#include <vector>

void GLRenderer::RenderSkyPlane()
{
#ifdef GL_PERF_HOOKS
    GL_PERF_SCOPE("RenderSkyPlane");
#endif
    if (!m_skyVAO || !m_skyTexture || !m_skyShader.IsValid()) {
        return;
    }

    UploadSkyTexture();

    const float localCa = std::cos(CameraAlpha);
    const float localSa = std::sin(CameraAlpha);
    const float pitchCos = std::cos(CameraBeta);
    const float pitchSin = std::sin(CameraBeta);

    SKYDTime = RealTime & ((1 << 16) - 1);

    // Sky cloud-texture placement, selected by config.cfg "sky_mode" (see
    // SkyMappingMode in GameState.h):
    //   0 = legacy camera-coupled pitch offset (C2 dynamic 0.10..0.20 rad).
    //       The offset is applied after yaw, so the plane's tilt direction
    //       followed the camera and the cloud rows leaned by up to the full
    //       offset while turning.
    //   1 = world-level projected plane.  Level with the world (no
    //       camera-coupled tilt); OptSkyHorizonDrop lowers the compression
    //       singularity to a fixed elevation below the true horizon -- the
    //       C1 offset's effect, but as a world-elevation shift applied in
    //       the shader, so the canopy never leans while turning.  0 gives
    //       the plain level plane.
    //   2 = direction-based dome sampling in sky.frag (stereographic canopy,
    //       scaled by OptSkyDomeScale).  The plane basis below still supplies
    //       the legacy fog-proxy metric, so fog is identical in all modes.
    // kSkyVBias is the cloud-phase knob (texels, 256 = one wrap): in modes
    // 0/1 it selects which cloud band sits at the skyline.
    constexpr float kSkyVBias = 0.0f;

    const int skyMode = (OptSkyMode >= kSkyModeLegacy && OptSkyMode < kSkyModeCount)
                            ? OptSkyMode
                            : kSkyModeLevel;
    const float domeScale = (OptSkyDomeScale >= kSkyDomeScaleMin &&
                             OptSkyDomeScale <= kSkyDomeScaleMax)
                                ? OptSkyDomeScale
                                : kSkyDomeScaleDefault;
    // Mode 1 horizon drop (config.cfg "sky_horizon_drop", degrees).
    const float dropDeg = (OptSkyHorizonDrop >= kSkyHorizonDropMin &&
                           OptSkyHorizonDrop <= kSkyHorizonDropMax)
                              ? OptSkyHorizonDrop
                              : kSkyHorizonDropDefault;
    const float dropSin = std::sin(dropDeg * (3.14159265358979323846f / 180.0f));
    // Legacy plane texture scale in texels: 0.004 texels per world unit at
    // the inherited 4*512*16 plane height (131.072 texels per unit cot).
    constexpr float kPlaneTexelScale = 0.004f * (4.0f * 512.0f * 16.0f);

    // Keep the legacy pitch calculation available in every mode. The
    // projected texture is allowed to choose a different basis, but the
    // inherited scanline-width fog proxy must not change when sky_mode is
    // changed. This pitch is therefore the canonical fog basis as well as
    // the texture basis for sky_mode 0.
    const float heightAboveTerrain = (std::max)(0.0f, -CameraY);
    const float altitudeFactor = (std::clamp)(
        heightAboveTerrain / (200.0f * ctHScale), 0.0f, 1.0f);
    const float legacySkyPitch =
        CameraBeta - (0.20f - altitudeFactor * 0.10f);
    const float skyPitch = (skyMode == kSkyModeLegacy)
                               ? legacySkyPitch
                               : CameraBeta;

    struct SkyProjectionBasis {
        Vector3d tangentX;
        Vector3d tangentY;
        Vector3d normal;
        float planeP;
        float ddx;
        float ddy;
    };

    const auto buildSkyBasis = [&](float pitch) {
        const float pitchCos = std::cos(pitch);
        const float pitchSin = std::sin(pitch);
        SkyProjectionBasis basis = {
            {0.004f, 0.0f, 0.0f},
            {0.0f, 0.0f, 0.004f},
            {0.0f, -1.0f, 0.0f},
            0.0f,
            0.0f,
            0.0f,
        };

        auto rotateSky = [&](Vector3d& v) {
            // First rotate around Y axis (CameraAlpha).
            float x = v.x * localCa - v.z * localSa;
            float z = v.z * localCa + v.x * localSa;
            // Then rotate around X axis (the selected sky pitch).
            float y = v.y * pitchCos + z * pitchSin;
            float zz = z * pitchCos - v.y * pitchSin;

            v.x = x;
            v.y = y;
            v.z = zz;
        };

        rotateSky(basis.tangentX);
        rotateSky(basis.tangentY);
        rotateSky(basis.normal);

        Vector3d vbase = {-CameraX, 4.0f * 512.0f * 16.0f, CameraZ};
        rotateSky(vbase);

        basis.planeP = basis.normal.x * vbase.x +
                       basis.normal.y * vbase.y +
                       basis.normal.z * vbase.z;
        basis.ddx = vbase.x * basis.tangentX.x +
                    vbase.y * basis.tangentX.y +
                    vbase.z * basis.tangentX.z;
        basis.ddy = vbase.x * basis.tangentY.x +
                    vbase.y * basis.tangentY.y +
                    vbase.z * basis.tangentY.z;
        return basis;
    };

    const SkyProjectionBasis skyBasis = buildSkyBasis(skyPitch);
    const SkyProjectionBasis fogBasis = buildSkyBasis(legacySkyPitch);
    const auto buildProjection = [](const SkyProjectionBasis& basis,
                                     float cameraW, float cameraH) {
        return skyfog::BuildProjectionCoefficients(
            basis.normal, basis.tangentX, basis.tangentY, basis.planeP,
            basis.ddx, basis.ddy, cameraW, cameraH);
    };
    const skyfog::ProjectionCoefficients skyProjection =
        buildProjection(skyBasis, CameraW, CameraH);

    // The inherited sky fog is based on the texture span across a scanline,
    // not a world-space distance. Controls.cpp multiplies CameraW/H for an
    // optic, so evaluate that metric with the player's non-optic FOV instead.
    // The shader reprojects its row by this same factor to preserve fog for
    // the same world-space ray while the texture itself remains zoomed.
    //
    // Important: use fogBasis, not skyBasis. This preserves the pre-change
    // fog envelope in sky_mode 1/2 while still allowing their cloud mapping
    // to use a level plane or dome.
    const float opticZoom = (std::max)(
        1.0f, IsBinocularView() ? BinocularPower : ActiveWorldZoom());
    const skyfog::ProjectionCoefficients fogProjection =
        buildProjection(fogBasis, CameraW, CameraH);
    const skyfog::ProjectionCoefficients fogReferenceProjection =
        opticZoom > 1.0f
            ? buildProjection(fogBasis, CameraW / opticZoom, CameraH / opticZoom)
            : fogProjection;

    // The sky's distance-fog color is global, not the color of the
    // fixed fog volume the camera is currently inside. Local volumes are
    // still applied to terrain/models by their per-vertex fog color.
    const Vector3d targetSkyFogColor = GetDistanceFogColor();

    // Temporal low-pass filter on the sky color so day/night sky changes
    // settle smoothly instead of popping between frames.
    if (!m_smoothedSkyFogColorInit) {
        m_smoothedSkyFogColor = targetSkyFogColor;
        m_smoothedSkyFogColorInit = true;
    } else {
        constexpr float k = 0.15f;
        m_smoothedSkyFogColor.x += (targetSkyFogColor.x - m_smoothedSkyFogColor.x) * k;
        m_smoothedSkyFogColor.y += (targetSkyFogColor.y - m_smoothedSkyFogColor.y) * k;
        m_smoothedSkyFogColor.z += (targetSkyFogColor.z - m_smoothedSkyFogColor.z) * k;
    }

    UpdatePerFrameUBO();
    m_skyShader.Use();
    glUniform1i(m_locSkyTexture, 0);
    glUniform2f(m_locSkyViewport, static_cast<float>(WinW), static_cast<float>(WinH));
    glUniform2f(m_locSkyVideoCenter, static_cast<float>(VideoCX), static_cast<float>(VideoCY));
    // uFogColor now sourced from PerFrame UBO (Phase 1.1). uForceFog is
    // still bound in the UBO (kept for layout compatibility) but no longer
    // used by this shader; the sky's underwater look comes from the
    // 3dfx fog formula plus the uUnderwaterDepth uniform, plus scissor
    // clipping below the water surface horizon.
    glUniform3f(m_locSkyQ, skyProjection.q.x, skyProjection.q.y, skyProjection.q.z);
    glUniform3f(m_locSkyP, skyProjection.p.x, skyProjection.p.y, skyProjection.p.z);
    glUniform3f(m_locSkyR, skyProjection.r.x, skyProjection.r.y, skyProjection.r.z);
    glUniform3f(m_locSkyFogReferenceQ, fogReferenceProjection.q.x,
                fogReferenceProjection.q.y, fogReferenceProjection.q.z);
    glUniform3f(m_locSkyFogReferenceP, fogReferenceProjection.p.x,
                fogReferenceProjection.p.y, fogReferenceProjection.p.z);
    glUniform3f(m_locSkyFogReferenceR, fogReferenceProjection.r.x,
                fogReferenceProjection.r.y, fogReferenceProjection.r.z);
    glUniform1f(m_locSkyFogReferenceZoom, opticZoom);
    // Reverted: original (non-wind) sky scroll.  The gradient,
    // sun glow and pocket fog below are unchanged.
    glUniform1f(m_locSkyTime, static_cast<float>(SKYDTime) / 256.0f);
    glUniform1f(m_locSkyVBias, kSkyVBias);
    glUniform1i(m_locSkyMode, skyMode);
    glUniform2f(m_locSkyDomeScale, domeScale, domeScale);
    // World-level plane sampling constants (sky_mode 1).  The anchor keeps
    // the canopy phased to world X/Z the way the legacy plane coefficients
    // do (U = 0.004 * plane.x, V = -0.004 * plane.z).
    glUniform1f(m_locSkyPlaneScale, kPlaneTexelScale);
    glUniform1f(m_locSkyPlaneDrop, dropSin);
    glUniform2f(m_locSkyPlaneAnchor, 0.004f * CameraX, -0.004f * CameraZ);

    // §3.6: Sun glow on sky texture.  The sun's screen position (m_sunScrX/Y)
    // and visibility (m_skyTraceK) are members updated by RenderSun(), which
    // runs later in this same function — so these values are at most one
    // frame stale.  That lag is imperceptible for a slowly-moving sun.
    glUniform2f(m_locSkySunScreenPos, static_cast<float>(m_sunScrX), static_cast<float>(m_sunScrY));
    glUniform1f(m_locSkySunVisibility, m_skyTraceK);
    // §3.6: sun vs moon get different glow character.  The sun is bright and
    // warm; the moon is dim and cool, so it gets a smaller master strength
    // (handled in the shader via uBodyIsMoon).  OptDayNight==2 is night/moon.
    glUniform1f(m_locSkySunGlow, (OptDayNight == 2) ? 0.10f : 0.18f);
    glUniform1f(m_locSkyBodyIsMoon, (OptDayNight == 2) ? 1.0f : 0.0f);

    // §3.1 (world-space gradient): pass the camera basis so the sky shader
    // can derive the view ray's world elevation (pitch-invariant horizon).
    // The shader builds a WORLD-space view ray:
    //     vWorldDir = pos.x*uCamRight + pos.y*uCamUp + uCamForward
    // so these must be the camera's right/up/forward AXES in WORLD space, i.e.
    // R^(-1) * unit_axis, where R = R_x(beta)*R_y(alpha) is the engine's
    // world->view rotation (RotateVector).  NOTE: RotateVector computes R*v
    // (world->view), which is the WRONG direction here — the gradient needs
    // the inverse.  R^(-1) = R_y(-alpha)*R_x(-beta); the closed forms below are
    // exactly that inverse applied to (1,0,0), (0,1,0) and (0,0,-1).  camRight.y
    // = 0 is correct: with no camera roll the right axis is always horizontal.
    // (This basis was briefly swapped for a RotateVector-based one following a
    // review that misread RotateVector as view->world; that broke the gradient
    // under combined yaw+pitch and was reverted.)
    {
        const float camCa = std::cos(CameraAlpha);
        const float camSa = std::sin(CameraAlpha);
        const float camCb = std::cos(CameraBeta);
        const float camSb = std::sin(CameraBeta);
        const Vector3d camRight   = { camCa, 0.0f, camSa };                 // R^(-1)*(1,0,0)
        const Vector3d camUp      = { camSb * camSa, camCb, -camSb * camCa }; // R^(-1)*(0,1,0)
        const Vector3d camForward = { camCb * camSa, -camSb, -camCb * camCa }; // R^(-1)*(0,0,-1)
        const float tanX = (CameraW > 1e-3f) ? VideoCX / CameraW : 1.0f;
        const float tanY = (CameraH > 1e-3f) ? VideoCY / CameraH : 1.0f;
        glUniform3f(m_locSkyCamRight,   camRight.x * tanX,   camRight.y * tanX,   camRight.z * tanX);
        glUniform3f(m_locSkyCamUp,      camUp.x * tanY,      camUp.y * tanY,      camUp.z * tanY);
        glUniform3f(m_locSkyCamForward, camForward.x,        camForward.y,        camForward.z);
    }

    // §3.5: Per-pixel pocket fog on the sky.  Sample CalcFogLevel at the
    // camera (origin in view space) and, when a pocket fog volume is active,
    // pass its density/colour to the shader so the horizon blends into it.
    // Gated so it only activates with a real pocket fog (CameraFogI in
    // 1..126) and never underwater.  Reuses GetFogColor() (engine-canonical
    // decoder) so the sky fog colour matches the rest of the scene.
    float pocketFogAmount = 0.0f;
    Vector3d pocketFogColor = {0.0f, 0.0f, 0.0f};
    if (FOGON && !IsUnderwater() && CAMERAINFOG && CameraFogI > 0 && CameraFogI < 127) {
        const Vector3d cameraFogProbe = {0.0f, 0.0f, 0.0f};
        const float cameraFog = CalcFogLevel(cameraFogProbe);
        pocketFogColor = GetFogColor();
        pocketFogAmount = (std::max)(0.0f, (std::min)(1.0f,
            cameraFog / (std::max)(1.0f, FogsList[CameraFogI].FLimit)));
    }
    glUniform1f(m_locSkyPocketFog, pocketFogAmount);
    glUniform3f(m_locSkyPocketFogColor, pocketFogColor.x, pocketFogColor.y, pocketFogColor.z);

    // §3.10: camera-in-fog global envelope also fogs the sky (see sky.frag).
    {
        static const GLint uCamFogCol = glGetUniformLocation(m_skyShader.GetProgramID(), "uCamFogColor");
        static const GLint uCamFogAmt = glGetUniformLocation(m_skyShader.GetProgramID(), "uCamFogAmount");
        if (uCamFogCol >= 0 && uCamFogAmt >= 0 && g_GLRenderer) {
            const Vector3d c = g_GLRenderer->GetCamEnvelopeColor();
            glUniform3f(uCamFogCol, c.x, c.y, c.z);
            glUniform1f(uCamFogAmt, g_GLRenderer->GetCamEnvelopeAmount());
        }
    }

    // Sample CalcFogLevel directly above the camera (X=0, Z=0 in
    // camera-relative space) at sky height to get the base fog amount
    // for the per-pixel sky gradient. Using (0, ...) instead of
    // (512, ...) keeps the probe in the same map cell as the camera,
    // so the resulting fog amount matches the volume the camera is in
    // (when CAMERAINFOG) and doesn't jump as the camera crosses cell
    // boundaries along the X axis.
    const Vector3d fogProbe = {0.0f, 4.0f * 512.0f * 16.0f, 0.0f};
    const float fogBase = CalcFogLevel(fogProbe);
    glUniform1f(m_locSkyFogBase, fogBase);

    // uUnderwaterDepth: 0 above water, ramps to 1 at ~1024 world units
    // below the surface.  CameraWaterDepthFactor is computed once per
    // frame in ProcessControls().  The shader uses it to add up to 30%
    // extra fog on top of the 3dfx sky formula, giving a depth-based
    // dimming effect that matches the per-vertex fog on terrain and models.
    glUniform1f(m_locSkyUnderwaterDepth, CameraWaterDepthFactor);

    // uWaterLineY: screen Y (from top) of the water surface horizon.
    // Used by the shader to fade the sky to full fog near the water
    // line for a seamless blend with the distance-fog colour below.
    // Set to WinH (no fade) when not underwater or looking up.
    float waterLineY = static_cast<float>(WinH);

    // When the camera is underwater, clip the sky to only render above
    // the water-surface horizon in screen space.  Below that line the
    // distance-fog colour (already set as glClearColor) fills the
    // background and terrain renders on top.  Matches the 3DFX renderer
    // which only draws the sky plane down to scry (Render3DFX.cpp:4841).
    //
    // Three cases for the screen-space water horizon (scry, Y from top):
    //   scry >= WinH  → water line below screen → looking UP through
    //                   water surface → full sky (dimmed by shader)
    //   0 < scry < WinH → water line on screen → scissor to top scry px
    //   scry <= 0    → water line above screen → looking DOWN into
    //                   water → no sky at all
    bool underwaterFullSky = false;  // scry >= WinH: full sky, no scissor
    bool scissorEnabled = false;
    if (IsUnderwater()) {
        // Reuse the pitch cos/sin computed at the top of this function.
        const float waterLevel = GetLandUpH(CameraX, CameraZ);
        // Camera-relative height of the water surface in world units
        // (positive when the camera is below the surface).
        const float sh = waterLevel - CameraY;
        // A point on the water surface at a representative distance
        // directly in front of the camera.
        const float vz = static_cast<float>(ctViewR * 4 / 5) * 256.0f;
        const float vy = sh;
        // Rotate by CameraBeta (pitch) to get view-space position.
        float viewY = vy * pitchCos + vz * pitchSin;
        float viewZ = vz * pitchCos - vy * pitchSin;
        if (viewZ < 128.0f) viewZ = 128.0f;
        // Project to screen space (Y from top, matching 3DFX scry).
        int scry = VideoCY - static_cast<int>((viewY / viewZ) * CameraH);

        if (scry >= WinH) {
            // Water horizon is below the screen: the entire view is above
            // the water surface (looking up through water at the sky).
            // Render the full sky with the shader's depth-based dimming.
            underwaterFullSky = true;
            waterLineY = static_cast<float>(WinH);  // no water line on screen
        } else if (scry > 0) {
            // Water horizon is on screen.  Clip the sky to the top
            // scry pixels so it only appears above the water line.
            // OpenGL window coords: Y=0 at bottom, so the region is
            // [WinH - scry, WinH].
            glEnable(GL_SCISSOR_TEST);
            glScissor(0, WinH - scry, WinW, scry);
            scissorEnabled = true;
            waterLineY = static_cast<float>(scry);
        }
        // else scry <= 0: entire view is below water surface.
        // Sky is completely hidden — scissorEnabled stays false,
        // underwaterFullSky stays false, and the draw is skipped.
    }

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_skyTexture);
#ifdef GL_PERF_HOOKS
    GL_PERF_TEXTURE_BIND(m_skyTexture);
#endif

    const bool shouldDrawSky = !IsUnderwater() || underwaterFullSky || scissorEnabled;
    if (shouldDrawSky) {
        glUniform1f(m_locSkyWaterLineY, waterLineY);
        glBindVertexArray(m_skyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
#ifdef GL_PERF_HOOKS
        GL_PERF_DRAW(1);
#endif
        glBindVertexArray(0);
    }

    glDepthMask(GL_TRUE);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glEnable(GL_DEPTH_TEST);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif

    // Render sun on top of sky (matching D3D/3DFX: sky plane renders sun).
    // The sun is only drawn when the sky is drawn.  The scissor (if any)
    // is still active, so the sun is also clipped to above the water line.
    if (SunModel && shouldDrawSky) {
        m_sunLight = 0.0f;
        // Match the legacy D3D/3DFX source for dawn, day, and night.
        Vector3d sunDir = Sun3dPos;
        sunDir = RotateVector(sunDir);
        if (sunDir.z < -2024.0f) {
            RenderSun(sunDir.x, sunDir.y, sunDir.z);
            // GetSkyK is called inside RenderSun for cloud occlusion.
            // GetTraceK (depth-based) is deferred to ShowVideo() after the
            // full scene is rendered, so the depth buffer has terrain/models.
        }
    }

    if (scissorEnabled) {
        glDisable(GL_SCISSOR_TEST);
    }
}

void GLRenderer::RenderFSRect(uint32_t color, bool additive)
{
    float a = static_cast<float>((color >> 24) & 0xFF) / 255.0f;
    float r = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    float g = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    float b = static_cast<float>(color & 0xFF) / 255.0f;

    // Reuse the persistent 1x1 white texture created for flat-color rendering.
    if (!m_whiteTexture) return;

    // Build a fullscreen quad using the packed ModelVertex layout
    // (Phase 1.4: 32 bytes, color attributes are uint8 normalized).
    // The pre-Phase-1.4 local FSVertex struct used float fields for
    // light/fog/fogR/G/B/alpha/cutout, which the GL driver reads as
    // raw bytes -- producing garbage colors. Must use the same packed
    // layout as ModelVertex for the VBO's attribute pointers to interpret
    // the data correctly.
    const uint8_t lightByte  = 255;
    const uint8_t fogByte    = 255;                 // 1.0 normalized
    const uint8_t alphaByte  = static_cast<uint8_t>(a * 255.0f + 0.5f);
    const uint8_t cutoutByte = 0;
    const uint8_t fogRByte   = static_cast<uint8_t>(r * 255.0f + 0.5f);
    const uint8_t fogGByte   = static_cast<uint8_t>(g * 255.0f + 0.5f);
    const uint8_t fogBByte   = static_cast<uint8_t>(b * 255.0f + 0.5f);
    const ModelVertex quad[6] = {
        {-1.0f, -1.0f, 0.0001f, 0, 0, lightByte, fogByte, alphaByte, cutoutByte, fogRByte, fogGByte, fogBByte, {0,0,0,0,0}},
        { 1.0f, -1.0f, 0.0001f, 0, 0, lightByte, fogByte, alphaByte, cutoutByte, fogRByte, fogGByte, fogBByte, {0,0,0,0,0}},
        { 1.0f,  1.0f, 0.0001f, 0, 0, lightByte, fogByte, alphaByte, cutoutByte, fogRByte, fogGByte, fogBByte, {0,0,0,0,0}},
        {-1.0f, -1.0f, 0.0001f, 0, 0, lightByte, fogByte, alphaByte, cutoutByte, fogRByte, fogGByte, fogBByte, {0,0,0,0,0}},
        { 1.0f,  1.0f, 0.0001f, 0, 0, lightByte, fogByte, alphaByte, cutoutByte, fogRByte, fogGByte, fogBByte, {0,0,0,0,0}},
        {-1.0f,  1.0f, 0.0001f, 0, 0, lightByte, fogByte, alphaByte, cutoutByte, fogRByte, fogGByte, fogBByte, {0,0,0,0,0}},
    };

    // The fullscreen quad is already in NDC, so the UBO must carry the
    // identity projection. With the world projection here, the NDC
    // vertices get re-projected off-screen and the glare is invisible.
    const std::array<float, 16> identity = {
        1.0f,0.0f,0.0f,0.0f, 0.0f,1.0f,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, 0.0f,0.0f,0.0f,1.0f
    };

    glDisable(GL_DEPTH_TEST);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glDepthMask(GL_FALSE);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glEnable(GL_BLEND);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    // Use additive blending (glare) or standard alpha blending (dark overlay)
    if (additive)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    else
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif

    UpdatePerFrameUBO(identity);
    m_modelShader.Use();
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    // uProjection in PerFrame UBO (Phase 1.1)

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_whiteTexture);
#ifdef GL_PERF_HOOKS
    GL_PERF_TEXTURE_BIND(m_whiteTexture);
#endif
    glBindVertexArray(m_modelVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_modelVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad);
    glDrawArrays(GL_TRIANGLES, 0, 6);
#ifdef GL_PERF_HOOKS
    GL_PERF_DRAW(2);
#endif
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void GLRenderer::ApplySunDepthOcclusion()
{
    // Called from ShowVideo() after the full scene is rendered.
    // Samples the depth buffer at the sun's screen position to check
    // if terrain/models are occluding the sun.
    // Depth-based occlusion sample is cached for the current frame in
    // m_lastSunTraceK / m_lastSunTraceScrX / m_lastSunTraceFrame so
    // ApplySunDepthOcclusion() can reuse it without a second readback.
    float traceK = 0.0f;
    if (m_sunScrX == m_lastSunTraceScrX &&
        m_sunScrY == m_lastSunTraceScrY &&
        RealTime == m_lastSunTraceFrame) {
        traceK = m_lastSunTraceK;
    } else {
        traceK = GetTraceK(m_sunScrX, m_sunScrY);
        m_lastSunTraceK = traceK;
        m_lastSunTraceScrX = m_sunScrX;
        m_lastSunTraceScrY = m_sunScrY;
        m_lastSunTraceFrame = RealTime;
    }
    m_sunLight *= traceK;
}

// UpdateSunVisibility() was removed: it had no call site (dead since its
// introduction) and combined cloud + depth occlusion into m_skyTraceK, which
// the architecture deliberately splits (cloud occlusion is live in GetSkyK
// during the sky pass; depth occlusion is applied to m_sunLight later in
// ApplySunDepthOcclusion()).  The §3.4 asymmetric transition now lives in
// GetSkyK's DeltaFunc.

void GLRenderer::RenderModelSun(TModel* mptr, float x0, float y0, float z0, int alpha)
{
    // Phase 2.2: ensure the static mesh is uploaded (cache hit after first call).
    UploadStaticMesh(mptr);

    if (!mptr || !mptr->lpTexture || !mptr->gVertex || !mptr->gFace) return;

    const GLuint texture = UploadModelTexture(mptr);
    if (!texture) return;

    m_sunModelVertices.clear();
    const size_t reserveCount = static_cast<size_t>(mptr->FCount) * 3;
    m_sunModelVertices.reserve(reserveCount);
    const float alphaVal = static_cast<float>(alpha) / 255.0f;

    for (int f = 0; f < mptr->FCount; ++f) {
        const TFace& face = mptr->gFace[f];
        const int texHeight = (mptr->TextureHeight > 1) ? mptr->TextureHeight : 1;

        auto makeVertex = [&](int vIdx, int tx, int ty) -> ModelVertex {
            const Vector2df uv = DecodeLegacyFaceUV(static_cast<float>(tx), static_cast<float>(ty), texHeight);
            return {
                mptr->gVertex[vIdx].x + x0,
                mptr->gVertex[vIdx].y + y0,
                mptr->gVertex[vIdx].z + z0,
                uv.x, uv.y,
                Light255ToByte(255.0f),  // full brightness
                Float01ToByte(0.0f),     // no fog
                Float01ToByte(alphaVal),  // per-frame alpha
                CutoutToByte(false),      // no cutout
                0, 0, 0,                  // fog color (unused)
                {0, 0, 0, 0, 0}
            };
        };

        m_sunModelVertices.push_back(makeVertex(face.v1, face.tax, face.tay));
        m_sunModelVertices.push_back(makeVertex(face.v2, face.tbx, face.tby));
        m_sunModelVertices.push_back(makeVertex(face.v3, face.tcx, face.tcy));
    }

    if (m_sunModelVertices.empty()) return;

    const auto projection = BuildLegacyProjection();
    UpdatePerFrameUBO();
    m_modelShader.Use();
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    // The moon/sun is a sky element, not world geometry. Keep it outside
    // the world-only night lighting applied by DrawModelVertices().
    {
        static const GLint uNight = glGetUniformLocation(m_modelShader.GetProgramID(), "uNightStrength");
        if (uNight >= 0) glUniform1f(uNight, 0.0f);
    }
    // uProjection in PerFrame UBO (Phase 1.1)

    // The weapon's phong/env-map draw passes set uTintByFogColor=1.0 on this
    // shader. With vFogColor=(0,0,0) on the sun, that multiplies litColor to
    // black and the sun goes invisible under additive blending. Reset to 0
    // here so the sun renders normally.
    glUniform1f(m_locModelTint, 0.0f);

    // §3.10: the sun is a sky/light element drawn additively on top of the
    // already-fogged sky. It must NOT inherit the model shader's global
    // envelope — that would colour-replace the bright corona into a harsh,
    // out-of-place fog-coloured additive blob when looking up inside a fog
    // volume. The sky shader fogs the sky around it and attenuates the halo
    // instead, so disable the envelope for this draw. (DrawModelVertices
    // re-pushes the real value for every subsequent world-model draw.)
    {
        static const GLint uCamFogAmt = glGetUniformLocation(m_modelShader.GetProgramID(), "uCamFogAmount");
        if (uCamFogAmt >= 0) glUniform1f(uCamFogAmt, 0.0f);
    }

    glEnable(GL_BLEND);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    // Both bodies use additive blending.  MOON.3DF has a black/transparent
    // surround; normal alpha blending lets that surround darken the sky and
    // produces a black halo around the moon.  Additive blending leaves the
    // surround neutral, exposing the soft halo generated by sky.frag.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glEnable(GL_DEPTH_TEST);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);  // don't write depth for sun
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
#ifdef GL_PERF_HOOKS
    GL_PERF_TEXTURE_BIND(texture);
#endif
    glBindVertexArray(m_modelVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_modelVBO);
    const GLsizeiptr vertexSize = static_cast<GLsizeiptr>(m_sunModelVertices.size() * sizeof(ModelVertex));
    glBufferData(GL_ARRAY_BUFFER, vertexSize, nullptr, GL_STREAM_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertexSize, m_sunModelVertices.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_sunModelVertices.size()));
#ifdef GL_PERF_HOOKS
    GL_PERF_DRAW(static_cast<uint32_t>(m_sunModelVertices.size()) / 3);
#endif

    glDepthMask(GL_TRUE);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glDisable(GL_BLEND);
#ifdef GL_PERF_HOOKS
    GL_PERF_STATE_CHANGE();
#endif
    glBindVertexArray(0);
}

void GLRenderer::RenderSun(float x, float y, float z)
{
    m_sunScrX = VideoCX + static_cast<int>(x / (-z) * CameraW);
    m_sunScrY = VideoCY - static_cast<int>(y / (-z) * CameraH);

    // Rate-limit the sky-occlusion readback to ~15 Hz (66 ms) to avoid
    // GPU pipeline stalls every frame.  When the sun screen position
    // hasn't changed and we updated recently, reuse the cached value.
    // GetTraceK (depth occlusion) is deferred to ApplySunDepthOcclusion()
    // which has its own caching; we don't need to worry about it here.
    if (m_sunScrX != m_lastSunVisibilityScrX ||
        m_sunScrY != m_lastSunVisibilityScrY ||
        RealTime - m_lastSunVisibilityUpdate >= 66) {
        GetSkyK(m_sunScrX, m_sunScrY);
        m_lastSunVisibilityUpdate = RealTime;
        m_lastSunVisibilityScrX = m_sunScrX;
        m_lastSunVisibilityScrY = m_sunScrY;
    }
    // else: m_skyTraceK retains the value from the last GetSkyK call

    float d = std::sqrt(x * x + y * y);
    if (d < 2048.0f) {
        m_sunLight = 220.0f - d * 220.0f / 2048.0f;
        if (m_sunLight > 140.0f) m_sunLight = 140.0f;
        m_sunLight *= m_skyTraceK;
    }

    if (d > 812.0f) d = 812.0f;
    d = (2048.0f + d) / 3048.0f;
    d += (1.0f - m_skyTraceK) / 2.0f;
    if (OptDayNight == 2) d = 1.5f;

    // §3.3: Sun-size modulation with haze/elevation.  The disc appears
    // larger through haze and at low sun, smaller on a clear high sun.
    // NOTE: x/y/z are already the rotated sun direction (the caller rotates
    // {-2048,4048,-2048} before calling RenderSun), so use them directly —
    // do NOT re-rotate here (that would double-rotate).
    float horizonFog = (m_skyTraceK < 0.8f) ? (1.0f - m_skyTraceK) * 0.6f : 0.0f;
    float altitude = (std::max)(0.0f, -CameraY / ctHScale);
    float sunLen = std::sqrt(x * x + y * y + z * z);
    float sunElev = (sunLen > 1e-3f) ? y / sunLen : 0.0f;   // up-component of rotated dir
    float elevFactor = 1.0f + (1.0f - (std::max)(0.0f, sunElev)) * 0.15f;
    // §3.3 review fix: the original altitude term was sign-inverted — it made
    // the disc grow at HIGH altitude.  Lower camera = more atmosphere = larger
    // sun, so use altitude directly (clamped) with no 0.85 floor.
    float sizeBoost = 1.0f + horizonFog
                    + (std::clamp)(1.0f - altitude / 200.0f, 0.0f, 1.0f) * 0.10f   // low altitude = bigger disc
                    + (elevFactor - 1.0f);
    float baseD = d;
    d *= sizeBoost;
    d = (std::min)(d, baseD * 1.5f);   // cap at +50% of the base scale

    // Underwater depth fade: the sun's corona should dim the deeper the
    // camera is below the water surface, matching the per-vertex fog
    // behaviour on terrain and models.  CameraWaterDepthFactor is computed
    // once per frame in ProcessControls() (0 at the surface, 1 at ~1024
    // world units below).  The sun keeps ~30% brightness at maximum fade
    // so it remains a faint glow when very deep, rather than vanishing
    // entirely.
    float depthAtten = 1.0f;
    if (IsUnderwater()) {
        depthAtten = 1.0f - CameraWaterDepthFactor * 0.7f;
    }

    // §3.4: Perceptual (non-linear) brightness curve on the *visibility*
    // signal only.  NOTE: m_sunLight is intentionally left linear — it also
    // drives the underwater/pocket fog-scatter paths, which are tuned
    // against the raw linear value.
    // §3.10: inside a fog volume the sun should recede into the haze rather
    // than stay a crisp disc. Dim it with the global envelope (gentle, so a
    // light fog barely touches it and even a dense fog only pulls it toward
    // ~30%), complementing the sky shader's halo attenuation.
    const float sunEnvDim = 1.0f - m_camEnvelopeAmount * 0.7f;
    // SunGlare_Disc scales the disc (1.0 = stock); m_sunLight-style paths
    // elsewhere are untouched (fog-scatter tuning reads the raw value).
    const int sunAlpha = static_cast<int>(200.0f * std::pow(m_skyTraceK, 0.6f) * depthAtten * sunEnvDim * SunGlare_Disc);

    // The moon uses the same sunAlpha as the day sun, so it is already
    // dimmed by clouds (m_skyTraceK).  RenderModelSun switches to normal
    // alpha blending at night (no additive glare), and the night-darkness
    // overlay provides the tonal dimming — so full brightness here is fine.
    RenderModelSun(SunModel.get(), x * d, y * d, z * d, sunAlpha);
}

float GLRenderer::GetSkyK(int x, int y)
{
    // Cloud-occlusion readback for the sun/moon glow.
    // Average the sky colour in a ring at R (outside the <=120px glow halo,
    // §3.6) and compare it to a SYMMETRIC reference ring at Rref around the
    // body. A symmetric reference (not a single off-centre point) removes the
    // directional offset, and averaging over a DENSE ring treats the cloud as
    // a FORMATION (overall coverage) instead of flickering with individual
    // cloud pixels, so the glow dims smoothly as the sun enters or leaves a
    // cloud. The sun/moon model is drawn after this read, so it is never
    // sampled. Near a screen edge keep the current value.
    //
    // PERF: the readback uses a double-buffered PBO. We kick the copy into the
    // current PBO (GPU -> GPU memory, no CPU stall) and process the PREVIOUS
    // frame's PBO, which was filled ~one readback earlier and is therefore
    // already complete when we map it. This removes the synchronous GPU->CPU
    // stall that a plain glReadPixels into a CPU buffer would cause.
    const int R = 140;       // detection ring (outside glow halo)
    const int Rref = 170;    // reference ring (local sky around the body)
    if (x < Rref || y < Rref || x > WinW - Rref || y > WinH - Rref) return m_skyTraceK;

    const int half = Rref;
    const int bx = x - half;
    const int ey = y + half;
    const int bw = 2 * half + 1;
    const int bh = 2 * half + 1;
    const size_t need = static_cast<size_t>(bw) * static_cast<size_t>(bh) * 4u;

    // Lazily allocate the double-buffered PBOs (block size is fixed by Rref).
    if (m_skyReadPBO[0] == 0) {
        glGenBuffers(2, m_skyReadPBO);
        for (int i = 0; i < 2; ++i) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_skyReadPBO[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, need, nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    // Process the previous frame's block (already GPU-complete when mapped).
    // Sample it at the sun position it was CAPTURED at (not the current one),
    // so a moving sun does not skew the ring samples.
    const int cur = m_skyReadPBOIdx;
    const int prev = 1 - m_skyReadPBOIdx;
    if (m_skyReadPBOReady) {
        const int capX = m_skyReadPBOX[prev];
        const int capY = m_skyReadPBOY[prev];
        const int cbx = capX - half;
        const int cey = capY + half;
        auto pidxOf = [&](int sx, int sy) { return (cey - sy) * bw * 4 + (sx - cbx) * 4; };
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_skyReadPBO[prev]);
        const unsigned char* ptr = static_cast<const unsigned char*>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
        if (ptr) {
            // Average the sky colour over a dense detection ring and reference ring.
            const int N = 32;
            long detR = 0, detG = 0, detB = 0;
            long refR = 0, refG = 0, refB = 0;
            for (int i = 0; i < N; ++i) {
                const float a = static_cast<float>(i) * (6.2831853f / static_cast<float>(N));
                const int cx = static_cast<int>(std::cos(a) * R + 0.5f);
                const int cy = static_cast<int>(std::sin(a) * R + 0.5f);
                const int di = pidxOf(capX + cx, capY + cy);
                detR += ptr[di + 0]; detG += ptr[di + 1]; detB += ptr[di + 2];

                const int rx = static_cast<int>(std::cos(a) * Rref + 0.5f);
                const int ry = static_cast<int>(std::sin(a) * Rref + 0.5f);
                const int ri = pidxOf(capX + rx, capY + ry);
                refR += ptr[ri + 0]; refG += ptr[ri + 1]; refB += ptr[ri + 2];
            }
            detR /= N; detG /= N; detB /= N;
            refR /= N; refG /= N; refB /= N;

            // Deviation between the averaged formation colours (cancels §3.1 gradient).
            const long dr = detR - refR, dg = detG - refG, db = detB - refB;
            const float dev = std::sqrt(static_cast<float>(dr * dr + dg * dg + db * db));
            // Cloud response is intentionally aggressive: ~2x the earlier sensitivity
            // (divisor 80 -> 40) and a much lower floor (0.2 -> 0.05) so an overcast
            // sun nearly loses its glow, while a clear sky (dev ~2-9) stays bright.
            float k = 1.0f - dev / 40.0f;
            if (k < 0.05f) k = 0.05f;
            if (k > 1.0f) k = 1.0f;

            // §2.2 workaround: hysteresis latch (INTERIM).  The ring-vs-ring
            // detector is blind when a large cloud covers both rings (dev ≈ 0 →
            // k ≈ 1 → "clear").  Once the detector sees a cloud EDGE (k drops),
            // this latch clamps k to prevent the false re-brighten while the sun
            // is still inside the cloud, and only clears after the sky has been
            // confirmed clear (k > 0.9) for several frames.  NOTE: this suppresses
            // the transit flicker but does NOT detect TRUE uniform overcast — a
            // cloud with no edge in the annulus never drops k below 0.5, so the
            // latch never arms and the glow stays bright.  A proper fix needs an
            // absolute clear-sky reference (the full sky.frag pipeline replicated
            // in C++); that is deferred to a separate, in-game-validated commit.
            // Applied BEFORE the night remap so it operates on the raw detector
            // value — at night k is remapped to 0.12–0.32, which would always
            // trigger the latch if applied after.
            const float kLatchThreshold = 0.5f;      // stronger dimming under cloud
            const int   kLatchClearFrames = 15;        // hold longer before allowing brighten
            if (k < kLatchThreshold) {
                m_cloudLatched = true;
                m_cloudLatchFrames = 0;
            } else if (k > 0.9f && m_cloudLatched) {
                m_cloudLatchFrames++;
                if (m_cloudLatchFrames >= kLatchClearFrames)
                    m_cloudLatched = false;
            }
            if (m_cloudLatched)
                k = (std::min)(k, kLatchThreshold);

            // Night remap: moon glow is dimmer and uses linear (not squared)
            // cloud dependence, so remap k to a lower range.
            if (OptDayNight == 2) k = 0.12f + k / 5.0f;

            // §2.1 (review fix): apply the §3.4 asymmetric transition HERE (where it
            // is actually live).  A cloud covering the sun (k < m_skyTraceK) darkens
            // fast; the sun emerging (k > m_skyTraceK) brightens slowly as the eye
            // readapts.  (The asymmetry was originally added to the now-deleted
            // UpdateSunVisibility().)
            const float speed = (k > m_skyTraceK) ? 0.04f : 0.08f;   // slow brighten, moderate darken
            // Smoother response so the glow tracks cloud coverage without
            // unrealistic flicker.  The dense ring average already provides
            // spatial smoothing; this adds temporal smoothing.
            DeltaFunc(m_skyTraceK, k, (speed + std::fabs(k - m_skyTraceK)) * (TimeDt / 192.0f));

            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    // Kick the async copy for THIS frame (GPU -> PBO, no CPU stall), recording
    // the position it was captured at so the next call samples it correctly.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_skyReadPBO[cur]);
    glReadPixels(bx, WinH - ey, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_skyReadPBOX[cur] = x;
    m_skyReadPBOY[cur] = y;
    m_skyReadPBOIdx = prev;
    m_skyReadPBOReady = true;

    return m_skyTraceK;
}

float GLRenderer::GetTraceK(int x, int y)
{
    if (x < 10 || y < 10 || x > WinW - 10 || y > WinH - 10) return 0.0f;

    // Batch-read a 21x21 block (covers all offsets from -10..+10) in one
    // glReadPixels call instead of 9 separate 1x1 reads.
    float block[21 * 21];
    glReadPixels(x - 10, WinH - (y + 10), 21, 21, GL_DEPTH_COMPONENT, GL_FLOAT, block);

    float k = 0.0f;
    // Sample 9 points around the sun position on the depth buffer
    const int offsets[][2] = {
        {0, 0}, {10, 0}, {-10, 0}, {0, 10}, {0, -10},
        {8, 8}, {8, -8}, {-8, 8}, {-8, -8}
    };
    for (const auto& off : offsets) {
        const float depth = block[(off[1] + 10) * 21 + (off[0] + 10)];
        // Depth near 1.0 means sky (nothing occluding)
        if (depth > 0.9999f) k += 1.0f;
    }
    k /= 9.0f;

    DeltaFunc(m_traceK, k, TimeDt / 1024.0f);
    return m_traceK;
}

void GLRenderer::UploadSkyTexture()
{
    if (!m_skyTextureDirty || !m_skyTexture) {
        return;
    }
    m_skyTextureDirty = false;

    std::vector<uint32_t> expanded(256 * 256);
    for (int i = 0; i < 256 * 256; ++i) {
        expanded[i] = Expand1555to8888(SkyPic[i]);
    }

    glBindTexture(GL_TEXTURE_2D, m_skyTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, expanded.data());
    // Regenerate mipmaps on every level's sky upload.  The projected-plane
    // mapping minifies the texture by orders of magnitude toward the horizon;
    // without mips the tiled cloud pattern aliases into the visible
    // woven/moire band.  Mip filtering resolves that region to the texture's
    // local average instead.
    glGenerateMipmap(GL_TEXTURE_2D);
}

void GLRenderer::ShutdownSkyPipeline()
{
    for (int i = 0; i < 2; ++i) {
        if (m_skyReadPBO[i]) {
            glDeleteBuffers(1, &m_skyReadPBO[i]);
            m_skyReadPBO[i] = 0;
        }
    }
    m_skyReadPBOIdx = 0;
    m_skyReadPBOReady = false;
    m_skyReadPBOX[0] = m_skyReadPBOX[1] = 0;
    m_skyReadPBOY[0] = m_skyReadPBOY[1] = 0;
    if (m_skyTexture) {
        glDeleteTextures(1, &m_skyTexture);
        m_skyTexture = 0;
    }
    if (m_skyVAO) {
        glDeleteVertexArrays(1, &m_skyVAO);
        m_skyVAO = 0;
    }

}

void GLRenderer::InitializeSkyPipeline()
{
    if (!m_skyShader.LoadFromFile("shaders/sky.vert", "shaders/sky.frag")) {
        LOG_ERROR("Sky shader compilation failed");
        return;
    }
    LOG_INFO("Sky shader compilation succeeded");

    glGenVertexArrays(1, &m_skyVAO);
    glGenTextures(1, &m_skyTexture);

    glBindTexture(GL_TEXTURE_2D, m_skyTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_skyTextureDirty = true;
}

#endif // _gl
