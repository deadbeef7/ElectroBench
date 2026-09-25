#version 330 core
// Pool scene vertex shaders: shared tiny vertex shader for every object pass
// (water grid, sky dome, teapot, splash droplets). Positions arrive pre-built
// in object space; uModel places them in the world, uViewProj is camera
// view * projection. Normal matrices are cheap: the scene only rotates the
// teapot around Y and uniform-scales, so mat3(uModel) renormalised is exact.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vWorld;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    vUV = aUV;
    gl_Position = uViewProj * w;
}
