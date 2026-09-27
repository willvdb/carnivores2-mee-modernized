// ============================================================================
// SkyFogProjection.h -- Legacy sky-plane projection coefficients
// ============================================================================
#pragma once

#include "Core/MathTypes.h"

namespace skyfog {

// Coefficients used by the projected sky texture mapping. The sky shader uses
// q/p/r for its perspective-correct UVs and, separately, for the inherited
// scanline-width fog approximation.
struct ProjectionCoefficients {
    Vector3d q;
    Vector3d p;
    Vector3d r;
};

inline ProjectionCoefficients BuildProjectionCoefficients(
    const Vector3d& normal,
    const Vector3d& tangentX,
    const Vector3d& tangentY,
    float planeP,
    float ddx,
    float ddy,
    float cameraW,
    float cameraH)
{
    ProjectionCoefficients coefficients = {
        {
            cameraH * normal.x,
            cameraW * normal.y,
            cameraW * cameraH * normal.z,
        },
        {
            planeP * cameraH * tangentX.x,
            planeP * cameraW * tangentX.y,
            planeP * cameraW * cameraH * tangentX.z,
        },
        {
            planeP * cameraH * tangentY.x,
            planeP * cameraW * tangentY.y,
            planeP * cameraW * cameraH * tangentY.z,
        },
    };

    coefficients.p.x -= ddx * coefficients.q.x;
    coefficients.p.y -= ddx * coefficients.q.y;
    coefficients.p.z -= ddx * coefficients.q.z;
    coefficients.r.x -= ddy * coefficients.q.x;
    coefficients.r.y -= ddy * coefficients.q.y;
    coefficients.r.z -= ddy * coefficients.q.z;
    return coefficients;
}

} // namespace skyfog
