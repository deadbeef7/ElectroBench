// GENERATED from shaders/pole/object_vert.glsl by vitaport/tools/make_vita_shaders.py -- do not edit.
// GLSL 1.00-style rewrite for vitaGL/SceShaccCg (see that script's RULES); the
// math is identical to the desktop shader except for the documented rewrites.
#version 100


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

    vNormal = mat3(uModel[0].xyz, uModel[1].xyz, uModel[2].xyz) * aNormal;
    vColor = aColor;
    vMat = aMat;
    vAlpha = aAlpha;
    gl_Position = uViewProj * w;
}
