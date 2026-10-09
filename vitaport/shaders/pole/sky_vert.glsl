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
    vDir = w.xyz;
    gl_Position = uViewProj * w;
}
