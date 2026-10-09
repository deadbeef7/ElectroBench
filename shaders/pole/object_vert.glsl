#version 330 core


layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;
layout(location = 3) in float aMat;
layout(location = 4) in float aAlpha;

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vWorld;
out vec3 vNormal;
out vec3 vColor;


flat out float vMat;
out float vAlpha;

void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;

    vNormal = mat3(uModel) * aNormal;
    vColor = aColor;
    vMat = aMat;
    vAlpha = aAlpha;
    gl_Position = uViewProj * w;
}
