#version 330 core
// Pool scene water. One fragment shader handles:
//   * the checkerboard sky reflected through an analytic mirror ray into the
//     SAME checker function the sky dome uses (identical pattern, exact
//     alignment across the horizon — the pool-room illusion),
//   * the hidden-light specular highlight (the only visible evidence of the
//     light) with Blinn-Phong sheen and sun-strength falloff,
//   * bobbing ripple rings from the teapot fleet: up to MAX_RINGS concurrent
//     rings, streamed as uniforms from the scene module (each splash owns a
//     private window of slots so nine pots splashing at once never overwrite
//     each other's ripples),
//   * soft subsurface-ish body colour and distance haze into the sky tint.

#define MAX_RINGS 30

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uTileA;
uniform vec3 uTileB;
uniform float uTime;
uniform vec4  uRings[MAX_RINGS]; // xy = centre (world), z = radius, w = strength 0..1

out vec4 fragColor;

vec3 normalize3(vec3 v) { return v / max(length(v), 1e-5); }

// ---- the SAME checker as sky_frag.glsl (copy kept intentional: one file
// ---- cannot include the other without extension support on all drivers)
float checker(vec2 p) {
    // TRUE alternating checkerboard (analytically box-filtered, iq-style) —
    // the SAME function as sky_frag.glsl so the water mirror lines up with
    // the ceiling tile for tile. The old test (dot(w,w) <= 0.25) lit a disc
    // per cell, not a checker. Equal red/white squares, antialiased.
    vec2 w = fwidth(p) + 1e-4;
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5)
                  - abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}

// reflected ray into the dome-space checker: mirror the view ray about the
// water plane, then map through the SAME ANGULAR GRID as sky_frag.glsl —
// tile columns line up across the horizon like a real room.
vec3 reflectedCheckerColor(vec3 dirToViewer, vec3 pos, float rippleBump) {
    vec3 rd = reflect(dirToViewer, normalize(vec3(0.0, 1.0, 0.0)) + vec3(rippleBump, 0.0, rippleBump) * 0.35);
    rd.y = abs(rd.y) * 0.85 + 0.02;      // keep rays skimming upward-ish
    float up = clamp(rd.y, 0.02, 1.0);
    vec2 plane = vec2(atan(rd.x, rd.z), asin(clamp(up, -1.0, 1.0))) * 4.0;
    float c = checker(plane);
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

    // --- splash rings: expand + fade, disturb the reflection ---------------
    float bump = 0.0;
    float foam = 0.0;
    for (int i = 0; i < MAX_RINGS; i++) {
        vec4 r = uRings[i];
        if (r.w <= 0.001) continue;   // dead slot (rings are per-pot windows now)
        float d = length(vWorld.xz - r.xy);
        float band = d - r.z;
        float width = 0.30 + r.z * 0.05;
        float ring = exp(-band * band / (width * width));
        bump += ring * r.w * 0.55;
        foam  += ring * r.w;
        // Residual foam trail: a wide, WEAK halo behind the expanding ring
        // (a decaying wake that dissolves with the ring's own strength —
        // tame, so the pool still reads blue between impacts).
        float halo = exp(-band * band / (width * width * 14.0));
        foam += halo * r.w * r.w * 0.22;
    }
    bump = clamp(bump, 0.0, 1.0);
    foam = clamp(foam, 0.0, 1.0);

    // HYPER-REAL AMBIENT MICRO-CHOP: real pool water never sits glass-flat
    // between splashes — a faint wind/return-wave chop keeps the surface
    // alive, modulating both the mirror ray and the highlight. Subtle by
    // design (0.05 bump), two crossing moving sin fields, no textures.
    float chop = sin(vWorld.x * 7.3 + uTime * 2.1) * sin(vWorld.z * 6.1 - uTime * 1.7);
    chop = 0.5 + 0.5 * chop;
    bump = clamp(bump + chop * 0.05, 0.0, 1.0);

    // --- hidden light specular: the only light you ever see ---------------
    // GGX microfacet answer instead of two hard Blinn lobes: ONE energy-true
    // highlight with a physical long tail of grazing glints off ripple slopes.
    // A fixed view normal (0,1,0) is right here: the water plane is flat, the
    // ripples only perturb the reflection ray.
    vec3 H = normalize(V + normalize(uLightDir));
    float NdH = max(dot(vec3(0.0, 1.0, 0.0), H), 0.0);
    float aGGX = mix(0.055, 0.16, bump);        // ripples roughen the surface
    float a2 = aGGX * aGGX;
    float dGGX = a2 / (3.14159265 * pow(NdH * NdH * (a2 - 1.0) + 1.0, 2.0));
    float fres = pow(1.0 - clamp(dot(vec3(0.0, 1.0, 0.0), V), 0.0, 1.0), 5.0);
    float Fk = 0.02 + 0.98 * fres;
    float spec = min(dGGX, 6.0) * Fk * 0.25;    // capped: an unbounded GGX peak minted white fireflies on ripple slopes
    // the hidden light FALLS OFF with distance from the viewer side of the
    // pool (inverse-square-ish over the room scale) — far water's sheen dims
    float lightFall = 1.0 - 0.45 * dist01;
    spec *= lightFall;
    float sheen = pow(NdH, 14.0) * 0.35 * lightFall; // broad faint glow floor

    // --- colour ------------------------------------------------------------
    vec3 refl = reflectedCheckerColor(-V, vWorld, bump);
    // BLUE water body: a saturated pool-water blue, deeper with distance.
    // Tinted slightly toward uTileA so the water and sky feel like one room.
    vec3 body = mix(vec3(0.045, 0.210, 0.360), vec3(0.012, 0.105, 0.225),
                    clamp(dist01, 0.0, 1.0));

    // Fresnel-correct mix: reflections are now nearly PERCEPTIBLE-FREE per
    // the user — 0.035 of the tile colour is a faint sheen that hints the
    // ceiling is mirrored without painting tiles on the water.
    float mirror = 0.035;
    vec3 col = mix(body, refl, clamp(mirror, 0.0, 1.0));
    // Water absorbs red as light travels through it: even the REFLECTED
    // light that skirts the surface picks up a cool cast, which keeps the
    // pool reading blue at plane-level views instead of warm-pink.
    col *= vec3(0.86, 0.99, 1.09);
    col += uLightTint * (spec * 1.6 + sheen * 0.25 + chop * 0.018);  // the hidden light (tamed) + chop shimmer
    col += vec3(0.90, 0.94, 1.0) * foam * 0.22;       // foam brightening (cool white, sits in the room)
    col += vec3(0.05, 0.004, 0.005);                  // ambient skylight (red room)

    // --- caustics: the hidden light focuses through the curved crown walls
    // and jet columns into bright webbed shafts on the water. Two crossing
    // animated trig webs, gated to the rings' bump so caustics LIVE only
    // where the fleet is disturbing the surface, and modulated by ring
    // strength through bump. No texture fetches: analytic, like everything
    // else in this scene.
    vec2 cp = vWorld.xz * 3.1;
    float web1 = 0.5 + 0.5 * sin(cp.x + sin(cp.y * 1.7 + uTime * 1.9) * 1.4);
    float web2 = 0.5 + 0.5 * sin(cp.y * 1.3 - uTime * 1.4 + sin(cp.x * 1.9 - uTime * 0.8) * 1.4);
    float caustic = pow(web1 * web2, 3.0);
    // FINAL PASS: light dancing through the water volume — the body shimmers
    // faintly with the same caustic web the surface shows, brightest near the
    // camera where the volume is shallow and readable.
    float bodyShimmer = max(web1 * web2 - 0.25, 0.0) * (1.0 - dist01) * 0.10;
    col += uLightTint * caustic * (0.10 + 0.55 * bump) * lightFall;
    col += uLightTint * bodyShimmer;                  // volume shimmer

    // haze toward the horizon blends water into the sky glow — tinted
    // pool-WATER blue, not room-pink: the old haze target was dominated by
    // the pure-red tile, which turned all far water hot pink ("no blue in
    // the back"). Air picks up the WATER colour, not the walls.
    float dist = length(uEyePos - vWorld);
    float haze = 1.0 - exp(-dist * 0.004);
    vec3 hazeCol = vec3(0.16, 0.42, 0.62);            // airy pool-water blue
    col = mix(col, hazeCol, haze * 0.55);

    // The reflected sky colour is ALREADY tonemapped+gamma'd by sky_frag; a
    // second knee here desaturated everything to grey. Just clamp + a mild
    // gamma trim so bright reflections keep their tile colours.
    col = clamp(col, 0.0, 1.0);
    col = pow(col, vec3(1.0 / 1.15));
    fragColor = vec4(col, 1.0);
}
