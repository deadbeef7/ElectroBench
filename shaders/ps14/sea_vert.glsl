#version 330 core
// Sea surface (vertex stage).
//
// The displacement is the SAME sum of waves the fragment shader differentiates
// analytically, band-limited in two ways so the drawn surface and its normals
// agree:
//
//   * distance: a wave whose wavelength drops below a few pixels cannot be
//     drawn without aliasing, so it fades out before it gets there. This is
//     what removes the moire/faceting the old 384-quad mesh showed at grazing
//     angles, where the short chop was displaced but unresolved;
//   * grid: a wave shorter than a few grid cells cannot be displaced at all
//     (4 m quads here), so it is dropped from the geometry and survives only in
//     the fragment normal, where it is cheap and never jags.
//
// If you retune the wave table, retune sea_frag.glsl's table the same way.

layout(location = 0) in vec2 aXZ; // world-space XZ position of the grid node
layout(location = 1) in vec2 aUV; // large-scale water UV

uniform mat4  uViewProj;
uniform float uTime;
uniform vec3  uEyePos;
uniform float uGridCell; // world size of one quad (m)

out vec3  vWorld;
out vec2  vUV;
out float vDist;

const float kWavePixelAngle = 0.0018; // radians per pixel at the reference FOV

// Resolvability of a component of wavenumber k (rad/m) at this distance.
float bandFade(float k, float dist) {
    float lambda = 6.2831853 / k;
    float pixelFade = smoothstep(1.5, 4.0, lambda / max(dist * kWavePixelAngle, 1e-3));
    float gridFade = smoothstep(2.2, 4.2, lambda / max(uGridCell, 1e-3));
    return pixelFade * gridFade;
}

float waveHeight(vec2 p, float t, float dist) {
    float h = 0.0;
    float f;

    // Long swell passing under everything: always resolved, no fade needed.
    h += sin(dot(p, vec2(0.52, 0.30)) * 0.045 + t * 0.42) * 2.10;
    h += sin(dot(p, vec2(0.10, -0.49)) * 0.075 + t * 0.55) * 1.30;

    // Crest-sharpened chop. Monotonic like the fragment's gradient: no
    // saturated flat top, which used to turn every crest into a mirror plate.
    f = bandFade(0.170, dist);
    float c1 = sin(dot(p, vec2(0.98, 0.20)) * 0.170 + t * 1.30);
    h += 1.45 * f * (c1 + 0.16 * c1 * abs(c1));

    f = bandFade(0.240, dist);
    float c2 = sin(dot(p, vec2(-0.64, 0.77)) * 0.240 + t * 1.60);
    h += 0.85 * f * (c2 + 0.16 * c2 * abs(c2));

    h += sin(dot(p, vec2(0.36, -0.93)) * 0.380 + t * 2.10) * 0.55 * bandFade(0.380, dist);
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.34 * bandFade(0.540, dist);
    h += sin(dot(p, vec2(0.71, -0.70)) * 0.620 + t * 2.95) * 0.28 * bandFade(0.620, dist);
    return h;
}

void main() {
    vDist = length(aXZ - uEyePos.xz);
    vec3 pos = vec3(aXZ.x, waveHeight(aXZ, uTime, vDist), aXZ.y);
    vWorld = pos;
    vUV = aXZ / 4096.0; // world-anchored UVs so ripples do not swim with the mesh
    gl_Position = uViewProj * vec4(pos, 1.0);
}
