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

    vec3 base = vColor;
    float rough = 0.85;     // 0 = mirror, 1 = chalk
    float sheen = 0.0;      // extra grazing term (cables, glass)

    if (m == kMatGround) {
        // dirt/gravel: broad damp patches + fine grit, then a dry-grass
        // tint where the broad noise runs high (weeds in the verges).
        // BUILD-P8: the amplitudes are up and the base albedo is down — a
        // single bright flat plane was reading as blown-out white paper in
        // the mid-distance, which is the "washed out" complaint.
        float n1 = vnoise(P.xz * 0.42);
        float n2 = vnoise(P.xz * 2.10);
        float n3 = near > 0.02 ? vnoise(P.xz * 7.5) : 0.5;
        base *= 0.66 + 0.52 * n1 + 0.26 * n2 + 0.14 * n3 * near;
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
        if (near > 0.02)
            base += (vnoise(P.xz * 11.0) - 0.5) * 0.09 * near;
        // a darker seam down the very centre where the tar is oldest
        base *= 1.0 - 0.18 * exp(-pow(rx / 0.55, 2.0));
        rough = mix(0.95, 0.62, wp);          // polished paths catch the sun
    } else if (m == kMatLine) {
        // worn road paint: chipped at the edges, dirty in the middle
        float wear = near > 0.02 ? vnoise(P.xz * 6.0) : 0.5;
        base *= 0.78 + 0.34 * wear * near + 0.18 * (1.0 - near);
        rough = 0.85;
    } else if (m == kMatWood) {
        // grain runs ALONG the trunk: squash x/z, stretch y
        float along = P.y * 1.15 + (P.x + P.z) * 0.06;
        float g1 = vnoise(vec2((P.x + P.z) * 6.5, along));
        float g2 = near > 0.02 ? vnoise(vec2((P.x + P.z) * 19.0, along * 2.6))
                               : 0.5;
        base *= 0.80 + 0.28 * g1 + 0.16 * g2 * near;
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
        float spangle = near > 0.02 ? vnoise(vec2((P.x + P.z) * 26.0, P.y * 3.0))
                                   : 0.5;
        base *= 0.80 + 0.30 * streak + 0.20 * spangle * near;
        base = mix(base, base * 1.24 + vec3(0.028, 0.029, 0.029),
                   smoothstep(0.55, 1.0, streak) * 0.55);
        float rustN = vnoise(vec2((P.x + P.z) * 2.60, P.y * 0.90));
        float rust = smoothstep(1.80, 0.25, P.y) * smoothstep(0.42, 0.86, rustN);
        base = mix(base, vec3(0.135, 0.060, 0.030), rust * 0.70);
        rough = mix(0.62, 0.34, smoothstep(0.25, 0.85, streak)) + rust * 0.25;
        sheen = 0.42;
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

    // specular: shininess from roughness, scaled down for chalky materials
    vec3 H = normalize(L + V);
    float shin = mix(6.0, 220.0, 1.0 - rough);
    float sp = pow(max(dot(N, H), 0.0), shin) * (1.0 - rough * 0.85) * 1.6;

    // grazing rim: wires glint along their whole length against the sun
    float edge = 1.0 - abs(dot(N, V));
    float rim = pow(edge, 6.0) * sheen;

    vec3 col = base * (amb + sunTint * diff * 1.15) + sunTint * (sp + rim);

    // ---- contact darkening -------------------------------------------------
    // Everything standing on the ground picks up a contact gradient: the
    // house walls darken into their own footprint, the pole bases sit in
    // dirt. Ground/road/paint are excluded (they ARE the floor) and so are
    // the wires (they never touch).
    if (m != kMatGround && m != kMatRoad && m != kMatLine)
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
    // tiny dither: the haze gradient is a wide smooth ramp and 8-bit output
    // bands visibly on the corridor floor
    col += (hash21(gl_FragCoord.xy + fract(uTime) * 13.0) - 0.5) * (1.2 / 255.0);
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
