#version 330 core
// Vertex shader for the sea surface (shared wave equation with the fragment
// shader). The grid is displaced on the GPU by a sum of sine waves shaped to
// break into crests (the classic cheap stand-in for Gerstner waves used by
// 2001-era demos).

layout(location = 0) in vec2 aXZ; // world-space XZ position of the grid node
layout(location = 1) in vec2 aUV; // large-scale water UV

uniform mat4 uViewProj;
uniform float uTime;

out vec3 vWorld;
out vec2 vUV;

// NOTE: this is the DISPLACEMENT field. It is intentionally NOT identical to
// the fragment shader's normal field (see sea_frag.glsl): crests here are
// SHARPENED into choppy banks (smoothstep skew + a long swell passing under
// everything) while the fragment shader keeps smooth normals for its analytic
// lighting so the surface never faceting-alarms. If you retune one, check the
// other still agrees on scale/speed of each component.
float waveHeight(vec2 p, float t) {
    float h = 0.0;
    // big rolling swell passing under everything (visible banks, not lines)
    h += sin(dot(p, vec2(0.52, 0.30)) * 0.045 + t * 0.42) * 2.10;
    h += sin(dot(p, vec2(0.10, -0.49)) * 0.075 + t * 0.55) * 1.30;
    // primary chop, crest-skewed via smoothstep
    float c1 = sin(dot(p, vec2(0.98, 0.20)) * 0.170 + t * 1.30);
    h += smoothstep(-0.35, 1.0, c1) * 1.75 - 0.45;
    float c2 = sin(dot(p, vec2(-0.64, 0.77)) * 0.240 + t * 1.60);
    h += smoothstep(-0.35, 1.0, c2) * 1.05 - 0.28;
    // fast secondary chop stays a plain sine
    h += sin(dot(p, vec2(0.36, -0.93)) * 0.380 + t * 2.10) * 0.55;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.34;
    h += sin(dot(p, vec2(0.59, 0.81)) * 0.860 + t * 3.40) * 0.20;
    h += sin(dot(p, vec2(-0.20, 0.98)) * 1.450 + t * 4.40) * 0.11;
    return h;
}

void main() {
    vec2 xz = aXZ; // the patch is static; the camera orbits its centre
    vec3 pos = vec3(xz.x, waveHeight(xz, uTime), xz.y);
    vWorld = pos;
    vUV = aXZ / 4096.0; // world-anchored water UVs so waves/foam do not swim
    gl_Position = uViewProj * vec4(pos, 1.0);
}
