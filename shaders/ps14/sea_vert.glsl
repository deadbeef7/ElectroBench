// GENERATED from shaders/ps14/sea_vert.glsl by vitaport/tools/make_vita_shaders.py -- do not edit.
// GLSL 1.00-style rewrite for vitaGL/SceShaccCg (see that script's RULES); the
// math is identical to the desktop shader except for the documented rewrites.
#version 100
// Re-seeded from the v0.3 release (tag v0.3: shaders/ps14/sea_vert.glsl) and
// re-ported through the same rewrites. The only deltas beyond the mechanical
// rewrite are the shared-tree fixes the ports need on ES 3.0 linkers, noted
// inline below and in sea_frag.glsl.
// Vertex shader for the sea surface (shared wave equation with the fragment
// shader). The grid is displaced on the GPU by a sum of sine waves shaped to
// break into crests (the classic cheap stand-in for Gerstner waves used by
// 2001-era demos).

attribute vec2 aXZ; // world-space XZ position of the grid node
attribute vec2 aUV; // large-scale water UV

uniform mat4 uViewProj;
uniform float uTime;

// highp explicitly because this varying carries a *world* position across the
// 4096-unit sea patch (+/-2048). The fragment stage reads it at its own
// default mediump, which cannot hold those coordinates (mediump tops out at
// 16384): the far half of the patch collapsed and painted the near-black band
// between sea and sky that the README records. Declared highp on both sides
// the stages agree explicitly, which is also what the ES 3.0 linker checks
// (mismatching precision on a shared varying/uniform fails the link). A
// vertex float already defaults to highp; this only makes it explicit.
varying highp vec3 vWorld;
varying vec2 vUV;

// NOTE: this is the DISPLACEMENT field. It is intentionally NOT identical to
// the fragment shader's normal field (see sea_frag.glsl): crests here are
// SHARPENED into choppy banks (smoothstep skew + a long swell passing under
// everything) while the fragment shader keeps smooth normals for its analytic
// lighting so the surface never faceting-alarms. If you retune one, check the
// other still agrees on scale/speed of each component.
float waveHeight(vec2 p, float t) {
    float h = 0.0;
    // big rolling swell passing under everything (visible banks, not lines)
    h += sin(dot(p, vec2(0.52, 0.30)) * 0.045 + t * 0.42) * 2.10;
    h += sin(dot(p, vec2(0.10, -0.49)) * 0.075 + t * 0.55) * 1.30;
    // primary chop, crest-skewed via smoothstep
    float c1 = sin(dot(p, vec2(0.98, 0.20)) * 0.170 + t * 1.30);
    h += smoothstep(-0.35, 1.0, c1) * 1.75 - 0.45;
    float c2 = sin(dot(p, vec2(-0.64, 0.77)) * 0.240 + t * 1.60);
    h += smoothstep(-0.35, 1.0, c2) * 1.05 - 0.28;
    // fast secondary chop stays a plain sine
    h += sin(dot(p, vec2(0.36, -0.93)) * 0.380 + t * 2.10) * 0.55;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.34;
    h += sin(dot(p, vec2(0.59, 0.81)) * 0.860 + t * 3.40) * 0.20;
    h += sin(dot(p, vec2(-0.20, 0.98)) * 1.450 + t * 4.40) * 0.11;
    return h;
}

void main() {
    vec2 xz = aXZ; // the patch is static; the camera orbits its centre
    vec3 pos = vec3(xz.x, waveHeight(xz, uTime), xz.y);
    vWorld = pos;
    vUV = aXZ / 4096.0; // world-anchored water UVs so waves/foam do not swim
    gl_Position = uViewProj * vec4(pos, 1.0);
}
