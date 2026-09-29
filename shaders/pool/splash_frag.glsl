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
uniform vec3 uTileA;   // hot white checker tile (linear) — the room's dome
uniform vec3 uTileB;   // deep pure red checker tile — the room's dome
uniform float uTime;
uniform float uJet;    // 0 = crown sheet, 1 = central Worthington jet cone
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

// ---- the pool-dome environment: THE SAME angular-grid checkerboard the
// ---- water mirror shows (identical checker + mapping constants as
// ---- water_frag.glsl, *4.0 grid). The crown film is part of the ROOM now:
// ---- its reflected and refracted rays land in the same world the surface
// ---- mirrors, so the splash finally reads as water IN this pool.
float checker(vec2 p) {
    vec2 w = fwidth(p) + 1e-4;
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5)
                  - abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}
vec3 poolEnv(vec3 dir) {
    float up = clamp(dir.y, -1.0, 1.0);
    if (up > 0.0) {
        // mirrored rays see the checkerboard dome — same angular grid as the
        // water mirror (tile columns line up between film and surface)
        vec2 plane = vec2(atan(dir.x, dir.z), asin(up)) * 4.0;
        float c = checker(plane);
        vec3 albedo = mix(uTileB, uTileA, c);
        float fres = 0.35 + 0.65 * pow(1.0 - up, 1.5);   // grazing rays brighter
        return albedo * fres;
    }
    // refracted rays: what you see THROUGH the film. The crown sits ABOVE
    // the waterline — through its upper sheet you see the far side of the
    // room (bright tiles, horizon glow), NOT the seabed. Deep blue is only
    // for rays that plunge steeply down. This single distinction is what
    // separates real spray from the "blue cotton cloud" look.
    if (up > 0.0) {
        // grazing up-rays skim along the water plane: horizon glow
        float horiz = 1.0 - up;
        vec3 albedo = vec3(0.42, 0.40, 0.40);
        return albedo * (0.45 + 0.55 * horiz);
    }
    // rays that plunge into the pool: the deep water mass, brighter near
    // the surface
    return mix(vec3(0.10, 0.42, 0.62), vec3(0.028, 0.150, 0.300), clamp(-up, 0.0, 1.0));
}

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    vec3 N = normalize(vNormal);

    // the film sees the REAL POOL: reflect and refract the view ray against
    // the wobbly sheet and sample the actual dome checkerboard / water body.
    // Neighbouring fingers bend the rays differently, so red/white tiles
    // smear and swim across the film exactly like reflections on real water.
    vec3 rd = reflect(-V, N);                 // mirror ray -> the dome
    vec3 rt = refract(-V, N, 0.75);           // transmitted ray -> the body
                                              // (air->water, eta = 1/1.33)
    vec3 envMirror = poolEnv(rd);
    vec3 envRefr   = poolEnv(rt);

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

    // ANTI-FOG S-CURVE: raw sheet values hover in the 0.3-0.7 mush band;
    // hundreds of those stacked by overdraw = the grey-blue fog bank seen
    // from afar. Pushing the field to its ENDS (dense film or nothing) keeps
    // the torn edges AND restores contrast between the crowns and the sky.
    sheet = smoothstep(0.18, 0.62, sheet);
    // DISTANCE HARDENING: real crowns seen from across a pool are distinct
    // sheets, not haze (individual droplets resolve below the eye's angular
    // threshold). Steepen the curve with distance so far crowns are MORE
    // discrete, not less.
    float camDist = length(uEyePos - vWorld);
    sheet = smoothstep(0.30 - 0.15 * clamp(camDist / 90.0, 0.0, 1.0),
                       0.75, sheet);

    // FOAM WHITENING: a crumbling sheet is not clear water — entrained air
    // bubbles scatter ALL wavelengths. The whiteness follows the tear field:
    // where the film is disintegrating (low fingers, high tear) it reads as
    // aerated foam; where it is still a continuous sheet it stays glassy.
    // (declared AFTER fingers/tear — strict drivers reject forward refs)
    float foam = (1.0 - fingers) * tear * smoothstep(0.15, 0.85, vParam);
    foam = clamp(foam * 1.7, 0.0, 1.0);

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
    float glint = min(dGGX * 2.2, 1.8);          // capped, tamed: at fleet
                                                 // scale stacked glints fed
                                                 // the fog-wash glow

    // Beer-Lambert absorption through the film thickness: deep film tints
    // toward the saturated body colour, torn thin film stays watery-bright
    vec3 absorb = vec3(0.35, 0.08, 0.04);        // per-unit-film-thickness
    vec3 filmTint = exp(-absorb * thick * 2.4);

    vec3 transmitted = envRefr * (0.55 + 0.45 * vParam) * filmTint; // through-film:
                                                                    // the far room,
                                                                    // dimmed by film
                                                                    // thickness
    vec3 mirrored   = envMirror * 1.15;                            // grazing mirror:
                                                                    // hot dome tiles
    vec3 col = mix(transmitted, mirrored, fres * (0.45 + 0.55 * edge));
    // aerated foam overlays the optics: bubble scatter washes toward white
    // regardless of what the rays do (this is the crumbling-edge look)
    col = mix(col, vec3(0.94, 0.97, 1.0), foam * 0.75);

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

    // DROPLET BEADS: as the film tears, each finger's rim beads up into a
    // necklace of droplets — the last thing a real crown does before it
    // collapses. Bright micro-highlights riding the torn edge, drifting with
    // the crawl of the tear field.
    float beadBand = smoothstep(0.55, 0.95, vParam) * smoothstep(0.35, 0.75, fingers);
    float beads = pow(max(sin(ang * 6.2831853 * 9.0 + jitter * 3.0 + uTime * 1.3), 0.0), 6.0);
    col += vec3(0.85, 0.93, 1.0) * beadBand * beads * 0.85;

    // SPRAY STREAKS: real crowns throw a burst of ballistic droplets from
    // the crest of every finger. Analytic sparkle spikes hugging the rim,
    // seeded per-finger so they burst WHERE the film is tearing, with a
    // fast per-frame drift (spray lives fractions of a second).
    float burst = smoothstep(0.80, 1.0, vParam) * smoothstep(0.45, 0.85, fingers);
    float streakPhase = ang * 6.2831853 * 17.0 + jitter * 5.0 + uTime * 6.0;
    float streaks = pow(max(sin(streakPhase), 0.0), 14.0)
                  * pow(max(sin(streakPhase * 0.53 + 1.7), 0.0), 6.0);
    col += vec3(0.96, 0.98, 1.0) * burst * streaks * 1.2;
    // sparse ballistic droplets beyond the rim: high-frequency twinkle just
    // above the crown lip (the "sparkle spray" of high-speed footage)
    float dropletField = pow(max(sin(ang * 6.2831853 * 31.0 - uTime * 9.0
                                        + jitter * 2.0), 0.0), 24.0);
    col += vec3(1.0) * burst * dropletField * 0.8;
    // clamp below clip: no amount of overlap can saturate a solid white mass
    col = min(col, vec3(0.97, 0.96, 0.95));

    // PHYSICS alpha: transmission face-on (film nearly invisible), mirror at
    // grazing (bright sheen reads even at low alpha). Thickness raises alpha;
    // torn film (low sheet) fades to near-nothing.
    float alpha = mix(0.06 + 0.24 * thick, 0.58 + 0.35 * edge, fres);
    alpha = mix(alpha, 0.94, foam * 0.8);             // foam is nearly opaque
    alpha = clamp(alpha + burst * streaks * 0.5 + burst * dropletField * 0.35, 0.0, 1.0);
    alpha *= sheet;
    alpha += collar * 0.30;
    alpha += edge * (1.0 - thick) * 0.15;             // pinched-off rim glint
    alpha += beadBand * beads * 0.25;                 // droplet beads catch light

    // ---- WORTHINGTON JET MODE (uJet = 1): the central column erupting
    // from the crown's middle — the element every real splash has that a
    // lone ring lacks (high-speed footage: crown first, then the jet
    // spikes up through it, shedding droplets). Same film optics, tuned
    // for a fast column: near-mirror grazing sides (vertical walls seen
    // from afar), heavy wind-driven aerated breakup along the column.
    if (uJet > 0.5) {
        float jetFade = 1.0 - smoothstep(0.78, 0.98, camDist / 90.0); // pop-in veil
        vec3 jcol = mix(envMirror * 1.25, envRefr * 0.8, fres * 0.45); // glassy column
        jcol += uLightTint * glint * 0.9;                              // strong glint
        float aerate = 0.35 + 0.65 * grain;                            // crawling breakup
        jcol = mix(jcol, vec3(0.92, 0.96, 1.0), (1.0 - aerate) * 0.55);// aeration foam
        float jalpha = (0.55 + 0.40 * edge) * aerate * jetFade;
        fragColor = vec4(jcol, clamp(jalpha, 0.0, 1.0));
        return;
    }
    fragColor = vec4(col, alpha);
}
