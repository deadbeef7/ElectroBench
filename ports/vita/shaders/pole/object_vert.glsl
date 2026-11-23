// Hand-ported from ElectroBench v0.6 shaders/pole/object_vert.glsl for vitaGL /
// SceShaccCg (GLSL ES 1.00, shared by the Vita and Android GLES builds).
// ES 1.00 rewrites only: in/layout -> attribute, out -> varying, #version 100.
#version 100
precision highp float;

attribute vec3 aPos;
attribute vec3 aNormal;
attribute vec3 aColor;
attribute float aMat;
attribute float aAlpha;

uniform mat4 uViewProj;
uniform mat4 uModel;

varying vec3 vWorld;
varying vec3 vNormal;
varying vec3 vColor;
varying float vMat;
varying float vAlpha;

void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;

    vNormal = normalize(mat3(uModel) * aNormal);
    vColor = aColor;
    vMat = aMat;
    vAlpha = aAlpha;
    gl_Position = uViewProj * w;
}
