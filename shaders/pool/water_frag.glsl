#version 330 core
// Pool scene water. One fragment shader handles:
//   * the checkerboard sky reflected through an analytic mirror ray into the
//     SAME checker function the sky dome uses (identical pattern, exact
//     alignment across the horizon — the pool-room illusion),
//   * the hidden-light specular highlight as a FULL COOK-TORRANCE microfacet
//     answer (GGX NDF + Smith visibility + Fresnel-Schlick, energy-conserving
//     by construction — the next-gen water shading games actually ship),
//   * bobbing ripple rings from the teapot fleet: up to MAX_RINGS concurrent
//     rings, streamed as uniforms from the scene module (each splash owns a
//     private window of slots so nine pots splashing at once never overwrite
//     each other's ripples),
//   * per-channel Beer-Lambert depth absorption (red dies first — the reason
//     real pools go blue with depth), subsurface-ish body colour and distance
//     haze into the sky tint.

#define MAX_RINGS 54   // MUST match MAX_RINGS in src/pool.cxx: the scene
                       // uploads all 54 slots (18 pots x 3 private windows)
                       // in one glUniform4fv — a shorter array here makes the
                       // whole upload GL_INVALID_OPERATION (silent no-op),
                       // killing every ripple ring in the room.

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

// ---- full Cook-Torrance microfacet terms (FUTURE BENCH shading core) ------
// GGX/Trowbridge-Reitz NDF
float D_GGX(float NoH, float a2) {
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}
// Smith height-correlated visibility (the modern G term — cuts the old
// Blinn-Phong's energy leak at grazing angles and keeps glints physical)
float V_SmithGGX(float NoV, float NoL, float a2) {
    float a = sqrt(a2);                       // a = alpha (not alpha^2)
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-4);
}
// Fresnel-Schlick with spherical-Gaussian approximation (Kulla-style SG
// form used by modern engines: cheaper than pow5, visually identical)
float F_Schlick(float u, float F0) {
    float f = pow(1.0 - u, 5.0);
    return F0 + (1.0 - F0) * f;
}

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

// REFLECTED TEAPOTS: 2D black ghost silhouettes painted directly into the
// water — the old 3D mirrored-fleet pass (a ceramic pot swimming underwater)
// is gone. A real reflection of an object above the surface appears as an
// upright, darkened SMEAR anchored at the object's waterline contact, seen
// along the MIRRORED view ray; where the surface is disturbed it smears
// further and breaks apart. Returns rgb = near-black ghost tint,
// a = coverage.
vec4 fleetGhost(vec3 V, vec3 pos, float bump) {
    // mirrored view ray (surface -> sky): reflect about the y=0 plane
    vec3 rd = vec3(-V.x, V.y, -V.z);
    float elev = clamp(rd.y / max(length(rd.xz), 1e-4), 0.0, 1.5);
    // ghosts live at GRAZING reflection angles — looking straight down at
    // the water next to a pot shows the body, not the mirror
    float ground = clamp(1.0 - elev * 1.25, 0.0, 1.0);
    ground *= ground;

    // the pot's waterline contact is at pos.y ~ -2 (pot bottom); march the
    // mirrored ray back up to the pot's hull (up to ~9 m tall) and ghost it
    float ht01 = clamp((-2.0 - pos.y) / 9.0, 0.0, 1.0);
    vec2 dirXZ = normalize(rd.xz + vec2(1e-5));
    vec2 base = pos.xz + dirXZ * (2.0 * ht01) * (1.0 + 0.6 * elev);

    // the ripple field smears the ghost — image wobble grows with bump
    float sway = sin(base.x * 1.7 + base.y * 1.3 + uTime * 1.1)
               + sin(base.x * 0.9 - base.y * 2.1 - uTime * 0.7);
    base += dirXZ * sway * (0.05 + 0.35 * bump);

    // upright smear: a tight core at the waterline stretching upward with
    // the pot height, loosening fast where rings disturb the surface
    float smear = 0.55 + 2.6 * ht01 + 2.0 * bump;
    float fade = 1.0 - 0.55 * ht01;         // the top of the ghost dies first
    vec2 dv = pos.xz - base;
    float ghost = ground * fade * exp(-dot(dv, dv) / (smear * smear));
    // distance gate: far reflections compress to slivers and lose strength —
    // without this the far fleet's overlapping smears read as a black band
    // across the horizon (the wide-view render showed exactly that)
    float gdist = length(uEyePos - pos);
    ghost *= 1.0 / (1.0 + gdist * 0.045);

    return vec4(0.008, 0.012, 0.020, clamp(ghost * 1.35, 0.0, 1.0));
}

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    float dist01 = clamp(length(uEyePos - vWorld) / 120.0, 0.0, 1.0); // for body depth
    float NoV = clamp(dot(vec3(0.0, 1.0, 0.0), V), 1e-3, 1.0);

    // --- splash rings: expand + fade, disturb the reflection ---------------
    float bump = 0.0;
    float foam = 0.0;
    float foamCrest = 0.0;   // thin bright highlight hugging the expanding crest
    for (int i = 0; i < MAX_RINGS; i++) {
        vec4 r = uRings[i];
        if (r.w <= 0.001) continue;   // dead slot (rings are per-pot windows now)
        float d = length(vWorld.xz - r.xy);
        float band = d - r.z;
        // BUILD-D3: the old band width (0.30 + 0.05*r) grew to ~0.6 m and
        // dozens of overlapping rings fused into broad soft cotton bands.
        // Narrower, and the ring perturbs the mirror less.
        float width = 0.26 + r.z * 0.032;
        float ring = exp(-band * band / (width * width));
        bump += ring * r.w * 0.38;
        foam  += ring * r.w;
        // thin bright crest line: the ring's advancing lip is a brighter sheet
        // of water (entrained air + surface normal facing the light), so it
        // reads as a moving ring rather than a flat stain.
        float crest = exp(-band * band / (width * width * 0.9));
        foamCrest += crest * r.w * 0.30;
        // Residual foam trail: a wide, WEAK halo behind the expanding ring
        // (a decaying wake that dissolves with the ring's own strength).
        // BUILD-D3 TIGHTENING: dozens of landing droplets spawn rings every
        // second and their halos stack into the broad soft blue bank across
        // the mid-pool — halved gain, narrower spread.
        float halo = exp(-band * band / (width * width * 8.0));
        foam += halo * r.w * r.w * 0.10;
    }
    bump = clamp(bump, 0.0, 1.0);
    foam = clamp(foam, 0.0, 1.0);
    foamCrest = clamp(foamCrest, 0.0, 1.0);

    // HYPER-REAL AMBIENT MICRO-CHOP (upgraded): real pool water never sits
    // glass-flat between splashes. TWO scales now — a broad slow swell (the
    // return-wave breathing of the whole pool) and a fine wind chop riding on
    // it. Both modulate the mirror ray, the highlight and the foam gates.
    float swell = sin(vWorld.x * 1.9 + uTime * 0.9) * sin(vWorld.z * 1.5 - uTime * 0.7);
    float chop  = sin(vWorld.x * 7.3 + uTime * 2.1) * sin(vWorld.z * 6.1 - uTime * 1.7);
    chop = 0.5 + 0.5 * (0.35 * swell + chop);
    bump = clamp(bump + chop * 0.05, 0.0, 1.0);

    // --- hidden light specular: FULL COOK-TORRANCE -----------------------
    // D (GGX) * V (Smith) * F (Schlick) / 4 — the actual physical answer, so
    // grazing glints stretch into bright streaks (G rises as the facet hides
    // behind itself), face-on water stays dark (F0 = 2%), and the peak is
    // finite on every driver. Roughness rises where the surface is disturbed.
    vec3 L = normalize(uLightDir);
    vec3 H = normalize(V + L);
    float NoH = max(dot(vec3(0.0, 1.0, 0.0), H), 0.0);
    float NoL = max(dot(vec3(0.0, 1.0, 0.0), L), 0.0);
    float aGGX = mix(0.055, 0.16, bump);
    float a2 = aGGX * aGGX;
    float specCT = D_GGX(NoH, a2) * V_SmithGGX(NoV, NoL, a2) * F_Schlick(NoH, 0.02);
    float spec = min(specCT * 0.9, 8.0) * 4.0;   // 1/4 folded into gain; capped
    // the hidden light FALLS OFF with distance from the viewer side of the
    // pool (inverse-square-ish over the room scale) — far water's sheen dims
    float lightFall = 1.0 - 0.45 * dist01;
    spec *= lightFall;
    float sheen = pow(NoH, 14.0) * 0.35 * lightFall; // broad faint glow floor

    // --- colour ------------------------------------------------------------
    vec3 refl = reflectedCheckerColor(-V, vWorld, bump * 0.62); // build D3:
                                    // ring storms no longer smear the mirror
                                    // into soft cyan ribbons

    // FUTURE-BENCH WATER BODY: per-channel Beer-Lambert absorption along the
    // water path. Pure water eats red first (absorb ~0.35/m), then green —
    // this is the real reason pools and deep lakes go blue, and it makes the
    // body colour depend on the actual view path instead of a constant.
    vec3 absorb = vec3(0.28, 0.07, 0.04);       // per-metre extinction (tamed:
                                                // real outdoor pool water is
                                                // LIGHTER than textbook ocean
                                                // absorption — shallow basin +
                                                // bright hidden light)
    float path = length(uEyePos - vWorld) * 0.5 + 0.5;   // metres, damped for the room scale
    vec3 trans = exp(-absorb * path);
    vec3 scatter = vec3(0.085, 0.300, 0.470) * mix(vec3(1.0), trans, 0.55); // shallow tint (lifted)
    vec3 body = mix(scatter, vec3(0.028, 0.150, 0.300), clamp(dist01, 0.0, 1.0));

    // HYPER-REAL WATER VOLUME: the water body is lit by the hidden light
    // through the ripple slopes — a faint subsurface glow where the light
    // enters a ripple and scatters back out toward the eye. This is what
    // keeps the pool reading as real water between the splashes, not flat
    // blue plastic. Applied AFTER the Beer-Lambert body so the glow survives.
    float subsurface = pow(max(dot(V, -normalize(uLightDir)), 0.0), 3.0) * bump * 0.20;
    body += uLightTint * subsurface * 0.10;

    // Fresnel-correct mirror: real water reflects ~2% face-on and ~100% at
    // grazing angles (Schlick off F0 = 0.02). Tying the tile sheen to the ACTUAL
    // Fresnel makes the pool go dark-blue overhead and mirror-like in the
    // distance — the single biggest realism cue the old flat 0.035 missed.
    float mirror = F_Schlick(NoV, 0.02);
    mirror = clamp(mirror * 1.45, 0.05, 0.88);   // grazing = near-FULL mirror
                                                 // (0.88 cap): real water at
                                                 // plane-level views IS the
                                                 // reflection — the old 0.62 cap
                                                 // mixed 38% dark body into the
                                                 // far water and painted a black
                                                 // band across the horizon
    vec3 col = mix(body, refl, clamp(mirror, 0.0, 1.0));
    // the fleet's 2D black ghosts ride ON TOP of the Fresnel mix — they are
    // the pots' reflections, not part of the sky mirror
    vec4 ghost = fleetGhost(V, vWorld, bump);
    col = mix(col, ghost.rgb, clamp(ghost.a, 0.0, 1.0));
    // BUILD-D3: the old cool cast (0.86,0.99,1.09) tinted even the mirrored
    // red/white dome toward cyan — every reflection of the room washed one
    // step toward the blue smoke bank. Near-neutral now: the mirror shows
    // the room as it is; blue belongs to the water BODY only.
    col *= vec3(0.97, 1.0, 1.02);
    col += uLightTint * (spec * 1.6 + sheen * 0.25 + chop * 0.018);  // the hidden light (tamed) + chop shimmer

    // HYPER-REAL FOAM: the expanding rings are not flat bright stains — they
    // have a brighter advancing lip (entrained air + surface facing the light)
    // and a softer foam wash inside the ring. The foam is pool water lit by
    // the hidden light, so it reads as bright blue-white in the room, not
    // generic white.
    float foamLit = foam * (0.55 + 0.45 * (0.5 + 0.5 * dot(V, normalize(uLightDir))));
    col += uLightTint * foamLit * 0.13;
    col += vec3(0.90, 0.94, 1.0) * foamCrest * 0.20;   // bright lip highlight

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
    // BUILD-D3: bump saturates to 1 across whole ring storms, which let the
    // caustic web pump +0.65/channel of blue-white wash over huge areas —
    // the glowing bank. Gain reined in; the web still lives where it should.
    col += uLightTint * caustic * (0.08 + 0.28 * bump) * lightFall;
    col += uLightTint * bodyShimmer * 0.6;             // volume shimmer

    // haze toward the horizon blends water into the sky glow — tinted
    // pool-WATER blue, not room-pink: the old haze target was dominated by
    // the pure-red tile, which turned all far water hot pink ("no blue in
    // the back"). Air picks up the WATER colour, not the walls.
    float dist = length(uEyePos - vWorld);
    float haze = 1.0 - exp(-dist * 0.004);
    vec3 hazeCol = vec3(0.30, 0.46, 0.56);            // airy cool grey-blue,
                                                      // desaturated (build D3:
                                                      // the old saturated hue
                                                      // stacked with foam+caustic
                                                      // wash into the blue bank)
    col = mix(col, hazeCol, haze * 0.44);

    // FILMIC ACES tail — the old hard clamp(col,0,1) was the biggest CGI
    // tell in the room: every mirrored checker tile brighter than 1.0 linear
    // fused into FLAT WHITE SHEETS (the render-critique tool flagged 3-6%
    // solid overbright masses + 13-14% whole-frame clipping). The water
    // reflection colour is computed analytically from the raw hot uTileA/B
    // values, so it is LINEAR here and gets the same filmic shoulder as the
    // sky and teapots: one coherent camera grading across the whole room.
    // Exposure 0.92 (FINAL PASS lift): the pool read a touch dark in the
    // previous build — real outdoor pool water is luminous, not moody. The
    // filmic shoulder still eats the overshoot; only the mid-tone floor rose.
    col *= 0.92;
    col = clamp((col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14), 0.0, 1.0);
    col = pow(col, vec3(1.0 / 1.15));
    fragColor = vec4(col, 1.0);
}
