// Hand-ported from ElectroBench v0.6 shaders/ps14/sea_vert.glsl for vitaGL /
// SceShaccCg (GLSL ES 1.00, shared by the Vita and Android GLES builds).
//
// The displacement is the SAME sum of waves the fragment shader differentiates
// analytically, band-limited in two ways so the drawn surface and its normals
// agree: distance (a wave below a few pixels fades out) and grid (a wave shorter
// than a few quads cannot be displaced and survives only in the fragment normal).
//
// ES 1.00 rewrites, and only these:
//   * in/layout -> attribute, out -> varying, #version 100 + precision;
//   * uEyePos / uGridCell / vDist added: v0.6 needs the per-vertex distance for
//     the band limit, which the v0.3 port computed nowhere. The C++ uploads both.
// The math is v0.6's, unchanged.
#version 100
precision highp float;

attribute vec2 aXZ; // world-space XZ position of the grid node
attribute vec2 aUV; // large-scale water UV

uniform mat4 uViewProj;
// highp on both stages: these are shared with the fragment shader, whose float
// default is highp here, and an ES 3.0 linker rejects mismatched precision.
uniform highp float uTime;
uniform highp vec3 uEyePos;
uniform highp float uGridCell; // world size of one quad (m)

varying highp vec3 vWorld;
varying highp vec2 vUV;
varying highp float vDist;

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
