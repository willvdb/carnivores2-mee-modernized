#version 330 core
in vec2 vNdc;
in vec3 vWorldDir;     // world-space view-ray dir (y = elevation factor)
out vec4 FragColor;
uniform PerFrame {
   mat4 uProjection;
   vec2 uFogRange;
   vec3 uDistanceFogColor;
   float uForceFog;
   vec3 uFogColor;
};
uniform sampler2D uSkyTexture;
uniform vec2 uViewport;
uniform vec2 uVideoCenter;
uniform vec3 uQ;
uniform vec3 uP;
uniform vec3 uR;
uniform vec3 uFogReferenceQ;
uniform vec3 uFogReferenceP;
uniform vec3 uFogReferenceR;
uniform float uFogReferenceZoom;
uniform float uSkyTime;
uniform float uSkyVBias;      // sky texture V phase (texels; 256 = one wrap)
uniform int uSkyMode;         // 0 = legacy offset, 1 = level plane, 2 = dome
uniform vec2 uSkyDomeScale;   // dome: stereographic texels per radian at horizon
uniform float uSkyPlaneScale; // plane: texture texels = scale * cot(elevation)
uniform float uSkyPlaneDrop;  // plane: sin(horizon drop); lowers the
                              // compression singularity below the horizon
uniform vec2 uSkyPlaneAnchor; // plane: world-origin texture phase (texels)
uniform vec2 uSunScreenPos;   // sun/moon screen pos (top-origin)
uniform float uSunVisibility; // = m_skyTraceK
uniform float uSunGlow;       // master glow strength (sun 0.18, moon 0.10)
uniform float uBodyIsMoon;    // 1.0 = moon (night), 0.0 = sun (day)
uniform float uPocketFog;     // camera pocket-fog density (0..1)
uniform vec3  uPocketFogColor;// camera pocket-fog colour
uniform vec3  uCamFogColor;     // camera-in-fog envelope colour
uniform float uCamFogAmount;    // camera-in-fog envelope strength (0 = off)
uniform float uFogBase;
uniform float uUnderwaterDepth;
uniform float uWaterLineY;
void main() {
   vec2 pixel = vec2((vNdc.x * 0.5 + 0.5) * uViewport.x,
                     (1.0 - (vNdc.y * 0.5 + 0.5)) * uViewport.y);

   // World-space view ray (camera basis passed from C++).  Its .y is the
   // elevation factor: 0 at the true horizon, +1 straight up -- independent
   // of camera pitch.  Computed up front because the dome sky mode samples
   // the texture from it directly; the gradient/atmosphere math below reuses
   // it so the horizon stays anchored to the world.
   vec3 wdir = normalize(vWorldDir);

   float sx = pixel.x - uVideoCenter.x;
   float sy = uVideoCenter.y - pixel.y;
   float sxQ = uQ.x * sx + uQ.y * sy + uQ.z;
   // sign(0.0) is zero in GLSL. With a level sky plane, sxQ reaches zero
   // on the true horizon, so multiplying sign(sxQ) by the epsilon would
   // still divide by zero there. Choose a stable side at exactly zero.
   float q = (sxQ < 0.0 ? -1.0 : 1.0) * max(abs(sxQ), 0.001);
   float skyU = (uP.x * sx + uP.y * sy + uP.z) / q;
   float skyV = (uR.x * sx + uR.y * sy + uR.z) / q;
   // The legacy sky-fog proxy measures the UV span across a whole scanline.
   // It must use a non-optic reference projection: otherwise changing only
   // magnification changes atmospheric density. Dividing sy maps this pixel
   // to the equivalent reference-FOV ray; uQ/uP/uR above still map the
   // zoomed sky texture in the projected-plane modes (sky_mode 0/1), while
   // the dome mode derives the texture straight from the ray.
   float fogSy = sy / max(uFogReferenceZoom, 1.0);
   float leftQ = uFogReferenceQ.x * (-uVideoCenter.x) + uFogReferenceQ.y * fogSy + uFogReferenceQ.z;
   float rightQ = uFogReferenceQ.x * uVideoCenter.x + uFogReferenceQ.y * fogSy + uFogReferenceQ.z;
   float leftU = (uFogReferenceP.x * (-uVideoCenter.x) + uFogReferenceP.y * fogSy + uFogReferenceP.z) / max(abs(leftQ), 0.001);
   float leftV = (uFogReferenceR.x * (-uVideoCenter.x) + uFogReferenceR.y * fogSy + uFogReferenceR.z) / max(abs(leftQ), 0.001);
   float rightU = (uFogReferenceP.x * uVideoCenter.x + uFogReferenceP.y * fogSy + uFogReferenceP.z) / max(abs(rightQ), 0.001);
   float rightV = (uFogReferenceR.x * uVideoCenter.x + uFogReferenceR.y * fogSy + uFogReferenceR.z) / max(abs(rightQ), 0.001);
   float dx = rightU - leftU;
   float dy = rightV - leftV;
   float dt = sqrt(dx*dx + dy*dy) / 96.0 - 6.0;
   dt = clamp(dt, 0.0, 10.0);
   float fogFactor = clamp(max(dt * 225.0 / 10.0, uFogBase) / 255.0, 0.0, 1.0);
   fogFactor = clamp(fogFactor + uUnderwaterDepth * 0.55, 0.0, 1.0);
   float distToWaterLine = uWaterLineY - pixel.y;
   float fadeWidth = 32.0;
   if (distToWaterLine < fadeWidth && uWaterLineY < uViewport.y) {
       fogFactor = mix(1.0, fogFactor, clamp(distToWaterLine / fadeWidth, 0.0, 1.0));
   }
   vec2 uv;
   if (uSkyMode == 2) {
      // Direction-based dome sampling.  The view ray is projected onto a
      // stereographic cloud canopy:
      //   r = |d.xz| / (1 + d.y)
      // The upper hemisphere maps to the unit disc (zenith = canopy origin,
      // horizon = the unit circle) and the lower hemisphere keeps going
      // outside it.  Unlike an azimuth/elevation map there is no pole pinch:
      // the projection's only singularity is the nadir, which is always
      // under terrain.  The map is conformal, so cloud shapes keep their
      // shape everywhere; only the scale changes (2x between the horizon and
      // the zenith).  U/V follow the world X/Z axes, like the legacy plane,
      // so the canopy stays anchored to the world.
      float denom = max(1.0 + wdir.y, 0.001);   // 0.0 only at the nadir
      vec2 domeST = vec2(wdir.x, wdir.z) * uSkyDomeScale / denom;
      uv = vec2((domeST.x + uSkyTime) / 256.0,
                (domeST.y + uSkyVBias - uSkyTime) / 256.0);
   } else if (uSkyMode == 1) {
      // World-level projected plane sampled from the world ray, with the C1
      // horizon drop (uSkyPlaneDrop, sine units).  The drop lowers the
      // plane's compression singularity to a fixed world elevation below
      // the horizon, uniformly in azimuth: unlike C1's camera-space pitch
      // offset it never tilts the canopy, so cloud rows cannot lean while
      // turning, and the visible sky shows a finite texture band the way C1
      // did.  Terrain hides the singular ring in most ground-level views;
      // the mip chain resolves it smoothly wherever it is visible.  A drop
      // of 0 is the plain level plane (the below-horizon half mirrors
      // instead of running off to the opposite side).
      float denom = wdir.y + uSkyPlaneDrop;
      vec2 planeST = vec2(wdir.x, -wdir.z) *
                     (uSkyPlaneScale / max(abs(denom), 0.001));
      planeST += uSkyPlaneAnchor;
      uv = vec2((planeST.x + uSkyTime) / 256.0,
                (planeST.y + uSkyVBias - uSkyTime) / 256.0);
   } else {
      // Original camera-coupled projected plane (sky_mode 0).
      uv = vec2((skyU + uSkyTime) / 256.0, (skyV + uSkyVBias - uSkyTime) / 256.0);
   }
   vec3 skyColor = texture(uSkyTexture, uv).rgb;
   bool isMoon = uBodyIsMoon > 0.5;

   // Horizon-zenith gradient.  Real skies are not flat: the zenith
   // is darker and more saturated, while the horizon is lighter and warmer.
   // Derived from the world-space elevation (wdir.y), not the screen, so it
   // does not swim when the camera pitches up/down.  (The debug-tab gradient
   // uniforms operate on this same world-anchored vert.)
   float vert = max(0.0, wdir.y);                 // 0 at horizon, 1 at zenith

   // §3.10 (computed early): how strongly the global envelope fogs THIS sky
   // pixel.  Used both to fade the glows below WITH the fog (so the sun halo
   // does not punch through as a separated disc) and, later, to mix the sky to
   // the volume colour.  Gentler vertical fade than §3.5 so the upper sky
   // fogs too (a fog layer sits above the camera).
   float vertFade = pow(clamp(wdir.y * 0.5 + 0.5, 0.0, 1.0), 3.0);
   float envSky = (uCamFogAmount > 0.001f)
       ? uCamFogAmount * (1.0f - 0.6f * vertFade)
       : 0.0f;

   // Zenith darkening: 1.0 at the horizon, 0.92 at the zenith.
   float zenithDark = 0.92 + 0.08 * (1.0 - vert);
   skyColor *= zenithDark;

   // §3.6: Sun/moon glow on the sky texture — a soft halo around the body's
   // screen position.  `pixel` is top-origin, matching uSunScreenPos.  The sun
   // and moon are handled separately because they have very different
   // character: the sun is a bright, warm body with a warm-core / cool-outer
   // scattering halo, while the moon is dim and cool, so it gets a softer,
   // bluer, much fainter moonlight halo suited to the dark night sky.  When
   // the body is off-screen the distance is huge and the Gaussian falls to ~0.

   // Keep the warm atmospheric horizon glow for daytime only. Applying that
   // golden term to the night sky created an artificial orange city-glow band
   // that expanded across the screen as the camera pitched upward.
   if (!isMoon) {
       float horizonGlow = exp(-vert * vert * 20.0);
       vec3 glowColor = vec3(1.0, 0.85, 0.6);
       float glowStrength = 0.15 * (1.0 - fogFactor) * (1.0f - envSky);
       skyColor += glowColor * horizonGlow * glowStrength;
   }

   vec2 sunDelta = pixel - uSunScreenPos;
   float sunDist = length(sunDelta);
   float glowRadius = isMoon ? 110.0 : 80.0 + (1.0 - uSunVisibility) * 40.0;
   float sunGlow = exp(-sunDist * sunDist / (glowRadius * glowRadius));
   vec3 sunGlowColor;
   if (isMoon) {
       // Cool moonlight: soft white-blue core fading to faint blue at the rim.
       sunGlowColor = mix(vec3(0.85, 0.90, 1.0),
                         vec3(0.72, 0.80, 1.0),
                         clamp(sunDist / glowRadius, 0.0, 1.0));
   } else {
       // Sun: warm-white core fading to cool blue-white outer.
       sunGlowColor = mix(vec3(1.0, 0.95, 0.8),
                         vec3(0.9, 0.85, 1.0),
                         clamp(sunDist / glowRadius, 0.0, 1.0));
   }
   // Cloud occlusion: the sun's glow is dominated by direct-light scattering,
   // so it falls off sharply with cloud cover (squared) — heavy cloud all but
   // kills it.  The moon's glow is diffuse moonlight, so it uses a gentler
   // (linear) dependence; at night m_skyTraceK is a dimness proxy rather than
   // cloud cover, so we don't want to crush the moon glow.
   float occ = isMoon ? uSunVisibility : uSunVisibility * uSunVisibility;
   float sunGlowStrength = uSunGlow * occ * (1.0 - fogFactor * 0.5) * (1.0f - envSky);
   skyColor += sunGlowColor * sunGlow * sunGlowStrength;
   skyColor = min(skyColor, vec3(1.0));            // clamp to prevent burn-out

   // Night fog still affects the sky, but its volume colour is desaturated so
   // bright green/brown fog does not become an artificial saturated night sky.
   vec3 skyFogColor = uFogColor;
   vec3 skyPocketFogColor = uPocketFogColor;
   vec3 skyCamFogColor = uCamFogColor;
   if (isMoon) {
       const float kNightFogDesaturation = 0.75;
       float fogLuma = dot(uFogColor, vec3(0.299, 0.587, 0.114));
       float pocketLuma = dot(uPocketFogColor, vec3(0.299, 0.587, 0.114));
       float camLuma = dot(uCamFogColor, vec3(0.299, 0.587, 0.114));
       skyFogColor = mix(uFogColor, vec3(fogLuma), kNightFogDesaturation);
       skyPocketFogColor = mix(uPocketFogColor, vec3(pocketLuma), kNightFogDesaturation);
       skyCamFogColor = mix(uCamFogColor, vec3(camLuma), kNightFogDesaturation);
   }

   // §3.5: Per-pixel pocket fog on the sky.  Blend the (already globally
   // fogged) sky toward the volume colour, but only near the horizon —
   // the zenith stays clear so the gradient/glow still read.  Applied after
   // the water-line fade that is already folded into fogFactor.
   // Night fog remains visible, but uses a lower sky blend so bright dynamic
   // volume colours cannot turn the entire night sky into a glowing billboard.
   const float kNightSkyFogBlend = 0.35;
   float skyFogBlend = isMoon ? fogFactor * kNightSkyFogBlend : fogFactor;
   vec3 color = mix(skyColor, skyFogColor, skyFogBlend);
   float pocketFade = uPocketFog * (1.0 - vertFade);
   if (isMoon) pocketFade *= kNightSkyFogBlend;
   color = mix(color, skyPocketFogColor, pocketFade);

   // §3.10: fog the whole sky toward the volume colour.  envSky (computed
   // above, near wdir) already fades the sun/moon halo WITH this fog, so the
   // glow dissolves smoothly into the haze instead of sitting as a harsh,
   // separated disc on a flat fogged sky.
   if (uCamFogAmount > 0.001f) {
       float skyEnvelopeBlend = isMoon ? envSky * kNightSkyFogBlend : envSky;
       color = mix(color, skyCamFogColor, skyEnvelopeBlend);
   }

   FragColor = vec4(color, 1.0);
}
