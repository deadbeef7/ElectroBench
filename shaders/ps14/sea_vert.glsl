#version 330 core
// Vertex shader for the sea surface (shared wave equation with the fragment
// shader). The grid is displaced on the GPU with a sum of sine waves, the
// classic cheap stand-in for Gerstner waves used by 2001-era demos.

layout(location = 0) in vec2 aXZ; // world-space XZ position of the grid node
layout(location = 1) in vec2 aUV; // large-scale water UV

uniform mat4 uViewProj;
uniform float uTime;

out vec3 vWorld;
out vec2 vUV;

// Must stay in sync with waveHeight() in sea_frag.glsl
// Chop everywhere: short wavelengths so several crests are always on screen
// (grid is 4096m across at 256x256, i.e. ~16m per quad; the fine shading
// detail comes from the per-pixel analytic normals in the fragment shader).
float waveHeight(vec2 p, float t) {
    float h = 0.0;
    h += sin(dot(p, vec2( 0.98,  0.20)) * 0.170 + t * 1.30) * 0.95;
    h += sin(dot(p, vec2(-0.64,  0.77)) * 0.240 + t * 1.60) * 0.64;
    h += sin(dot(p, vec2( 0.36, -0.93)) * 0.380 + t * 2.10) * 0.48;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.31;
    h += sin(dot(p, vec2( 0.59,  0.81)) * 0.860 + t * 3.40) * 0.20;
    h += sin(dot(p, vec2(-0.20,  0.98)) * 1.450 + t * 4.40) * 0.11;
    return h;
}

void main() {
    vec2 xz = aXZ; // the patch is static; the camera orbits its centre
    vec3 pos = vec3(xz.x, waveHeight(xz, uTime), xz.y);
    vWorld = pos;
    vUV = aXZ / 4096.0; // world-anchored water UVs so waves/foam do not swim
    gl_Position = uViewProj * vec4(pos, 1.0);
}
