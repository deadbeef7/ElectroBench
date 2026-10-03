#version 330 core
// SCENE 4 (POWER LINES) object shader: everything solid in the scene —
// utility poles, crossarms, the tangle of wires, houses, the road, the
// treeline. BUILD-P7 REALISM PASS.
//
// What changed and why the old one looked fake:
//   * ONE MATERIAL ID per surface (attribute location 3) instead of "if the
//     normal happens to point up, call it dirt". Wood got sine grain, the
//     road got the SAME dusty modulation as the gravel beside it, and the
//     houses were untextured slabs. Each material now has its own detail.
//   * HEMISPHERE ambient instead of one flat constant: at dusk a surface
//     facing the bright sky is much lighter than one facing the dark
//     ground, and that single term is most of "does this look lit".
//   * CONTACT darkening near y=0 — the analytic stand-in for the ambient
//     occlusion that stops a house from floating over its own shadow.
//   * Haze that (a) thins with altitude, (b) BRIGHTENS toward the sun.
//     Backlit haze is a forward-scattering medium: looking into the low sun
//     the air glows. A constant grey fog is the classic tell.
//   * Asphalt: two polished wheel paths, aggregate speckle near the camera,
//     and the surface detail fades out with distance instead of aliasing.
//
// Cost discipline: the bench runs on a 20-year-old Core 2 Duo, so the detail
// noise is one or two octaves and gated by `near` (1 near the camera, 0 past
// ~70 m) — far surfaces pay for nothing and alias nothing.

in vec3 vWorld;
in vec3 vNormal;
in vec3 vColor;
flat in float vMat;   // MUST match the flat qualifier in object_vert.glsl
in float vAlpha;      // smooth: this is the penumbra ramp

out vec4 fragColor;

uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform float uTime;       // reserved (subtle cable sway shading)
uniform float uRoadX;      // road centre line, must match kRoadX in pole.cxx

// material ids — MUST match the kMat* constants in src/pole.cxx
const float kMatPaint  = 0.0;
const float kMatWood   = 1.0;
const float kMatGround = 2.0;
const float kMatRoad   = 3.0;
const float kMatLine   = 4.0;   // road paint
const float kMatCable  = 5.0;
const float kMatCeramic= 6.0;
const float kMatMetal  = 7.0;
const float kMatWall   = 8.0;
const float kMatRoof   = 9.0;
const float kMatGlass  = 10.0;
const float kMatLeaf   = 11.0;
const float kMatShadow = 12.0;  // streamed, alpha-blended ground shadow
const float kMatGlow   = 13.0;  // emissive: lit signage / lamp lenses at dusk
const float kMatSteel  = 14.0;  // BUILD-P9: hot-dip galvanized steel (shafts, arms)
const float kMatKerb   = 15.0;  // BUILD-P10: cast concrete kerb + gutter
const float kMatConcrete = 16.0;// BUILD-P12: weathered precast concrete shaft

// cheap hash: no transcendentals (the old sin-based hash was 20+ cycles on
// a pre-SSE CPU). Same "value noise", a fraction of the cost.
float hash21(vec2 p) {
    vec3 q = fract(vec3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// BUILD-P8: the haze colour is needed by both the emissive branch (above) and
// the lit surfaces (below), so it is factored out rather than duplicated.
vec3 hazeColFor(vec3 base, vec3 L, vec3 V) {
    float toSun = max(dot(-V, L), 0.0);
    return mix(vec3(0.70, 0.52, 0.37), vec3(0.98, 0.75, 0.50), pow(toSun, 3.0));
}

// BUILD-P15: how well THIS PIXEL can resolve an octave of spatial frequency
// `freq` (cycles per metre), given the fragment's world-space footprint.
// 1.0 = fully resolved, 0.0 = below one pixel, i.e. would alias. Called with
// the single per-fragment footprint computed in main(), so every octave in
// the shader shares one footprint.
float octaveRes(float foot, float freq) {
    return 1.0 - smoothstep(0.35, 1.10, foot * freq);
}

// ---------------------------------------------------------------------------
// BUILD-P10: the atmosphere, copied BYTE-IDENTICALLY from sky_frag.glsl (the
// same rule the pool scene follows with its checker: one file cannot include
// the other without extension support on every driver).
//
// Why the object pass needs it: a wet road and a dark window do not reflect a
// guessed constant — they reflect THE SKY. A flat grey reflection term is one
// of the loudest "this is CG" tells in any shot containing a road, and the
// corridor is exactly that shot. Sampling the real analytic sky along the
// reflection ray is what turns the carriageway into a mirror of the sunset,
// and it is the cheapest large win available: the same twenty flops the sky
// pass already spends, over the road's share of the frame.
// ---------------------------------------------------------------------------
const vec3  kBetaR = vec3(0.058, 0.135, 0.331);
const float kBetaM = 0.0092;
const float kSunI  = 330.0;
const float kSunPath = 9.8;
const float kRayGain = 4.35;

vec3 atmosphere(vec3 dir, vec3 sun) {
    float h = dir.y;
    float mu = dot(dir, sun);
    float mView = 1.0 / (max(h, 0.0) + 0.14);
    float mSun  = 1.0 / (max(sun.y, 0.0) + 0.14);
    vec3 Tview = exp(-(kBetaR + vec3(kBetaM)) * mView * 0.92);
    vec3 Tsun  = exp(-kBetaR * mSun * kSunPath - vec3(kBetaM * mSun * 1.15));
    float phR = 0.0596831 * (1.0 + mu * mu);
    const float g = 0.76, gg = g * g;
    float phM = 0.0795775 * (1.0 - gg)
              / max(pow(1.0 + gg - 2.0 * g * mu, 1.5), 1e-3);
    vec3 single = kBetaR * phR * kRayGain * Tview * Tsun;
    vec3 mie    = vec3(kBetaM * phM * 0.25) * Tview * Tsun;
    float multiK = 0.005 + 0.30 * smoothstep(0.78, 1.06, h);
    vec3 multi  = kBetaR * phR * 0.80 * Tview * multiK;
    return (single + mie + multi) * kSunI;
}

void main() {
    float m = vMat;

    // ---- streamed ground shadows -------------------------------------------
    // Drawn LAST with blendFunc(ZERO, ONE_MINUS_SRC_ALPHA), so the fragment
    // colour is irrelevant: the pass simply multiplies whatever is already in
    // the framebuffer (road paint included) by (1 - vAlpha). That is why a
    // shadow lying across the road darkens the DASHES too, instead of
    // stamping a flat brown bar over them.
    if (m == kMatShadow) {
        fragColor = vec4(0.0, 0.0, 0.0, vAlpha);
        return;
    }

    vec3 N = normalize(vNormal);
    vec3 toEye = uEyePos - vWorld;
    float dist = length(toEye);
    vec3 V = toEye / max(dist, 1e-4);
    // two-sided: cables and single-sided quads read from any angle
    if (dot(N, V) < 0.0) N = -N;

    vec3 L = normalize(uSunDir);
    vec3 P = vWorld;
    float near = 1.0 - smoothstep(16.0, 64.0, dist);   // detail fade

    // BUILD-P15 THE DETAIL FILTER. The old gate was pure DISTANCE: `near`
    // reached zero at 64 m, so every fine octave switched off and the whole
    // far half of the corridor — road, verges, house walls, the near field of
    // the next pole down — rendered as a flat wash of unmodulated albedo.
    // That is the "pixellated, no detail" complaint, and it was a gate, not a
    // lack of detail: the octaves existed, they were just being told to stop.
    //
    // Distance is the wrong test anyway. Whether an octave is usable depends
    // on whether ONE PIXEL can resolve it, and the driver already knows that
    // exactly: fwidth() gives the fragment's world-space footprint. A 20 m
    // ground feature is still resolvable at 250 m; a 2 cm aggregate speckle
    // is not resolvable at 8 m. So each octave is now gated by
    //     res(freq) = 1 - smoothstep(0.35, 1.10, footprint * freq)
    // which keeps it alive exactly as long as the pixel it lands in can
    // resolve it, kills it one octave before it would alias into shimmer, and
    // costs ONE footprint computation for every octave in the shader.
    // The practical result is that texture now runs all the way to the
    // horizon instead of stopping dead at 64 m.
    float foot = fwidth(P.x) + fwidth(P.z) + 0.0015;

    vec3 base = vColor;
    float rough = 0.85;     // 0 = mirror, 1 = chalk
    float sheen = 0.0;      // extra grazing term (cables, glass)
    float wet = 0.0;        // BUILD-P10: standing-water film (road, kerb)

    if (m == kMatGround) {
        // dirt/gravel: broad damp patches + fine grit, then a dry-grass
        // tint where the broad noise runs high (weeds in the verges).
        // BUILD-P8: the amplitudes are up and the base albedo is down — a
        // single bright flat plane was reading as blown-out white paper in
        // the mid-distance, which is the "washed out" complaint.
        float n1 = vnoise(P.xz * 0.42);
        float n2 = vnoise(P.xz * 2.10);
        // BUILD-P15: the near-field-only grit octave is now GATED BY RESOLUTION,
        // not by distance, and a new broad octave sits UNDER it. The verges are
        // the largest flat area in the frame and they were reading as paper:
        // one 7 m mottling octave that is resolvable from the camera to the
        // horizon gives the ground a feature size, and the 7.5 c/m grit rides
        // on top of it for as long as a pixel can actually see it.
        float n0 = vnoise(P.xz * 0.135 + 3.7);
        float g3 = octaveRes(foot, 7.5);
        float n3 = g3 > 0.02 ? vnoise(P.xz * 7.5) : 0.5;
        base *= 0.58 + 0.30 * n0 + 0.38 * n1 + 0.26 * n2 + 0.14 * n3 * g3;
        base = mix(base, vec3(0.150, 0.155, 0.082),
                   smoothstep(0.48, 0.95, n1) * 0.55);
        // scuffed dust either side of the carriageway (traffic throws grit)
        float nearRoad = exp(-pow((abs(P.x - uRoadX) - 3.6) / 1.5, 2.0));
        base = mix(base, base * 1.22 + vec3(0.020, 0.016, 0.010),
                   nearRoad * 0.55);
        rough = 1.0;
    } else if (m == kMatRoad) {
        float rx = P.x - uRoadX;
        float n = vnoise(P.xz * 1.30);
        base *= 0.84 + 0.30 * n;
        // two polished wheel paths (traffic burnishes the tarmac lighter)
        float wp = exp(-pow((abs(rx) - 1.05) / 0.62, 2.0));
        base = mix(base, base * 1.42 + vec3(0.010), wp * 0.55);
        // aggregate speckle — near field ONLY. The branch is coherent across
        // the whole far field, so on a pre-SSE CPU it skips a whole value
        // noise for most of the frame (and stops the speckle aliasing).
        float gRoad = octaveRes(foot, 11.0);
        if (gRoad > 0.02)
            base += (vnoise(P.xz * 11.0) - 0.5) * 0.09 * gRoad;
        // a darker seam down the very centre where the tar is oldest
        base *= 1.0 - 0.18 * exp(-pow(rx / 0.55, 2.0));
        rough = mix(0.95, 0.62, wp);          // polished paths catch the sun
        // BUILD-P10: DAMP TARMAC. A wet surface is not "a shinier dry one":
        // the water film fills the aggregate, so the albedo drops hard (the
        // light that used to scatter back out of the stone now enters it and
        // comes back specular), and the roughness collapses. The physics is
        // free; what it buys is a road that mirrors the sunset down the
        // corridor instead of reading as grey felt. Damp comes from a broad
        // noise field plus the ruts, which is where water actually collects.
        // PATCHY ON PURPOSE: the first pass gave the whole carriageway a 0.16
        // wetness floor plus a broad coverage, and at this camera's grazing
        // angle Fresnel runs to ~0.6, so the entire road turned into one
        // uniform sheet of reflected horizon and the tarmac vanished. A wet
        // road is high CONTRAST — bright mirror patches next to dry, dark,
        // textured stone — and that contrast is the whole effect.
        float damp = smoothstep(0.52, 0.86, vnoise(P.xz * 0.30 + 11.0));
        damp = clamp(damp * 0.85 + wp * 0.22, 0.0, 1.0);
        // gutters at both edges never dry out
        damp = clamp(damp + 0.55 * exp(-pow((abs(rx) - 2.45) / 0.38, 2.0)), 0.0, 1.0);
        base *= mix(1.0, 0.55, damp);
        rough = mix(rough, 0.20, damp);
        wet = damp;
    } else if (m == kMatLine) {
        // worn road paint: chipped at the edges, dirty in the middle
        float wear = octaveRes(foot, 6.0);
        float wearN = wear > 0.02 ? vnoise(P.xz * 6.0) : 0.5;
        base *= 0.78 + 0.34 * wearN * wear + 0.18 * (1.0 - near);
        rough = 0.85;
    } else if (m == kMatWood) {
        // grain runs ALONG the trunk: squash x/z, stretch y
        float along = P.y * 1.15 + (P.x + P.z) * 0.06;
        float g1 = vnoise(vec2((P.x + P.z) * 6.5, along));
        float gGrain = octaveRes(foot, 19.0);
        float g2 = gGrain > 0.02 ? vnoise(vec2((P.x + P.z) * 19.0, along * 2.6))
                                 : 0.5;
        base *= 0.80 + 0.28 * g1 + 0.16 * g2 * gGrain;
        // creosote soaked darker in the first metre off the ground
        base *= mix(0.70, 1.0, smoothstep(0.0, 1.20, P.y));
        rough = 0.95;
    } else if (m == kMatWall) {
        // horizontal siding boards with a seam shadow between each course
        float seam = abs(fract(P.y * 1.25) - 0.5) * 2.0;
        base *= mix(0.70, 1.10, smoothstep(0.06, 0.34, seam));
        base *= 0.88 + 0.24 * vnoise(vec2((P.x + P.z) * 2.2, P.y * 0.9));
        // rain-streaked dirt splashing up the bottom 40 cm
        base *= mix(0.55, 1.0, smoothstep(0.0, 0.55, P.y));
        rough = 0.9;
    } else if (m == kMatRoof) {
        // pantile courses: a repeating ridge/valley along the slope
        float course = abs(fract(P.z * 0.85 + P.x * 0.0) - 0.5) * 2.0;
        base *= mix(0.70, 1.12, smoothstep(0.12, 0.55, course));
        base *= 0.84 + 0.32 * vnoise(P.xz * 1.7);
        rough = 0.85;
    } else if (m == kMatGlass) {
        // windows read as dark holes with a hard sky glint on them
        base *= 0.42;
        rough = 0.06;
        sheen = 0.55;
    } else if (m == kMatKerb) {
        // BUILD-P10: cast concrete kerb. Three things make concrete read as
        // concrete and not as a grey box: exposed aggregate (a bright speckle
        // of stones in a darker cement matrix), a chamfered top arris that
        // catches a hard highlight, and the dirt that collects in the gutter
        // and sprays up the face. The kerb is also the strongest single cue
        // that this is a real street rather than a plane with paint on it.
        float agg = vnoise(vec2(P.x * 41.0, P.z * 9.0));
        float cement = vnoise(vec2(P.x * 3.1, P.z * 0.85));
        base *= 0.80 + 0.30 * cement;
        float gKerb = octaveRes(foot, 41.0);
        if (gKerb > 0.02) {
            // aggregate resolves while a pixel can still see a stone, and
            // dies exactly one octave before it would shimmer at distance
            float stones = smoothstep(0.62, 0.86, agg);
            base = mix(base, base * 1.85 + vec3(0.020), stones * gKerb * 0.75);
        }
        // dirt in the gutter and splashed up the vertical face
        float gz = 1.0 - clamp(P.y / 0.16, 0.0, 1.0);
        base = mix(base, vec3(0.115, 0.098, 0.078), smoothstep(0.35, 1.0, gz) * 0.60);
        // a hair of standing water in the gutter line keeps the kerb wet too
        wet = 0.40 * smoothstep(0.55, 0.95, gz);
        base *= mix(1.0, 0.55, wet);
        rough = mix(0.88, 0.30, wet);
        sheen = 0.10;
    } else if (m == kMatLeaf) {
        // canopy clumping — one octave is enough at silhouette distance
        float n = vnoise(P.xz * 0.85 + P.y * 0.35);
        base *= 0.72 + 0.46 * n;
        rough = 1.0;
    } else if (m == kMatCable) {
        rough = 0.42;
        sheen = 0.45;
    } else if (m == kMatCeramic) {
        rough = 0.22;
        sheen = 0.30;
    } else if (m == kMatMetal) {
        rough = 0.38;
        sheen = 0.22;
    } else if (m == kMatSteel) {
        // BUILD-P9: GALVANIZED STEEL. Zinc is neither brown nor smooth: it has
        // a crystalline spangle, a chalky white bloom where it has weathered,
        // rain-washed streaks running down the shaft, and rust creeping up out
        // of the base plate. One of the three noise taps is gated by `near`, so
        // the far corridor — which is most of the frame — pays for two.
        float streak = vnoise(vec2((P.x + P.z * 0.35) * 1.60, P.y * 0.22));
        // BUILD-P15: zinc spangle is a 3-6 cm feature, so gating it on DISTANCE
        // meant every shaft past 64 m had no surface at all and read as a bare
        // tube. Gating it on RESOLUTION keeps the spangle on a pole at 40 m
        // and still lets it die cleanly one octave before it would alias.
        float gSpangle = octaveRes(foot, 26.0);
        float spangle = gSpangle > 0.02
                            ? vnoise(vec2((P.x + P.z) * 26.0, P.y * 3.0))
                            : 0.5;
        base *= 0.80 + 0.30 * streak + 0.20 * spangle * gSpangle;
        base = mix(base, base * 1.24 + vec3(0.028, 0.029, 0.029),
                   smoothstep(0.55, 1.0, streak) * 0.55);
        float rustN = vnoise(vec2((P.x + P.z) * 2.60, P.y * 0.90));
        float rust = smoothstep(1.80, 0.25, P.y) * smoothstep(0.42, 0.86, rustN);
        base = mix(base, vec3(0.135, 0.060, 0.030), rust * 0.70);
        rough = mix(0.62, 0.34, smoothstep(0.25, 0.85, streak)) + rust * 0.25;
        sheen = 0.42;
    } else if (m == kMatConcrete) {
        // BUILD-P12: WEATHERED PRECAST CONCRETE. The reference pole is a
        // concrete shaft, and the things that say "concrete" rather than
        // "grey cylinder" are all directional: vertical water staining that
        // runs DOWN from every bracket, horizontal form-board lifts from the
        // mould, spalled patches showing darker aggregate, and a dirt line
        // where rain has washed the street's dirt up the first two metres.
        float streak = vnoise(vec2((P.x + P.z * 0.45) * 2.10, P.y * 0.10));
        base *= 0.84 + 0.28 * streak;
        // form-board seams: the mould leaves a faint horizontal line every
        // 0.6 m, and they are what make the shaft read as CAST rather than as
        // turned
        float board = abs(fract(P.y * 1.62) - 0.5) * 2.0;
        base *= mix(0.94, 1.05, smoothstep(0.04, 0.26, board));
        // spalling — chipped patches revealing the darker coarse aggregate,
        // kept while a pixel can resolve a 13 cm spall patch
        float gSpall = octaveRes(foot, 7.5);
        if (gSpall > 0.02) {
            float spall = smoothstep(0.70, 0.93,
                                     vnoise(vec2((P.x + P.z) * 7.5, P.y * 1.7)));
            base = mix(base, vec3(0.180, 0.172, 0.162), spall * 0.55 * gSpall);
        }
        // grime washed up the foot, and rain streaks strongest just under the
        // hardware where the water always runs off
        base = mix(base, vec3(0.168, 0.158, 0.140),
                   smoothstep(2.4, 0.10, P.y) * 0.42);
        base = mix(base, base * 0.82,
                   smoothstep(0.55, 0.95, streak) * 0.30);
        rough = 0.90 - 0.10 * streak;
        sheen = 0.06;
    } else if (m == kMatGlow) {
        // BUILD-P8: emissive. A lit vending machine or lamp lens is its own
        // light source — running it through the diffuse model just made it a
        // pale box. Emitted straight through, with only a touch of haze on
        // top so distant lamps still sit in the air.
        float distG = dist;
        float hz = 1.0 - exp(-distG * 0.0016);
        vec3 g = base * 1.05;
        g = mix(g, hazeColFor(g, L, V), clamp(hz, 0.0, 0.45));
        fragColor = vec4(pow(max(g, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
        return;
    }

    // ---- lighting ----------------------------------------------------------
    // Hemispheric ambient: the dusk sky dome is BRIGHT, the ground bounces
    // warm and dim. A single flat ambient is why every surface in build P6
    // looked like it was lit by the same paint bucket.
    float up = N.y * 0.5 + 0.5;
    vec3 ambGround = vec3(0.125, 0.086, 0.064);
    vec3 ambSky    = vec3(0.395, 0.340, 0.360);
    vec3 amb = mix(ambGround, ambSky, up);

    // Sun. Matte surfaces get plain Lambert; the cables keep a wrap term so
    // a thin tube never goes fully black on its shadow side.
    float wrap = (m == kMatCable) ? 0.35 : 0.0;
    // BUILD-P9: galvanized steel is a polished cylinder, so the sun wraps
    // around it — without this the shafts read as flat cut-outs wherever the
    // sun is behind them, which is most of the run.
    if (m == kMatSteel) wrap = 0.22;
    float diff = clamp((dot(N, L) + wrap) / (1.0 + wrap), 0.0, 1.0);
    vec3 sunTint = vec3(1.00, 0.66, 0.34);

    // BUILD-P10: SPECULAR IS NOW A REAL MICROFACET LOBE. The old term was
    // Blinn-Phong with an exponent mapped from roughness, which cannot make a
    // highlight that is both tight AND energy-sane: it is why the damp road and
    // the steel never got a convincing glint. D (GGX) * G (Smith, Schlick
    // approximation) * F (Schlick) is the standard answer and costs about
    // twenty extra flops, so it is affordable even here.
    vec3 H = normalize(L + V);
    float NoV = clamp(dot(N, V), 1e-3, 1.0);
    float NoL = max(dot(N, L), 0.0);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float a2 = max(rough * rough, 0.0025);
    a2 = a2 * a2;
    float dd = NoH * NoH * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159265 * dd * dd);
    float kg = a2 * 0.5;
    float G = 0.25 / (max(NoV * (1.0 - kg) + kg, 1e-4)
                      * max(NoL * (1.0 - kg) + kg, 1e-4));
    float Fs = 0.045 + 0.955 * pow(1.0 - VoH, 5.0);
    // clamped so a near-mirror facet can never fire a white hole in the frame
    float sp = min(D * G * Fs / (4.0 * NoV) * NoL, 3.0) * 1.35;

    // BUILD-P10: WET SURFACES REFLECT THE SKY. A reflection ray perturbed by
    // the surface's own ripple noise (so the mirror breaks up like water, not
    // glass), sampled from the SAME analytic atmosphere the sky pass uses. At
    // the camera's grazing angle down the corridor Fresnel runs to 1, which is
    // what lays a long bright sheen of sunset down the wet road — the single
    // most photographic thing in the shot, and previously absent.
    vec3 skyRefl = vec3(0.0);
    float wetF = 0.0;
    if (wet > 0.01) {
        vec3 Np = N;
        float rip = vnoise(P.xz * 2.7) - 0.5;
        float rip2 = vnoise(P.xz * 2.7 + 19.0) - 0.5;
        Np = normalize(N + vec3(rip, 0.0, rip2) * (0.16 * wet));
        vec3 R = reflect(-V, Np);
        R.y = abs(R.y);                       // never sample below the horizon
        skyRefl = atmosphere(normalize(R), L);
        // the sheen is scaled back from a pure mirror: a real wet road is also
        // a thin film over an absorbing substrate, so even at grazing
        // incidence it never reaches the full Fresnel reflection
        wetF = (0.030 + 0.970 * pow(1.0 - NoV, 5.0)) * wet * 0.52;
        // a wet surface is also darker in the diffuse term
        diff *= mix(1.0, 0.55, wet);
    }

    // grazing rim: wires glint along their whole length against the sun
    float edge = 1.0 - abs(dot(N, V));
    float rim = pow(edge, 6.0) * sheen;

    vec3 col = base * (amb + sunTint * diff * 1.15) + sunTint * (sp + rim)
             + skyRefl * wetF;

    // ---- contact darkening -------------------------------------------------
    // Everything standing on the ground picks up a contact gradient: the
    // house walls darken into their own footprint, the pole bases sit in
    // dirt. Ground/road/paint are excluded (they ARE the floor) and so are
    // the wires (they never touch). The kerb is INCLUDED from P10 but with a
    // gentler ramp: it stands only 16 cm proud of the road, so the full
    // 1.30 m falloff would bury the whole thing in its own shadow.
    if (m == kMatKerb)
        col *= mix(0.80, 1.0, smoothstep(0.0, 0.16, P.y));
    else if (m != kMatGround && m != kMatRoad && m != kMatLine)
        col *= mix(0.60, 1.0, smoothstep(0.0, 1.30, P.y));

    // ---- aerial perspective ------------------------------------------------
    // Thinner with altitude (the haze lives in the first 30 m) and BRIGHTER
    // toward the sun — backlit air forward-scatters, so the corridor ahead
    // glows instead of fading into a flat grey wall. Kept deliberately
    // WEAK: the first version of this term turned the entire mid-distance
    // (road, shoulders, house bases) into one featureless cream sheet, which
    // is exactly the "washed out" complaint.
    float hFall = exp(-max(P.y, 0.0) * 0.11);
    float fog = 1.0 - exp(-dist * 0.0018 * hFall);
    fog += 0.09 * (1.0 - exp(-dist * 0.020));
    float toSun = max(dot(-V, L), 0.0);
    vec3 hazeCol = hazeColFor(base, L, V);
    col = mix(col, hazeCol, clamp(fog, 0.0, 0.70));

    // analytic silhouette veil: poles and wires are seen THROUGH the haze,
    // so a thin dark shape never reads as pure black against a bright sky
    float silh = clamp(1.0 - dot(base, vec3(0.333)) * 2.6, 0.0, 1.0);
    col = mix(col, vec3(0.52, 0.30, 0.14), silh * 0.16);

    // gentle highlight knee (only compresses the TOP end; dark values pass)
    col = clamp(col, 0.0, 4.0);
    col = col / (col * 0.35 + vec3(0.72));
    // ---- BUILD-P10: THE GRADE. A physically shaded frame still looks rendered
    // until it is graded like a photograph, and the grade is nearly free. Two
    // moves: a SPLIT TONE (cool blue in the shadows, warm in the highlights —
    // the signature of film stock and of every real dusk photograph, and the
    // cue that tells the eye "camera" rather than "renderer"), and a small
    // saturation lift, because the per-channel atmosphere model pulls colour
    // out of everything it touches.
    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(col, col * vec3(0.93, 0.98, 1.13),
              (1.0 - smoothstep(0.02, 0.34, lum)) * 0.50);   // cool shadows
    col = mix(col, col * vec3(1.07, 1.01, 0.92),
              smoothstep(0.52, 1.00, lum) * 0.45);          // warm highlights
    col = clamp(mix(vec3(lum), col, 1.12), 0.0, 1.0);
    // tiny dither: the haze gradient is a wide smooth ramp and 8-bit output
    // bands visibly on the corridor floor. BUILD-P15: 1.2 -> 3.0 levels. A
    // 3-level triangular dither is below the visible banding threshold on a
    // smooth ramp but is enough to break the 8-bit contour lines that made
    // the sky and the haze read as "pixellated" rather than as film grain.
    col += (hash21(gl_FragCoord.xy + fract(uTime) * 13.0)
            + hash21(gl_FragCoord.xy * 1.7 + 41.0) - 1.0) * (1.5 / 255.0);
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
