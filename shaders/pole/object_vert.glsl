#version 330 core
// SCENE 4 (POWER LINES) object vertex shader: world position, normal,
// per-vertex colour and the MATERIAL ID that drives the per-surface detail
// in object_frag.glsl (wood grain, asphalt wear, siding, roof tiles, ...).
// Explicit attribute locations (0..4, matching DrawGrid's VAO pointers):
// AMD/NVIDIA do NOT assign locations in declaration order the way Mesa
// does — without a layout the object pass renders NOTHING (sky/HUD only),
// exactly the "no poles, no nothing" report from the user's AMD box.
//
// Locations 3/4 (material, alpha) were ADDED in BUILD-P7. DrawGrid and
// DrawGroundShadows both set up their OWN VAO with all five pointers —
// every buffer here is streamed/dynamic and a driver that bleeds one
// attribute into the other draw silently corrupts the whole mesh.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aMat;
layout(location = 4) in float aAlpha;

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vWorld;
out vec3 vNormal;
out vec3 vColor;
// BUILD-P7 CRITICAL: the material id MUST be flat. As a smooth varying it
// is perspective-correctly interpolated across the triangle, and a constant
// 12.0 does not survive that division intact — it arrives as 11.9999995, the
// `m == kMatShadow` test fails, and the shadow falls through to the generic
// path, which outputs alpha 1.0. With the shadow pass blended as
// dst*(1 - srcAlpha) that multiplies the road by ZERO: hard black rectangles
// lying across the street. The rounding of that division is driver-specific,
// so this is exactly the kind of bug that looks fine on one GPU and ships as
// black slabs on another. vAlpha stays SMOOTH on purpose — it is the
// penumbra gradient and genuinely wants interpolating.
flat out float vMat;
out float vAlpha;

void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    // uniform scale + rotation only: normal transform = model's 3x3
    vNormal = mat3(uModel) * aNormal;
    vColor = aColor;
    vMat = aMat;
    vAlpha = aAlpha;
    gl_Position = uViewProj * w;
}
