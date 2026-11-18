#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent; // xyz + handedness
layout(location = 3) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    mat3 nm = mat3(uModel);
    vNormal = normalize(nm * aNormal);
    vTangent = vec4(normalize(nm * aTangent.xyz), aTangent.w);
    vUV = aUV;
    gl_Position = uViewProj * wp;
}
