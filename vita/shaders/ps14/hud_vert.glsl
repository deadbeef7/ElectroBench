// GENERATED from shaders/ps14/hud_vert.glsl by vitaport/tools/make_vita_shaders.py -- do not edit.
// GLSL 1.00-style rewrite for vitaGL/SceShaccCg (see that script's RULES); the
// math is identical to the desktop shader except for the documented rewrites.
#version 100


attribute vec2 aPos;
attribute vec2 aUV;

uniform vec2 uResolution;

varying vec2 vUV;

void main() {
    // `ndc`, not `clip`: under SceShaccCg `clip` shadows the Cg builtin and the
    // compiler says so ("'clip' shadows previous declaration" / "previous
    // declaration was builtin"). It compiles either way, but a name that collides
    // with a builtin in the language the shader is translated into is not worth
    // keeping.
    vec2 ndc = vec2(aPos.x / uResolution.x * 2.0 - 1.0,
                    1.0 - aPos.y / uResolution.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
}
