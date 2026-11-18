#version 330 core

// Crown sheet for scene 3.
//
// The old crown was a smooth cone whose "fingers", "beads" and "streaks" were
// painted in screen-parametric sine patterns in the fragment shader, so it read
// as a striped lampshade. Here the silhouette itself carries the detail: the
// rim height is a ridged noise field around the circumference, so the wall
// breaks into a few wide sheets with thin strands between them and holes where
// a ligament has already torn away. The fragment shader only shades the
// geometry it is given.

layout(location = 0) in vec2 aAngleH;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uRadius;
uniform float uHeight;
uniform float uTime;
uniform float uSpike;
uniform float uPhase;

out vec3 vWorld;
out vec3 vNormal;
out float vParam;
out float vAngle;
out float vLigament;
out float vThin;

const float TAU = 6.28318530718;

float hash11(float n) { return fract(sin(n * 127.1 + 311.7) * 43758.5453); }

// Periodic 1-D value noise around the crown.
float angNoise(float a, float cells, float seed) {
    float x = a * cells;
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float n0 = hash11(mod(i, cells) + seed * 37.0);
    float n1 = hash11(mod(i + 1.0, cells) + seed * 37.0);
    return mix(n0, n1, f);
}

// Surface tension pulls the wall into a handful of wide sheets (low angular
// frequency) with thin strands between them (higher frequencies). The field
// drifts slowly, so the strands move with the sheet instead of flickering.
float ligamentField(float a, float t, float phase) {
    float drift = t * 0.055 + phase * 0.11;
    // Broad sheets dominate; higher frequencies only roughen their edges, so
    // the wall reads as water rather than a row of glass needles.
    float l = 0.72 * angNoise(a + drift, 6.0, 1.0)
            + 0.20 * angNoise(a - drift * 1.7, 12.0, 2.0)
            + 0.08 * angNoise(a + drift * 2.3, 19.0, 3.0);
    l = smoothstep(0.18, 0.78, l);
    // A coarser field punches the occasional tear through the wall.
    float gate = smoothstep(0.06, 0.34, angNoise(a - drift * 0.8, 4.0, 7.0));
    return l * gate;
}

// Point on the sheet: hp runs from the contact line (0) to the torn rim (1).
vec3 surfacePoint(float ang, float hp, float rim, float wobble) {
    float flare = 0.94 + 0.24 * hp + 0.05 * hp * hp;
    float r = uRadius * flare * wobble * (1.0 - 0.10 * hp * (1.0 - rim));
    float y = uHeight * hp * rim;
    return vec3(cos(ang) * r, y, sin(ang) * r);
}

void main() {
    float ang = aAngleH.x * TAU;
    float hp = aAngleH.y;
    float t = uTime + uPhase;

    float lig = ligamentField(ang, t, uPhase);
    float wobble = 1.0 + 0.055 * (angNoise(ang + t * 0.05, 17.0, 11.0) - 0.5);

    // Rim height: the wall stays tall across a sheet and dips between sheets.
    float rim = 0.55 + 0.45 * lig;
    rim += uSpike * 0.10 * pow(lig, 3.0)
         * (0.5 + 0.5 * angNoise(ang - t * 0.21, 29.0, 13.0));
    rim = clamp(rim, 0.0, 1.30);

    vec3 local = surfacePoint(ang, hp, rim, wobble);
    vec3 pos = uCenter + local;

    // Normal from finite differences on the actual sheet, so lighting follows
    // the torn silhouette instead of a cone of revolution.
    float e = 0.01;
    vec3 pA = surfacePoint(ang + e, hp, rim, wobble);
    // At the torn rim (hp = 1) the forward difference vanishes; step backward so
    // the tangent stays defined. A normalize(0) here produced NaNs that the
    // bloom pyramid spread across the whole frame.
    float hp2 = hp < 0.98 ? min(hp + 0.02, 1.0) : max(hp - 0.02, 0.0);
    vec3 pH = surfacePoint(ang, hp2, rim, wobble);
    vec3 n = cross(pH - local, pA - local);
    float nlen = length(n);
    n = nlen > 1e-8 ? n / nlen : vec3(cos(ang), 0.0, sin(ang));

    vWorld = pos;
    vNormal = n;
    vParam = hp;
    vAngle = aAngleH.x;
    vLigament = lig;
    vThin = 1.0 - lig;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
