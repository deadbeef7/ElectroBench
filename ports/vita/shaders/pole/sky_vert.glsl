// Hand-ported from ElectroBench v0.6 shaders/pole/sky_vert.glsl for vitaGL /
// SceShaccCg (GLSL ES 1.00, shared by the Vita and Android GLES builds).
// ES 1.00 rewrites only: in/layout -> attribute, out -> varying, #version 100.
//
// vDir carries the dome's WORLD position because the v0.6 fragment stage
// evaluates the atmosphere at the eye (normalize(vDir - uEyePos)). It is highp
// on both sides: the dome radius is 400, and w.xyz magnitudes near 400 are fine
// at highp but overflow a mediump normalize() on hardware that really uses fp16.
#version 100
precision highp float;

attribute vec3 aPos;

uniform mat4 uViewProj;
uniform mat4 uModel;

varying highp vec3 vDir;

void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vDir = w.xyz;
    gl_Position = uViewProj * w;
}
