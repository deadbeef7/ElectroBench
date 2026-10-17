// GENERATED from shaders/pole/sky_vert.glsl by vitaport/tools/make_vita_shaders.py -- do not edit.
// GLSL 1.00-style rewrite for vitaGL/SceShaccCg (see that script's RULES); the
// math is identical to the desktop shader except for the documented rewrites.
#version 100


attribute vec3 aPos;

uniform mat4 uViewProj;
uniform mat4 uModel;

varying vec3 vDir;

void main() {

    vec4 w = uModel * vec4(aPos, 1.0);
    // vDir is the unit sphere direction, NOT the dome's world position
    // (w.xyz). The dome is scaled to kDomeRadius (400) and recentred on the
    // eye, so w.xyz carries magnitudes near 400 into a *mediump* fragment
    // varying: the fragment's normalize(vDir - uEyePos) then evaluates
    // dot(X,X) ~ 480000, which overflows mediump (fp16's maximum is 65504),
    // inversesqrt returns 0, and normalize() hands back (0,0,0) for every
    // fragment. The whole sky then samples atmosphere() at one constant
    // direction: a flat colour with the clouds, cirrus and sun disc all
    // gated away - reproducible on any GLES stack that honours mediump as
    // fp16 (Mesa on the host and every phone), i.e. not device specific.
    // aPos is already the unit direction, exactly what scene 2's sky sends
    // (ps14/sky_vert.glsl) and what mediump can normalize.
    vDir = aPos;
    gl_Position = uViewProj * w;
}
