#version 330 core
// Fragment shader for the sea surface.
//
// Structure intentionally mirrors the phases of a shader_model 1.4 pixel
// shader (as used by the 3DMark2001 SE "Nature" ocean):
//   1) addressing  : sample ripple gradient texture -> perturbation vector
//   2) dependent read: perturbed coords -> environment reflection lookup
//   3) blend/address: add sun glitter, blend with deep-water color
// Six texture fetches per phase is the PS1.4 budget; this uses far fewer.
//
// Noise discipline: every term here is analytic or texture-chaos MODULATED by
// analytic gates. Free-floating glow terms (not tied to the sun path, cloud
// shadows or crest height) show up as pale-teal speckle over the dark water —
// they were the "light blue noise" artifact and must stay gated.
//
// Realism layer (this pass): on top of the displaced swell banks the shading
// adds two octaves of ANALYTIC detail wavelets (extra normals, tiny amplitude,
// no vertex cost), slope-gated crest foam (it appears where waves actually
// face the light and break — not as a uniform tile), sun-tinted glitter, and
// a sky-tinted horizon sheen so the far sea is a mirror of the sky band above
// it rather than dark water with a haze knob.

in vec3 vWorld;
in vec2 vUV;

uniform sampler2D uRippleTex;    // two-channel ripple gradient (RG8)
uniform samplerCube uSkyEnvTex;  // sky environment cubemap (RGBA16F)
uniform sampler2D uFoamTex;      // tileable foam / caustic detail
uniform float uTime;
uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform vec3  uHorizonColor;
uniform vec3  uWaterColor;

#define MAX_CLOUDS 7             // must match sky_frag.glsl and scene2.cxx
uniform int   uCloudCount;
uniform float uCloudAzim[MAX_CLOUDS];   // centre azimuth, radians
uniform float uCloudElev[MAX_CLOUDS];   // centre elevation, radians
uniform float uCloudRadius[MAX_CLOUDS]; // angular half-height, radians
uniform float uCloudStretch[MAX_CLOUDS];// azimuthal elongation (>1 = wider than tall)

out vec4 fragColor;

const float PI = 3.14159265359;

// BUILD-P21: ONE DISPLAY TRANSFORM, SHARED WITH THE SKY DOME.
// Kept byte-identical in shaders/ps14/sky_frag.glsl, for the same reason the
// pool and pole scenes keep byte-identical copies: the sea reflects the sky out
// of the HDR cubemap and then runs the result through this function ITSELF, so
// any difference between the two curves shows up immediately as a reflection
// that does not match the thing it is reflecting.
//
// Before this pass the dome tone mapped with per-channel Reinhard PLUS a warm
// highlight roll-off and the sea tone mapped with per-channel Reinhard and NO
// roll-off, so the horizon band and the sun path came back out of the water
// visibly different from the sky directly above them - which is the one thing
// a mirror cannot get wrong and still look like a mirror.
vec3 encodeScene(vec3 hdr, float dith) {
    // ACES (Narkowicz) shoulder: rolls the sun disc and the glitter path off
    // instead of letting them clip into flat paper.
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    // EXPOSURE. ACES is not a like-for-like replacement for Reinhard: at a
    // scene value of 0.2 it returns 0.30 where Reinhard returned 0.17, so
    // swapping the curve in with no compensating stop lifted this frame's
    // mean luma from 0.299 to 0.375 and pushed p95 from 0.653 to 0.833. The
    // dusk sea is the DARKEST thing in a dusk frame and has to stay that way,
    // so the curve change is paid for here, once, in one place both passes
    // share, rather than by walking every radiance in the scene.
    hdr *= 0.70;
    vec3 x = clamp(max(hdr, vec3(0.0)), 0.0, 8.0);
    x = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);

    // Dusk highlight roll-off. A real sun does not go white, it goes through
    // orange on its way to being clipped; per-channel tonemapping alone sends
    // the brightest 2% of a saturated orange frame straight to paper.
    float lum0 = dot(x, vec3(0.2126, 0.7152, 0.0722));
    const float warmStart = 0.45;
    if (lum0 > warmStart) {
        float f = clamp((lum0 - warmStart) / 0.20, 0.0, 1.0);
        f = f * f;
        vec3 warm = vec3(1.0, 0.62, 0.36);
        warm *= lum0 / max(dot(warm, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
        x = mix(x, warm, f);
    }

    vec3 g = pow(max(x, vec3(0.0)), vec3(1.0 / 2.2));

    // ---- THE GRADE, and it is applied in DISPLAY space on purpose. Gamma
    // encoding roughly HALVES apparent saturation: a 0.52 channel ratio in
    // linear arrives near 0.28 on screen. A lift applied BEFORE the gamma is
    // therefore mostly cancelled by it, which is why the whole dusk-ocean
    // frame measured a mean saturation of 0.130 - a brown-grey mush rather
    // than a sunset - while the same lift applied post-gamma is worth roughly
    // twice as much. (The pole scene grades pre-gamma against a much hotter
    // palette and gets away with it; this palette cannot.)
    float lum = dot(g, vec3(0.2126, 0.7152, 0.0722));
    g = mix(g, g * vec3(0.93, 0.98, 1.13),
            (1.0 - smoothstep(0.02, 0.34, lum)) * 0.50);   // cool shadows
    g = mix(g, g * vec3(1.07, 1.01, 0.92),
            smoothstep(0.52, 1.00, lum) * 0.45);          // warm highlights
    g = clamp(mix(vec3(lum), g, 1.34), 0.0, 1.0);

    // Two-tap triangular dither. The dusk gradient is the widest smooth ramp
    // in the project - a whole ocean fading into a whole sky - and 8-bit
    // output draws visible contour lines across exactly that ramp.
    g += dith * (1.5 / 255.0);
    return clamp(g, 0.0, 1.0);
}

// BUILD-P21: octave resolution gate, the same rule the pole scene uses
// (BUILD-P15). An octave whose wavelength is smaller than the pixel that lands
// on it cannot be shaded, only aliased, so it is faded out over exactly the
// band where it stops being resolvable and not one step earlier. `foot` is the
// fragment's world-space footprint in metres; `freq` is the octave's frequency
// in cycles per metre.
float octaveRes(float foot, float freq) {
    return 1.0 - smoothstep(0.35, 1.10, foot * freq);
}

// NORMAL FIELD: smooth sines (finite differences of this drive the lighting).
// Scale/speed of each line must match the DISPLACEMENT field in sea_vert.glsl
// so crest banks light up where the geometry actually rises. The displacement
// shader additionally sharpens crests and adds two long swells; that asymmetry
// is deliberate (sharp banks, smooth lighting).
//
// BUILD-P21: each octave is gated on the fragment's own world footprint. The
// gate values are computed ONCE per fragment in main() and passed in, so all
// three finite-difference taps share them: six smoothsteps per pixel, not
// eighteen, and — more to the point — a fragment two hundred metres out gets a
// normal that is the AVERAGE slope of the water inside its pixel instead of
// the slope at one arbitrary point inside it. Those are different numbers, and
// the second one is what made the far sea shimmer.
float waveHeight(vec2 p, float t, vec4 rA, vec2 rB) {
    float h = 0.0;
    h += sin(dot(p, vec2(0.98,  0.20)) * 0.170 + t * 1.30) * 1.55 * rA.x;
    h += sin(dot(p, vec2(-0.64,  0.77)) * 0.240 + t * 1.60) * 1.00 * rA.y;
    h += sin(dot(p, vec2( 0.36, -0.93)) * 0.380 + t * 2.10) * 0.55 * rA.z;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.34 * rA.w;
    h += sin(dot(p, vec2( 0.59,  0.81)) * 0.860 + t * 3.40) * 0.20 * rB.x;
    h += sin(dot(p, vec2(-0.20,  0.98)) * 1.450 + t * 4.40) * 0.11 * rB.y;
    return h;
}

// CLOUD SHADOWS: intersect each water fragment's sun ray with the same 620 m
// cloud deck used by the sky. The eight 3D lobes, morph, and boundary erosion
// below intentionally match sky_frag.glsl, so the moving shadow footprint
// tracks the visible cloud instead of being a separate decorative mask.
// BUILD-P21: also the dither source for encodeScene() below.
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float cloudValueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float cloudErosion(vec2 p) {
    float n = 0.57 * cloudValueNoise(p);
    p = mat2(0.80, -0.60, 0.60, 0.80) * p * 2.03 + 17.1;
    n += 0.29 * cloudValueNoise(p);
    p = mat2(0.80, -0.60, 0.60, 0.80) * p * 2.01 + 11.7;
    return n + 0.14 * cloudValueNoise(p);
}

float cloudSmoothMax(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (a - b) / k, 0.0, 1.0);
    return mix(b, a, h) + k * h * (1.0 - h);
}

float projectedCloudDensity(vec3 dir) {
    float density = 0.0;
    dir = normalize(dir);
    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (i >= uCloudCount) break;
        vec3 c = vec3(cos(uCloudElev[i]) * cos(uCloudAzim[i]),
                      sin(uCloudElev[i]),
                      cos(uCloudElev[i]) * sin(uCloudAzim[i]));
        vec3 delta = dir - c;
        vec3 t1 = normalize(vec3(-sin(c.z), 0.0, cos(c.z)));
        vec3 t2 = normalize(cross(c, t1));
        vec3 p = vec3(dot(delta, t1) / max(uCloudStretch[i], 1.0),
                      dot(delta, t2), dot(delta, c));
        float R = max(uCloudRadius[i], 0.001);
        if (dot(p, p) > R * R * 3.1) continue;

        const int PUFFS = 8;
        vec3 offsets[PUFFS];
        offsets[0] = vec3( 0.00,  0.00,  0.00);
        offsets[1] = vec3( 0.68,  0.02,  0.03);
        offsets[2] = vec3(-0.62,  0.10, -0.04);
        offsets[3] = vec3( 0.25,  0.28,  0.06);
        offsets[4] = vec3(-0.27,  0.35, -0.02);
        offsets[5] = vec3( 0.05,  0.53,  0.08);
        offsets[6] = vec3( 0.43, -0.12, -0.07);
        offsets[7] = vec3(-0.40, -0.10,  0.05);
        float radii[PUFFS];
        radii[0] = 0.55; radii[1] = 0.39; radii[2] = 0.37; radii[3] = 0.34;
        radii[4] = 0.31; radii[5] = 0.27; radii[6] = 0.30; radii[7] = 0.29;

        float local = 0.0;
        for (int j = 0; j < PUFFS; j++) {
            vec3 q = p - offsets[j] * R;
            float ang = atan(q.y, q.x);
            float morph = 0.045 * sin(uTime * 0.10 + uCloudAzim[i] * 9.0 + float(j) * 1.7)
                        + 0.035 * cos(uTime * 0.065 + uCloudAzim[i] * 5.0 + float(j) * 2.9);
            float rj = R * radii[j] * (1.0
                + 0.11 * sin(ang * 3.0 + uCloudAzim[i] * 7.0 + float(j) * 2.1)
                + 0.065 * sin(ang * 5.0 - uCloudAzim[i] * 11.0 + float(j) * 4.7)
                + morph);
            float lobeNoise = cloudErosion(vec2(
                cos(ang) * 1.8 + uCloudAzim[i] * 3.7,
                sin(ang) * 1.8 + float(j) * 2.1 + uCloudElev[i] * 9.0));
            rj *= 0.91 + 0.16 * lobeNoise;
            vec3 metric = vec3(q.x / rj, q.y / (rj * 0.92), q.z / (rj * 1.18));
            local = cloudSmoothMax(local, 1.0 - smoothstep(0.62, 1.12, length(metric)), 0.10);
        }

        vec2 envelopeP = p.xy / R;
        float ang = atan(envelopeP.y, envelopeP.x);
        float envelopeRadius = length(envelopeP)
            * (1.0 + 0.055 * sin(ang * 4.0 + uCloudAzim[i] * 13.0));
        float envelope = 1.0 - smoothstep(0.96, 1.56, envelopeRadius);
        float n = cloudErosion(envelopeP * 3.2 + vec2(uCloudAzim[i] * 5.1, i * 7.3));
        float fine = cloudErosion(envelopeP * 7.8 + vec2(uCloudAzim[i] * 11.0, i * 13.0));
        float shoulder = 1.0 - smoothstep(0.10, 0.82, local);
        local = smoothstep(0.035, 0.78,
               local * (0.70 + 0.30 * n + 0.12 * fine - 0.20 * shoulder))
               * envelope;
        float baseCut = smoothstep(-0.78, -0.48, envelopeP.y + (n - 0.5) * 0.18);
        local *= baseCut * (1.0 - 0.12 * smoothstep(0.48, 0.95, envelopeP.y));
        density = cloudSmoothMax(density, clamp(local, 0.0, 1.0), 0.07);
    }
    return clamp(density, 0.0, 1.0);
}

float cloudShadow(vec3 world, vec3 sd) {
    float travel = max((620.0 - world.y) / max(sd.y, 0.15), 0.0);
    vec3 hitDir = normalize(world - uEyePos + sd * travel);
    float local = projectedCloudDensity(hitDir);
    return clamp(exp(-local * 2.4), 0.12, 1.0);
}

// Three octaves of small analytic wavelets: extra normal detail that would
// be wasted (and aliased) as vertex displacement, but sells micro-chop up
// close. BUILD-P21: the old fade was exp(-dist * 0.004), a distance guess that
// is wrong the moment the camera changes height or pitch — the same reason
// scene 4 moved its gates onto resolution. These run on the footprint instead.
void detailNormals(vec2 p, float t, float foot, inout vec2 grad) {
    float g1 = octaveRes(foot, 0.183);    // K = 1.15 / 2pi
    float g2 = octaveRes(foot, 0.366);    // K = 2.30 / 2pi
    float g3 = octaveRes(foot, 0.700);    // K = 4.40 / 2pi
    if (g1 + g2 + g3 < 0.02) return;
    float w1 = sin(dot(p, vec2(0.86, 0.51)) * 1.15 + t * 5.10);
    float w2 = sin(dot(p, vec2(-0.44, 0.90)) * 2.30 + t * 6.80);
    float w3 = sin(dot(p, vec2(0.22, -0.97)) * 4.40 + t * 8.60);
    grad += vec2(0.86, 0.51) * 1.15 * w1 * 0.045 * g1;
    grad += vec2(-0.44, 0.90) * 2.30 * w2 * 0.022 * g2;
    grad += vec2(0.22, -0.97) * 4.40 * w3 * 0.010 * g3;
}

void main() {
    vec3 sd = normalize(uSunDir);
    vec3 dir = normalize(vWorld - uEyePos);   // fragment direction (eye -> point)

    // ---- analytic wave normal (finite differences of the wave field) ----
    float dist = length(vWorld.xz - uEyePos.xz);
    // BUILD-P21: the finite-difference epsilon was a FIXED 0.35 m, so every
    // pixel sampled the wave field as a point however large its own footprint
    // was. Past a few hundred metres one pixel spans more than a wavelength and
    // the gradient came out as a difference of two nearly unrelated phases —
    // not a normal, just noise, which is exactly what "the sea shimmers at the
    // horizon" is made of. The epsilon now tracks the footprint, so the
    // gradient is an area average: the mean slope of the water inside the
    // pixel, which is the only thing the pixel can actually show.
    float foot = fwidth(vWorld.x) + fwidth(vWorld.z) + 1e-4;
    vec4 rA = vec4(octaveRes(foot, 0.02705), octaveRes(foot, 0.03820),
                   octaveRes(foot, 0.06048), octaveRes(foot, 0.08594));
    vec2 rB = vec2(octaveRes(foot, 0.13682), octaveRes(foot, 0.23079));
    float e = max(0.35, foot * 0.60);
    float hC = waveHeight(vWorld.xz, uTime, rA, rB);
    float hX = waveHeight(vWorld.xz + vec2(e, 0.0), uTime, rA, rB);
    float hZ = waveHeight(vWorld.xz + vec2(0.0, e), uTime, rA, rB);
    vec2 grad = vec2(-(hX - hC) / e, -(hZ - hC) / e);
    detailNormals(vWorld.xz, uTime, foot, grad);
    vec3 N = normalize(vec3(grad.x, 1.0, grad.y));

    // ---- phase 1: addressing - ripple normal map, scrolled over the surface
    float rippleFade = exp(-dist * 0.0018);           // ripples die out far away
    vec2 uvA = vUV * 40.0 + vec2(uTime * 0.0040, uTime * 0.0031);
    vec2 uvB = vUV * 97.0 - vec2(uTime * 0.0026, uTime * 0.0042);
    vec2 ripA = texture(uRippleTex, uvA).rg;
    vec2 ripB = texture(uRippleTex, uvB).rg;
    vec2 pert = (ripA + ripB) * 0.5 * rippleFade;

    // ---- phase 2: dependent read - perturbed reflection of the sky ----
    vec3 V = normalize(uEyePos - vWorld);             // towards the eye
    vec3 R = reflect(-V, N);
    R = normalize(R + vec3(pert.x, 0.0, pert.y) * 1.25);
    // Reflections stretch vertically (the classic flattened-reflection trick):
    // grazing rays would otherwise hug the bright horizon band and light the
    // whole sea up. Biasing the ray up makes off-sun water reflect the dark
    // upper sky while the sun glitter path stays put (it is a separate term).
    R.y = abs(R.y) * 0.30 + 0.45;
    R = normalize(R);
    // azimuthal smear toward the sun: only rays near the SUN azimuth keep
    // crisp reflections; off-path rays mirror-blend toward the dark upper sky
    // so the glow column stays narrow and the sides read deep blue/purple
    vec3 L = normalize(uSunDir);
    vec2 sunXZ = normalize(L.xz);
    vec2 dirXZ = normalize(vWorld.xz - uEyePos.xz + vec2(1e-4));
    float sunAlign = max(dot(dirXZ, sunXZ), 0.0);
    // BUILD-P21: hoisted. The sky colour just above this fragment's own
    // horizon is needed in three places — as the foam's ambient light, as the
    // far-sea mirror target and as the haze target — and it used to be
    // sampled after the foam that wanted it.
    vec2 dxdz = vWorld.xz - uEyePos.xz;
    vec3 skyAtHorizon = texture(uSkyEnvTex,
        normalize(vec3(dxdz.x, 0.012, dxdz.y))).rgb;
    vec3 Rdark = normalize(vec3(R.x, abs(R.y) * 1.8 + 0.62, R.z)); // steep: upper sky
    R = normalize(mix(Rdark, R, pow(sunAlign, 6.0)));

    // Roughness-matched reflection LOD: the sun disc occupies a handful of
    // cubemap texels, and sampling them at LOD 0 mirrors as small SQUARE
    // patches on the water. Wave facets are rough at every distance, so the
    // reflection blurs with range — which also smears the sun into a soft
    // vertical glow (real water behaviour) instead of texel squares.
    float reflDist = dist;
    vec3 reflColor = textureLod(uSkyEnvTex, R, clamp(1.5 + reflDist * 0.0012, 1.0, 5.0)).rgb;
    float offSun = 1.0 - smoothstep(0.08, 0.45, sunAlign);
    reflColor *= mix(1.0, 0.46, offSun);

    // ---- fresnel: sea is a mirror at grazing angles, glass straight down ----
    float NdV = max(dot(N, V), 0.0);
    float fresnel = 0.022 + 0.978 * pow(1.0 - NdV, 5.0);
    // stronger sky reflections: at dusk the whole water surface reads as a
    // dark mirror of the sky — lift the base fresnel and clamp the minimum
    // reflectance higher than the physical 2%
    fresnel = clamp(fresnel * 1.25 + 0.045, 0.0, 0.92);

    // ---- water body: near-black purple deep, warmed by the sky band ----
    vec3 body = uWaterColor * 0.55 + uHorizonColor * 0.03;

    // directional sun lighting on the wave slopes: faces tilted toward the
    // low sun glow warm, backslopes fall to near-black — this is what makes
    // the sea read as lit by the same sun as the sky instead of pasted on.
    // Prefaced by an azimuth gate so the WARM slope light lives inside the
    // sun path; off-path water stays deep blue/purple.
    float sunDiffuse = max(dot(N, L), 0.0);
    float warmGate = pow(sunAlign, 3.0);

    // cloud shadows: the projected puffs gate ALL direct sun terms
    float shadow = cloudShadow(vWorld, sd);

    // MUCH darker water where the sun's light doesn't reach: off-path base
    // drops to near-black indigo, and cloud shadows multiply direct light
    body *= 0.44 + 0.40 * sunDiffuse * warmGate * shadow + 0.12 * sunDiffuse * (0.35 + 0.65 * shadow);
    body *= mix(0.40, 1.0, warmGate * shadow + (1.0 - warmGate) * 0.25 * shadow); // dark off-path + shadowed body
    body *= mix(0.52, 1.0, 1.0 - offSun);
    body += vec3(1.05, 0.42, 0.20) * pow(sunDiffuse, 3.0) * warmGate * shadow * 0.42; // warm slopes in the path

    // ---- slope-gated crest foam ----
    // Foam only where the shading says waves actually BREAK: a height band
    // AND the slope facing the sun/light AND some ripple chaos — the noise
    // tile alone would foam uniformly everywhere, which reads fake.
    // Cloud shadows gate foam too: nothing breaks where light doesn't reach.
    float crest = smoothstep(0.55, 1.25, hC) * mix(0.25, 1.0, shadow);
    float slopeFacing = max(dot(N, L), 0.0) * warmGate + 0.15;
    float chaos = texture(uRippleTex, vUV * 190.0 + vec2(uTime * 0.011, -uTime * 0.007)).g;
    float foam = texture(uFoamTex, vUV * 23.0 + vec2(uTime * 0.010, 0.0)).r;
    foam *= texture(uFoamTex, vUV * 41.0 - vec2(0.0, uTime * 0.013)).g;
    // BUILD-P21: a breaking crest is the finest thing in the frame — it is
    // genuinely sub-pixel past a few hundred metres — so it now runs on the
    // same footprint gate as the wave octaves instead of painting a full-
    // contrast speckle pattern out to the horizon. Real whitecaps merge into
    // a pale band long before they stop being individually visible.
    foam *= octaveRes(foot, 0.62);
    foam *= crest * slopeFacing * (0.35 + 0.65 * chaos);
    // BUILD-P21: foam is not a grey constant. It is a rough near-white surface
    // a few centimetres thick, so what it mostly returns to the eye is the sky
    // directly above it, plus whatever sun actually reaches it. Lighting it
    // from a hard-coded tint is what made breaking crests read as confetti
    // stuck onto the water rather than as water thrown into the air.
    vec3 foamLit = skyAtHorizon * 0.62
                 + vec3(1.0, 0.50, 0.24) * (sunDiffuse * shadow) * 0.40;
    body += foamLit * foam * 0.62;

    // subsurface glow against the light: THIN CRESTS transmit a dim jade-green
    // where sunlight actually passes through the water. Gated to the sun's
    // azimuth AND to un-shadowed sun — ungated it speckled pale-teal noise
    // across the dark off-path sea (the light-blue artifact).
    body += vec3(0.05, 0.18, 0.14) * sunAlign * warmGate * shadow * crest * 0.55;

    vec3 color = mix(body, reflColor, fresnel);

    // ---- horizon sheen: the far sea is a MIRROR of the sky band above it
    // (grazing fresnel -> nearly pure reflection up there), so by geometry
    // the water edge converges into the sky with no haze knob needed.
    float horizonMix = smoothstep(1500.0, 1900.0, dist);
    color = mix(color, skyAtHorizon, horizonMix * 0.85);

    // ---- aerial haze: near water stays dark and readable, while the far
    // sea melts into the horizon. The haze target is the ACTUAL sky colour
    // at this fragment's azimuth — sampled from the env cubemap just above
    // the horizon line — so the far edge of the patch converges into the sky
    // band above it (bright glow on the sun side, dark maroon away from it).
    float haze = 1.0 - exp(-dist * 0.00075);
    color = mix(color, skyAtHorizon, haze * (0.30 + 0.70 * haze));

    // ---- phase 3: address + blend - sun glitter path, as a REAL MICROFACET
    // DISTRIBUTION. Before BUILD-P21 the path was three hand-fitted
    // pow(N·H, n) lobes with FIXED exponents: a "pinpoint sparkle" 520 wide is
    // sub-pixel past a few hundred metres, so the far half of the glitter path
    // was per-pixel noise the eye reads as shimmer, while the near half was
    // blown-out white beads. Physically the lobe WIDENS with distance — many
    // facets inside one pixel average into a smooth glare — so roughness is
    // driven by the same footprint that gates the wave octaves, and one
    // GGX + Smith + Fresnel term does the work of all three lobes.
    vec3 H = normalize(L + V);
    float NdH = clamp(dot(N, H), 0.0, 1.0);
    float NoV = clamp(dot(N, V), 1e-3, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    float rough = clamp(0.085 + foot * 0.016, 0.085, 0.30);
    float alpha = rough * rough;                 // GGX: alpha = roughness^2
    float a2 = alpha * alpha;
    float dd = NdH * NdH * (a2 - 1.0) + 1.0;
    float Dg = a2 / (3.14159265 * dd * dd);      // GGX / Trowbridge-Reitz NDF
    // Smith height-correlated visibility (the same form the pool scene uses, so
    // the project shades water one way everywhere).
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl2 = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    float Vis = 0.5 / max(gv + gl2, 1e-4);
    // Fresnel-Schlick at water's F0. pow5 written as a squared-cubed chain.
    float fq = 1.0 - VoH;
    float fq2 = fq * fq;
    float Fr = 0.02 + 0.98 * (fq2 * fq2 * fq);
    float spR = Dg * Vis * Fr / (4.0 * NoV * max(NoL, 1e-3));
    // Bounded roll-off. A mirror sun on a facet is orders of magnitude over 1.0;
    // the rational form keeps that energy in the frame without letting it clip
    // into a flat white bead (the same fix the pole scene's P19 specular got).
    float sp = spR / (1.0 + spR * 0.235);
    // Gated to the sun's azimuth column so the glow stays a NARROW path with
    // dark water either side, killed inside cloud shadows, and its sparkle
    // variance rides the ripple chaos.
    float pathGate = pow(sunAlign, 10.0) * 0.96 + 0.04;
    float sparkleGate = (0.55 + 0.90 * chaos);
    color += vec3(1.0, 0.56, 0.24) * sp * 0.42
             * (0.25 + max(L.y, 0.0) * 1.2) * pathGate * shadow * sparkleGate;

    // ---- SHARED DISPLAY TRANSFORM (identical to the sky dome's) ----------
    float dith = (hash21(gl_FragCoord.xy + fract(uTime) * 13.0)
                 + hash21(gl_FragCoord.xy * 1.7 + 41.0) - 1.0) * 0.5;
    fragColor = vec4(encodeScene(color, dith), 1.0);
}
