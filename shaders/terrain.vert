#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in float aLayer;
layout (location = 3) in vec4 aLightFogAlpha;
layout (location = 4) in vec3 aFogColor;
uniform PerFrame {
   mat4 uProjection;
   vec2 uFogRange;          // (fadeStart, distance)
   vec3 uDistanceFogColor;
   float uForceFog;
   vec3 uFogColor;
   mat4 uView;
   vec4 uWaterAlphaFade;    // x=start, y=end, z=enabled, w=fade step
   float uWaterDepthFactor; // 0 at surface, 1 at max depth
   float uCloudCover;       // 0=clear sun, 1=overcast (cloud colour temp)
};
out vec2 vTexCoord;
flat out int vLayer;
out float vLight;
out float vFog;
out vec3 vFogColor;
out float vAlpha;
out float vViewZ;
out float vViewDistance;
out float vWaterAlphaFade;
out vec3 vViewPos;          // view-space vertex position (camera at origin)
out float vRadialDist;       // radial (Euclidean) camera distance; drives the
                            // horizon distance-fog ramp so it matches the CPU
                            // alpha fade (CalcTerrainAlpha), which is radial.
void main() {
   gl_Position = uProjection * vec4(aPos, 1.0);
   vTexCoord = aTexCoord;
   vLayer = int(aLayer + 0.5);
   // uint8 attributes are normalized to [0,1] by the driver.
   vLight = aLightFogAlpha.x;
   vFog   = aLightFogAlpha.y;
   vAlpha = aLightFogAlpha.z;
   vFogColor = aFogColor;
   vViewZ = max(-aPos.z, 0.0);
   vRadialDist = length(aPos);
   // Use dot(aPos,aPos) instead of length(aPos) to avoid
   // sqrt per vertex. The fragment shader computes sqrt only for
   // water pixels (the minority).
   vViewDistance = dot(aPos, aPos);
   vWaterAlphaFade = aLightFogAlpha.w;
   vViewPos = aPos;          // already in view space (uProjection * aPos)
}
