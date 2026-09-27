#version 330 core
out vec4 FragColor;
in vec2 vTexCoord;
in float vLight;
in float vFog;
in vec3 vFogColor;
in float vAlpha;
in float vCutout;
in float vViewZ;
in float vRadialDist;
uniform PerFrame {
   mat4 uProjection;
   vec2 uFogRange;
   vec3 uDistanceFogColor;
   float uForceFog;
   vec3 uFogColor;
};
uniform sampler2D uModelTexture;
uniform float uTintByFogColor;
uniform float uNightStrength;    // world-only night lighting (0=day, 1=night)
uniform vec3 uCamFogColor;       // camera-in-fog envelope colour
uniform float uCamFogAmount;     // camera-in-fog envelope strength (0 = off)
void main() {
   vec4 texColor = texture(uModelTexture, vTexCoord);
   if (vCutout > 0.5 && texColor.a <= 0.5) discard;
   vec3 litColor = texColor.rgb * vLight;
   // Phase 2.7: branch-less tint via mix (was if > 0.5).
   vec3 tinted = litColor * vFogColor;
   litColor = mix(litColor, tinted, uTintByFogColor);
   vec3 finalColor = mix(litColor, vFogColor, vFog);
   // Match the terrain/instanced-model horizon fade for legacy
   // model-path objects (BMP billboards and water-clipped meshes).
   // Radial distance (not forward-Z) so wide-FOV screen edges fog in
   // step with the radial CPU alpha fade — see terrain.frag.
   float distanceFog = clamp((vRadialDist - uFogRange.x) / max(uFogRange.y - uFogRange.x, 1.0), 0.0, 1.0);
   finalColor = mix(finalColor, uDistanceFogColor, distanceFog);

   // §3.10: camera-in-fog global envelope — see terrain.frag.  Fogs the whole
   // scene by distance when the camera is submerged in a tall pocket-fog volume.
   if (uCamFogAmount > 0.001f) {
       // Near baseline so close objects are also hazed (sells "inside fog");
       // far objects still reach the full envelope amount.
       const float kNearFog = 0.25f;
       float camEnvDist = kNearFog + (1.0f - kNearFog) * (1.0f - exp(-2.5f * vViewZ / max(uFogRange.y, 1.0f)));
       float camFog = uCamFogAmount * camEnvDist;
       // Weapon phong/env-map overlays are additive passes drawn over a body
       // that already carries the envelope's fog-colour blend.  Adding the
       // fog colour a second time tinted the highlights twice as strongly as
       // the rest of the viewmodel (visible colour seam between specular and
       // diffuse weapon sections).  They attenuate toward black instead,
       // leaving the fog colour to the base pass -- mirroring why GLSky.cpp
       // zeroes the envelope for the sun, which is likewise added over an
       // already-fogged layer.  Base draws (uTintByFogColor=0) keep the
       // regular fog-colour blend.
       vec3 envelopeColor = mix(uCamFogColor, vec3(0.0f), uTintByFogColor);
       finalColor = finalColor * (1.0f - camFog) + envelopeColor * camFog;
   }

   // Apply night lighting to world models only. Sky and moon use their own
   // night textures/shading and never pass through this multiplier.
   if (uNightStrength > 0.001f) {
       float gray = dot(finalColor, vec3(0.299, 0.587, 0.114));
       finalColor = mix(finalColor, vec3(gray), 0.6 * uNightStrength);
       finalColor *= mix(1.0, 0.5, uNightStrength);
   }
   FragColor = vec4(finalColor, texColor.a * vAlpha);
}
