// GENERATED from shaders/ps14/hud_frag.glsl by vitaport/tools/make_vita_shaders.py -- do not edit.
// GLSL 1.00-style rewrite for vitaGL/SceShaccCg (see that script's RULES); the
// math is identical to the desktop shader except for the documented rewrites.
#version 100
precision mediump float;


varying vec2 vUV;

uniform sampler2D uAtlas;



void main() {
    float a = texture2D(uAtlas, vUV).r;
    a = smoothstep(0.25, 0.75, a);
    if (a < 0.02) discard;
    gl_FragColor = vec4(vec3(0.72, 0.93, 1.0), a * 0.9);
}
