#version 330 core
// Crown splash: a thin sheet of water erupting around the impact point.
//
// HYPER-REAL PASS: the film is POOL WATER, not a checker mirror — its
// colour is the same blues as the water body (uWaterA bright surface,
// uWaterB deep body), transmission-dominant face-on, and at grazing angles
// it shows the bright surface sheen of the pool, never flat white and
// never red/white tiles. A dense foam COLLAR hugs the water line (real
// crowns churn white-blue at the base), fingers tear irregularly and CRAWL
// up the sheet (the tear pattern drifts with time, like the sheet is
// actively disintegrating), and brightness follows local thickness.

in vec3 vWorld;
in vec3 vNormal;
in float vParam; // height parameter 0 (water line) .. 1 (rim)
in float vAngle; // 0..1 around the crown

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uWaterA;  // bright pool-water surface blue
uniform vec3 uWaterB;  // deep pool-water body blue
uniform float uTime;
out vec4 fragColor;

// ---- Cook-Torrance terms (same forms as water_frag.glsl) -----------------
float D_GGX(float NoH, float a2) {
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}
float V_SmithGGX(float NoV, float NoL, float a2) {
    float a = sqrt(a2);
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-4);
}
float F_Schlick(float u, float F0) {
    float f = pow(1.0 - u, 5.0);
    return F0 + (1.0 - F0) * f;
}

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    vec3 N = normalize(vNormal);

    // the film sees the POOL: bright surface blue where the sheet curves
    // toward the camera's reflected view, deep body blue where it shows
    // the water mass behind it. The sweep follows the ACTUAL reflected
    // ray's azimuth, so neighbouring fingers pick up different blues as
    // the wobbly sheet tilts — the film reads 3D, not painted.
    vec3 rd = reflect(-V, N);
    float sweepPhase = atan(rd.z, rd.x);
    float sweep = 0.5 + 0.5 * sin(sweepPhase * 3.0 +
                                  vWorld.x * 0.7 + vWorld.z * 0.9);
    vec3 env = mix(uWaterB, uWaterA, sweep);

    // thickness-driven film: a real crown sheet is THIN at the rim and thick
    // at the base, so it reflects at grazing angles (the rim glints) and
    // transmits face-on (the body shows the water behind). Thin-film edge
    // brightening — the first thing that separates real water film from
    // moulded plastic — comes from tying alpha to the local thickness.
    float thick = 1.0 - vParam;                       // 1 at base, 0 at rim
    // (rim brightening now comes from the Beer-Lambert film absorption below —
    // thin torn film transmits bright, thick base absorbs toward body blue)

    float edge = 1.0 - abs(dot(N, V));         // grazing = surface sheen
    float diff = max(dot(N, normalize(uLightDir)), 0.0);
    vec3 H = normalize(V + normalize(uLightDir));
    float spec = pow(max(dot(N, H), 0.0), 64.0);

    // ---- irregular, CRAWLING tearing ----
    // smooth phase warps (NOT per-pixel hash: the crown is only a few
    // hundred pixels around, unfiltered noise would alias to glitter) with
    // two incommensurate frequencies, seeded by world position so every
    // crown tears differently.
    float ang = vAngle * 6.2831853;
    float jitter = sin(ang * 3.0 + vWorld.x * 2.3) * 0.85
                 + sin(ang * 7.0 - vWorld.z * 1.7) * 0.55;
    // finger field: base comb + warped phase; climbs to a second wobble
    // frequency as the sheet climbs. The slow uTime drift makes the tear
    // pattern MIGRATE up the film — the sheet visibly churns.
    float fingers = 0.5 + 0.5 * sin(ang * 22.0 + jitter * 2.2
                                   + vParam * vParam * 4.0 - uTime * 1.6);
    // thickness ripple along the sheet, also slowly crawling
    float grain = 0.5 + 0.5 * sin(ang * 41.0 - jitter * 3.0 - vParam * 5.0
                                  + uTime * 2.2);
    // the sheet is dense at the base and disintegrates toward the rim:
    // the tear contrast DEEPENS with height (fingers pinch off there)
    float tear = mix(0.22, 0.62, smoothstep(0.15, 0.9, vParam));
    float sheet = (1.0 - 0.62 * vParam) * (0.55 + 0.45 * grain);
    sheet *= 1.0 - tear * (1.0 - fingers);
    sheet = max(sheet, 0.0);

    // ---- PHOTOREAL FILM SHADING ------------------------------------------
    // Real water-film optics, three ingredients:
    //  1. FRESNEL (Schlick, F0 = 0.02): face-on the film TRANSMITS (you see
    //     the water body through it, alpha low); at grazing angles it becomes
    //     a MIRROR (bright surface sheen, alpha up). This single term is the
    //     crown's most important realism cue.
    //  2. BEER-LAMBERT thin-film absorption: thicker film (crown base) eats
    //     more light and shifts blue, torn thin fingers stay bright —
    //     thickness variation now reads as COLOUR, not just alpha.
    //  3. COOK-TORRANCE glints (GGX + Smith + Fresnel): the microfacet
    //     answer on the wobbly sheet — energy-conserving sparkle streaks on
    //     finger rims, finite on every driver.
    vec3 L = normalize(uLightDir);
    float NoV = clamp(dot(N, V), 1e-3, 1.0);
    float NoL = max(dot(N, L), 0.0);
    float fres = F_Schlick(NoV, 0.02);           // mirror-ness of the film
    fres = clamp(fres * 1.6, 0.02, 0.85);        // film reads reflective sooner

    // Cook-Torrance microfacet glint on the film surface
    float aGGX = 0.18;                           // fairly sharp water glints
    float a2 = aGGX * aGGX;
    float NoH = max(dot(N, H), 0.0);
    float dGGX = D_GGX(NoH, a2) * V_SmithGGX(NoV, NoL, a2) * F_Schlick(NoH, 0.02);
    float glint = min(dGGX * 3.0, 2.2);          // capped: no fireflies

    // Beer-Lambert absorption through the film thickness: deep film tints
    // toward the saturated body colour, torn thin film stays watery-bright
    vec3 absorb = vec3(0.35, 0.08, 0.04);        // per-unit-film-thickness
    vec3 filmTint = exp(-absorb * thick * 2.4);

    vec3 transmitted = env * (0.42 + 0.30 * vParam) * filmTint;   // through-film
    vec3 mirrored   = env * 1.15;                                 // grazing mirror
    vec3 col = mix(transmitted, mirrored, fres * (0.45 + 0.55 * edge));

    // light answer: broad diffuse wrap + the microfacet glint + a faint
    // bright rim where fingers pinch off (thin edges catch the light)
    col += uLightTint * (0.05 + diff * 0.16);
    col += uLightTint * glint * (0.30 + 0.40 * edge);

    // local thickness shading: dense finger cores carry more water (brighter),
    // torn gaps are thinner film (dimmer, more water body through them)
    col *= 0.72 + 0.55 * fingers * vParam + 0.18 * grain;
    // dense white-blue foam COLLAR at the water line (the crown base churns)
    float collar = exp(-pow((vParam - 0.05) * 7.0, 2.0));
    col += vec3(0.72, 0.86, 0.98) * collar * 0.34;
    // clamp below clip: no amount of overlap can saturate a solid white mass
    col = min(col, vec3(0.97, 0.96, 0.95));

    // PHYSICS alpha: transmission face-on (film nearly invisible), mirror at
    // grazing (bright sheen reads even at low alpha). Thickness raises alpha;
    // torn film (low sheet) fades to near-nothing.
    float alpha = mix(0.10 + 0.25 * thick, 0.55 + 0.35 * edge, fres);
    alpha *= sheet;
    alpha += collar * 0.30;
    alpha += edge * (1.0 - thick) * 0.15;             // pinched-off rim glint
    fragColor = vec4(col, alpha);
}
