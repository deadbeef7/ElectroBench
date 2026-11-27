#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec4 aTangent; // xyz + handedness
layout(location = 3) in vec2 aUV;
// One model matrix per instance (the 110 guns differ only by placement).
layout(location = 4) in vec4 iModel0;
layout(location = 5) in vec4 iModel1;
layout(location = 6) in vec4 iModel2;
layout(location = 7) in vec4 iModel3;

uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec4 vTangent;
out vec2 vUV;

void main() {
    mat4 model = mat4(iModel0, iModel1, iModel2, iModel3);
    vec4 wp = model * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    mat3 nm = mat3(model);
    vNormal = normalize(nm * aNormal);
    vTangent = vec4(normalize(nm * aTangent.xyz), aTangent.w);
    vUV = aUV;
    gl_Position = uViewProj * wp;
}
