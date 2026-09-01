#pragma once

#include "Types.h"

// ---------------------------------------------------------------------------
// Shared camera-matrix builders and view-frustum culling.
//
// Both renderer backends compose the same column-major (OpenGL-convention)
// view / projection matrices; these helpers are the single source of truth so
// the CPU-side frustum used for culling always matches what is rasterized
// (each backend mirrors ApplyProjection/ApplyCameraTransform, the packet
// backend transforms vertices with these matrices directly).
// ---------------------------------------------------------------------------

// The six frustum planes as (a,b,c,d), normalized, with ax+by+cz+d >= 0 for
// points on the visible side.
struct FrustumPlanes
{
    float p[6][4];
};

// out = a * b (column-major 4x4). out may alias a or b.
void Frustum_Mult4x4(float out[16], const float a[16], const float b[16]);

// Perspective projection (glFrustum-equivalent, symmetric).
void Frustum_BuildPerspective(float out[16], float fovyDeg, float aspect, float zNear, float zFar);

// Right-handed look-at view matrix from a Camera3D pose.
void Frustum_BuildLookAt(float out[16], const Camera3D& camera);

// Extract the six clip planes from a combined view-projection matrix
// (Gribb–Hartmann). Planes are normalized for distance tests.
void Frustum_FromViewProj(FrustumPlanes* out, const float vp[16]);

// Conservative sphere-vs-frustum test: false only when the sphere is fully
// outside at least one plane (i.e. definitely invisible).
bool Frustum_SphereVisible(const FrustumPlanes* f, const Vector3& worldCenter, float worldRadius);

// Axis-aligned bounding box, world space.
struct Aabb3
{
    Vector3 min;
    Vector3 max;
};

// Conservative AABB-vs-frustum test (positive-vertex method): false only when the
// box is fully outside at least one plane. Used to cull whole sectors.
bool Frustum_AabbVisible(const FrustumPlanes* f, const Aabb3& box);

// Conservative world-space bounding sphere for an object-space sphere
// (objCenter/objRadius) under a translation + non-uniform scale. The center
// offset is folded into the radius, so the result is rotation-independent and
// needs no rotation matrix.
void Frustum_WorldSphere(const Vector3& position, const Vector3& scale, const Vector3& objCenter, float objRadius, Vector3* outCenter, float* outRadius);
