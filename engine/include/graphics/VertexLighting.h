#pragma once

#include "Primitives.h"

// Ambient + dynamic-light Lambertian shading for one world-space vertex,
// shared by vertex-lit-tier backends that must compose baked static lighting
// with dynamic lights themselves rather than delegate the whole thing to
// real fixed-function hardware/library lighting. A fixed-function light
// equation multiplies a per-vertex "diffuse material" by each light's
// contribution -- correct when that per-vertex value is a raw, unlit
// material colour (a primitive's flat tint, say), but wrong when it is
// already the level compiler's baked, fully-lit result: multiplying a dark
// baked shadow by a new dynamic light's contribution would keep it dark no
// matter how bright that light is, permanently blocking anything from ever
// relighting it. See docs/subsystems/RENDERER.md, "Static vs. dynamic
// lighting", and docs/formats/MATERIAL_FORMAT.md.
//
// out = baseline + ambient + sum_of_active_lights(NdotL * intensity *
// attenuation * lightColor) -- added on top of the baseline, never
// multiplied into a sum that starts at zero. `baseline` is the level
// compiler's baked colour for sector geometry, or full white for dynamic
// geometry (models, primitives) with nothing baked yet -- this is what
// keeps a scene with no dynamic lights configured looking exactly as it did
// before any of this existed, rather than going black.
// @param worldPos World-space vertex position (point-light distance/direction).
// @param worldNormal World-space vertex normal; a zero-length normal (no
//        normal data, e.g. screen-space geometry) skips lighting entirely
//        and passes `baseline` through unlit.
// @param baseline RGBA -- the vertex's own baked colour, or {1,1,1,1}.
// @param lights `lightCount`-long; a slot with intensity <= 0 is off.
// @param ambient The scene's dynamic ambient term (Renderer::SetAmbientLight).
// @param outColor Receives the result, unclamped -- the caller quantizes
//        (and must clamp before doing so; this never wraps a colour, but it
//        also never assumes what range the caller's format needs).
void VertexLighting_Compute(const Vector3& worldPos, const Vector3& worldNormal, const float baseline[4], const Light3D* lights, uint32_t lightCount, const Color3& ambient, float outColor[4]);
