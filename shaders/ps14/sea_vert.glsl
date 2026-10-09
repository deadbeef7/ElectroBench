#version 330 core


layout(location = 0) in vec2 aXZ;
layout(location = 1) in vec2 aUV;

uniform mat4 uViewProj;
uniform float uTime;

out vec3 vWorld;
out vec2 vUV;


float waveHeight(vec2 p, float t) {
    float h = 0.0;

    h += sin(dot(p, vec2(0.52, 0.30)) * 0.045 + t * 0.42) * 2.10;
    h += sin(dot(p, vec2(0.10, -0.49)) * 0.075 + t * 0.55) * 1.30;

    float c1 = sin(dot(p, vec2(0.98, 0.20)) * 0.170 + t * 1.30);
    h += smoothstep(-0.35, 1.0, c1) * 1.75 - 0.45;
    float c2 = sin(dot(p, vec2(-0.64, 0.77)) * 0.240 + t * 1.60);
    h += smoothstep(-0.35, 1.0, c2) * 1.05 - 0.28;

    h += sin(dot(p, vec2(0.36, -0.93)) * 0.380 + t * 2.10) * 0.55;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.34;
    h += sin(dot(p, vec2(0.59, 0.81)) * 0.860 + t * 3.40) * 0.20;
    h += sin(dot(p, vec2(-0.20, 0.98)) * 1.450 + t * 4.40) * 0.11;
    return h;
}

void main() {
    vec2 xz = aXZ;
    vec3 pos = vec3(xz.x, waveHeight(xz, uTime), xz.y);
    vWorld = pos;
    vUV = aXZ / 4096.0;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
