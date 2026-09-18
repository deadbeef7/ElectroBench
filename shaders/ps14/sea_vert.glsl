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
float waveHeight(vec2 p, float t) {
    float h = 0.0;
    h += sin(dot(p, vec2( 0.98,  0.20)) * 0.055 + t * 1.05) * 1.55;
    h += sin(dot(p, vec2(-0.64,  0.77)) * 0.083 + t * 1.35) * 1.05;
    h += sin(dot(p, vec2( 0.36, -0.93)) * 0.121 + t * 1.75) * 0.65;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.187 + t * 2.30) * 0.38;
    h += sin(dot(p, vec2( 0.59,  0.81)) * 0.283 + t * 2.90) * 0.22;
    h += sin(dot(p, vec2(-0.20,  0.98)) * 0.421 + t * 3.60) * 0.12;
    return h;
}

void main() {
    vec3 pos = vec3(aXZ.x, waveHeight(aXZ, uTime), aXZ.y);
    vWorld = pos;
    vUV = aUV;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
