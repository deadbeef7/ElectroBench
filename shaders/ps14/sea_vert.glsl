#version 330 core
// Vertex shader for the sea surface (shared wave equation with the fragment
// shader). The grid is displaced on the GPU with a sum of sine waves, the
// classic cheap stand-in for Gerstner waves used by 2001-era demos.

layout(location = 0) in vec2 aXZ; // world-space XZ position of the grid node
layout(location = 1) in vec2 aUV; // large-scale water UV

uniform mat4 uViewProj;
uniform float uTime;
uniform vec2 uSeaCenter; // camera-following sea origin (snapped to 64m)

out vec3 vWorld;
out vec2 vUV;

// Must stay in sync with waveHeight() in sea_frag.glsl
// Chop everywhere: short wavelengths so several crests are always on screen
// (grid is 1024m across at 256x256, i.e. ~4m per quad).
float waveHeight(vec2 p, float t) {
    float h = 0.0;
    h += sin(dot(p, vec2( 0.98,  0.20)) * 0.170 + t * 1.30) * 0.68;
    h += sin(dot(p, vec2(-0.64,  0.77)) * 0.240 + t * 1.60) * 0.46;
    h += sin(dot(p, vec2( 0.36, -0.93)) * 0.380 + t * 2.10) * 0.34;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.22;
    h += sin(dot(p, vec2( 0.59,  0.81)) * 0.860 + t * 3.40) * 0.14;
    h += sin(dot(p, vec2(-0.20,  0.98)) * 1.450 + t * 4.40) * 0.08;
    return h;
}

void main() {
    vec2 xz = aXZ + uSeaCenter; // the grid travels with the camera
    vec3 pos = vec3(xz.x, waveHeight(xz, uTime), xz.y);
    vWorld = pos;
    vUV = aXZ / 1024.0; // world-anchored water UVs so waves/foam do not swim
    gl_Position = uViewProj * vec4(pos, 1.0);
    gl_Position = uViewProj * vec4(pos, 1.0);
}
