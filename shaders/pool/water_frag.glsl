#version 330 core
// Pool scene water. One fragment shader handles:
//   * the checkerboard sky reflected through an analytic mirror ray into the
//     SAME checker function the sky dome uses (identical pattern, exact
//     alignment across the horizon — the pool-room illusion),
//   * the hidden-light specular highlight (the only visible evidence of the
//     light) with Blinn-Phong sheen and sun-strength falloff,
//   * bobbing ripple rings from the teapot splash: up to N concurrent rings,
//     positions/amplitudes streamed as uniforms from the scene module,
//   * soft subsurface-ish body colour and distance haze into the sky tint.

#define MAX_RINGS 6

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uTileA;
uniform vec3 uTileB;
uniform float uTime;
uniform int   uRingCount;
uniform vec4  uRings[MAX_RINGS]; // xy = centre (world), z = radius, w = strength 0..1

out vec4 fragColor;

vec3 normalize3(vec3 v) { return v / max(length(v), 1e-5); }

// ---- the SAME checker as sky_frag.glsl (copy kept intentional: one file
// ---- cannot include the other without extension support on all drivers)
float checker(vec2 p) {
    vec2 w = fract(p) - 0.5;
    vec2 a = abs(fract(p * 0.5) - 0.5) / fwidth(p * 0.5);
    vec2 fade = clamp(a * 1.6 - 0.5, 0.0, 1.0);
    float cw = min(fade.x, fade.y);
    return mix(step(dot(w, w), 0.25), 0.5, cw);
}

// reflected ray into the dome-space checker, evaluated exactly like the sky:
// mirror the view ray about the water plane, then unroll like the dome does.
vec3 reflectedCheckerColor(vec3 dirToViewer, vec3 pos, float rippleBump) {
    vec3 rd = reflect(dirToViewer, normalize(vec3(0.0, 1.0, 0.0)) + vec3(rippleBump, 0.0, rippleBump) * 0.35);
    rd.y = abs(rd.y) * 0.85 + 0.02;      // keep rays skimming upward-ish
    float up = clamp(rd.y, 0.02, 1.0);
    // gnomic unroll with the SAME 1.05 gain as the sky dome, then the same
    // 0.16-cell checker scale — tiles line up across the horizon line
    vec2 plane = rd.xz / up * 1.05;
    float c = checker(plane / 0.16);
    vec3 albedo = mix(uTileB, uTileA, c);
    // Water reflects its surroundings strongly at ALL angles (water's Fresnel
    // only kills the reflection at very steep views, and even there R~0.02
    // against a BRIGHT sky still wins over the dark body). Keep most of the
    // tile colour: brightness modulated mildly by view angle.
    float fres = 0.35 + 0.65 * pow(1.0 - clamp(dot(-dirToViewer, vec3(0.0, 1.0, 0.0)), 0.0, 1.0), 1.5);
    return albedo * fres;
}

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    float dist01 = clamp(length(uEyePos - vWorld) / 120.0, 0.0, 1.0); // for body depth

    // --- hidden light specular: the only light you ever see ---------------
    vec3 H = normalize(V + normalize(uLightDir));
    float spec = pow(clamp(dot(vec3(0.0, 1.0, 0.0), H), 0.0, 1.0), 220.0);
    float sheen = pow(clamp(dot(vec3(0.0, 1.0, 0.0), H), 0.0, 1.0), 14.0);

    // --- splash rings: expand + fade, disturb the reflection ---------------
    float bump = 0.0;
    float foam = 0.0;
    for (int i = 0; i < MAX_RINGS; i++) {
        if (i >= uRingCount) break;
        vec4 r = uRings[i];
        float d = length(vWorld.xz - r.xy);
        float band = d - r.z;
        float width = 0.30 + r.z * 0.05;
        float ring = exp(-band * band / (width * width));
        bump += ring * r.w * 0.55;
        foam  += ring * r.w;
    }
    bump = clamp(bump, 0.0, 1.0);
    foam = clamp(foam, 0.0, 1.0);

    // --- colour ------------------------------------------------------------
    vec3 refl = reflectedCheckerColor(-V, vWorld, bump);
    // BLUE water body: a saturated pool-water blue, deeper with distance.
    // Tinted slightly toward uTileA so the water and sky feel like one room.
    vec3 body = mix(vec3(0.030, 0.180, 0.320), vec3(0.010, 0.090, 0.200),
                    clamp(dist01, 0.0, 1.0));

    // Fresnel-correct mix: grazing angles (far water) mirror the sky hard,
    // steep angles (near camera) show the BLUE body through. Without this the
    // coral tiles' reflections out-shout the blue everywhere and the whole
    // pool reads orange.
    float mirror = 0.28 + 0.62 * pow(1.0 - clamp(V.y, 0.0, 1.0), 1.6);
    vec3 col = mix(body, refl, clamp(mirror, 0.0, 1.0));
    col += uLightTint * (spec * 2.4 + sheen * 0.35);  // the hidden light
    col += vec3(0.9) * foam * 0.22;                   // foam brightening
    col += uTileA * 0.06;                             // ambient skylight

    // haze toward the horizon blends water into the sky glow — tinted
    // turquoise so the far water stays blue instead of greying out
    float dist = length(uEyePos - vWorld);
    float haze = 1.0 - exp(-dist * 0.004);
    vec3 hazeCol = normalize3(mix(uLightTint, uTileA, 0.8)) * 0.75;
    col = mix(col, hazeCol, haze * 0.6);

    // The reflected sky colour is ALREADY tonemapped+gamma'd by sky_frag; a
    // second knee here desaturated everything to grey. Just clamp + a mild
    // gamma trim so bright reflections keep their tile colours.
    col = clamp(col, 0.0, 1.0);
    col = pow(col, vec3(1.0 / 1.15));
    fragColor = vec4(col, 1.0);
}
