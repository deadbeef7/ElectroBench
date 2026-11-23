// GENERATED from shaders/ps14/sky_vert.glsl by vitaport/tools/make_vita_shaders.py -- do not edit.
// GLSL 1.00-style rewrite for vitaGL/SceShaccCg (see that script's RULES); the
// math is identical to the desktop shader except for the documented rewrites.
#version 100


attribute vec3 aPos;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uRadius;

// highp on both sides: the fragment stage derives the sun's angular distance
// from this direction, and at mediump the fp16 mantissa is coarser than the
// sun's own halo (the concentric "ghost suns" around the disc).
varying highp vec3 vDir;

void main() {
    vDir = aPos;
    vec3 pos = aPos * uRadius + uCenter;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
