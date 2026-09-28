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

#define MAX_CLOUDS 9             // must match sky_frag.glsl and tidebench.cxx
uniform int   uCloudCount;
uniform float uCloudPhase[MAX_CLOUDS]; // cloud-local aging frame, matches sky_frag.glsl
uniform float uCloudAzim[MAX_CLOUDS];   // centre azimuth, radians
uniform float uCloudElev[MAX_CLOUDS];   // centre elevation, radians
uniform float uCloudRadius[MAX_CLOUDS]; // angular half-height, radians
uniform float uCloudStretch[MAX_CLOUDS];// azimuthal elongation (>1 = wider than tall)

out vec4 fragColor;

const float PI = 3.14159265359;

// NORMAL FIELD: smooth sines (finite differences of this drive the lighting).
// Scale/speed of each line must match the DISPLACEMENT field in sea_vert.glsl
// so crest banks light up where the geometry actually rises. The displacement
// shader additionally sharpens crests and adds two long swells; that asymmetry
// is deliberate (sharp banks, smooth lighting).
//
// Split NEAR/FAR: the three short waves (k = 0.54..1.45 rad/m) alias into
// one-pixel normal spikes once their screen wavelength collapses — on lit
// slopes the glitter then fires isolated WHITE PIXELS across the sun path
// (the salt-and-pepper artifact). The far build keeps only k <= 0.38 waves;
// the short build fades out with distance exactly like micro-chop should.
float waveHeightFar(vec2 p, float t) {
    float h = 0.0;
    h += sin(dot(p, vec2(0.98,  0.20)) * 0.170 + t * 1.30) * 1.55;
    h += sin(dot(p, vec2(-0.64,  0.77)) * 0.240 + t * 1.60) * 1.00;
    h += sin(dot(p, vec2( 0.36, -0.93)) * 0.380 + t * 2.10) * 0.55;
    return h;
}

float waveHeightNear(vec2 p, float t) {
    float h = 0.0;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.34;
    h += sin(dot(p, vec2( 0.59,  0.81)) * 0.860 + t * 3.40) * 0.20;
    h += sin(dot(p, vec2(-0.20,  0.98)) * 1.450 + t * 4.40) * 0.11;
    return h;
}

float waveHeight(vec2 p, float t) {
    return waveHeightFar(p, t) + waveHeightNear(p, t);
}

// CLOUD SHADOWS: intersect each water fragment's sun ray with the same 620 m
// cloud deck used by the sky. The eight 3D lobes, morph, and boundary erosion
// below intentionally match sky_frag.glsl, so the moving shadow footprint
// tracks the visible cloud instead of being a separate decorative mask.
float cloudHash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float cloudValueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = cloudHash21(i);
    float b = cloudHash21(i + vec2(1.0, 0.0));
    float c = cloudHash21(i + vec2(0.0, 1.0));
    float d = cloudHash21(i + vec2(1.0, 1.0));
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

float projectedCloudDensity(vec3 dir, float detailFade) {
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
            // Same cloud-local aging phase as the sky: the shadow silhouette
            // evolves with the bank instead of counter-scrolling against it.
            float ph = uCloudPhase[i];
            float morph = 0.045 * sin(ph + float(j) * 1.7)
                        + 0.035 * cos(ph * 0.65 + float(j) * 2.9);
            float rj = R * radii[j] * (1.0
                + 0.11 * sin(ang * 3.0 + uCloudAzim[i] * 7.0 + float(j) * 2.1)
                + 0.065 * sin(ang * 5.0 - uCloudAzim[i] * 11.0 + float(j) * 4.7)
                + morph);
            float lobeNoise = cloudErosion(vec2(
                cos(ang) * 1.8 + uCloudAzim[i] * 3.7,
                sin(ang) * 1.8 + float(j) * 2.1 + uCloudElev[i] * 9.0));
            lobeNoise = mix(0.5, lobeNoise, detailFade); // far range: smooth silhouette
            rj *= 0.91 + 0.16 * lobeNoise;
            // The wisp squash must match sky_frag.glsl exactly so the shadow
            // footprint tracks the visible streak.
            vec3 metric = vec3(q.x / rj, q.y / (rj * 0.92), q.z / (rj * 1.18)) * vec3(1.0, 1.0, i >= 7 ? 0.55 : 1.0);
            local = cloudSmoothMax(local, 1.0 - smoothstep(0.62, 1.12, length(metric)), 0.10);
        }

        vec2 envelopeP = p.xy / R;
        float ang = atan(envelopeP.y, envelopeP.x);
        float envelopeRadius = length(envelopeP)
            * (1.0 + 0.055 * sin(ang * 4.0 + uCloudAzim[i] * 13.0));
        float envelope = 1.0 - smoothstep(0.96, 1.56, envelopeRadius);
        float n = cloudErosion(envelopeP * 3.2 + vec2(uCloudAzim[i] * 5.1, i * 7.3));
        float fine = cloudErosion(envelopeP * 7.8 + vec2(uCloudAzim[i] * 11.0, i * 13.0));
        // SPECKLE ROOT FIX: the erosion octaves carry sub-pixel detail once a
        // distant fragment's water footprint projects to a tiny angular patch
        // — the shadow boundary then flickers per pixel, and since shadow
        // gates every sun term the lit wave faces break into salt-and-pepper
        // dots. Collapse the boundary noise toward its mean with range; near
        // water keeps the full ragged shadow edges.
        n = mix(0.5, n, detailFade);
        fine = mix(0.5, fine, detailFade);
        float shoulder = 1.0 - smoothstep(0.10, 0.82, local);
        local = smoothstep(0.06, 0.74,
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
    // detail fade rides the FRAGMENT's water distance (the footprint that
    // aliases is on the water, not up at the deck)
    float detailFade = exp(-length(world.xz - uEyePos.xz) * 0.0025);
    float local = projectedCloudDensity(hitDir, detailFade);
    return clamp(exp(-local * 2.4), 0.12, 1.0);
}

// Two octaves of small analytic wavelets: extra normal detail that would
// be wasted (and aliased) as vertex displacement, but sells micro-chop up
// close. The third octave was removed for good: its ~2.4 m wavelength lands
// as half-bright teal squiggles on the dark sea in motion — the "small light
// blue waves" artifact. Two slow octaves keep the chop without the scum.
void detailNormals(vec2 p, float t, float dist, inout vec2 grad) {
    float fade = exp(-dist * 0.004);   // micro-chop is a NEAR-camera feature
    if (fade < 0.02) return;
    float w1 = sin(dot(p, vec2(0.86, 0.51)) * 1.15 + t * 5.10);
    float w2 = sin(dot(p, vec2(-0.44, 0.90)) * 2.30 + t * 6.80);
    grad += vec2(0.86, 0.51) * 1.15 * w1 * 0.040;
    grad += vec2(-0.44, 0.90) * 2.30 * w2 * 0.020;
}

void main() {
    vec3 sd = normalize(uSunDir);
    vec3 dir = normalize(vWorld - uEyePos);   // fragment direction (eye -> point)

    // ---- analytic wave normal (finite differences of the wave field) ----
    float dist = length(vWorld.xz - uEyePos.xz);
    float e = 0.35;
    // Distance-faded finite differences: the long waves always shade, the
    // short waves fade out over ~500 m BEFORE they alias. Evaluating the far
    // and near builds separately keeps 6 taps (3+3 per axis pair shared).
    float nearFade = exp(-dist * 0.0022);
    float hC = waveHeightFar(vWorld.xz, uTime) + waveHeightNear(vWorld.xz, uTime) * nearFade;
    float hX = waveHeightFar(vWorld.xz + vec2(e, 0.0), uTime)
             + waveHeightNear(vWorld.xz + vec2(e, 0.0), uTime) * nearFade;
    float hZ = waveHeightFar(vWorld.xz + vec2(0.0, e), uTime)
             + waveHeightNear(vWorld.xz + vec2(0.0, e), uTime) * nearFade;
    vec2 grad = vec2(-(hX - hC) / e, -(hZ - hC) / e);
    detailNormals(vWorld.xz, uTime, dist, grad);
    vec3 N = normalize(vec3(grad.x, 1.0, grad.y));

    // ---- phase 1: addressing - ripple normal map, scrolled over the surface
    // Speckle root fix: the perturbation jitters the reflection RAY per-pixel;
    // at range, adjacent pixels land on wildly different env texels and the
    // reflection shimmers into salt-and-pepper dots. The ripple detail is a
    // NEAR-camera feature — fade it 2x faster than before so the mid/far sea
    // reflects smoothly from the analytic wave normals alone.
    float rippleFade = exp(-dist * 0.0035);           // ripples die out far away
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
    vec3 Rdark = normalize(vec3(R.x, abs(R.y) * 1.8 + 0.62, R.z)); // steep: upper sky
    R = normalize(mix(Rdark, R, pow(sunAlign, 6.0)));

    // Roughness-matched reflection LOD: the sun disc occupies a handful of
    // cubemap texels, and sampling them at LOD 0 mirrors as small SQUARE
    // patches on the water. Wave facets are rough at every distance, so the
    // reflection blurs with range — which also smears the sun into a soft
    // vertical glow (real water behaviour) instead of texel squares.
    float reflDist = dist;
    // The env sun disc is HDR-hot; sampled at low LOD near the camera its
    // sharp edge lands as ISOLATED WHITE PIXELS in the middle of the lit
    // path (the firefly artifact). Lifting the LOD when R points near the
    // sun smears the disc into the soft vertical glow real water shows —
    // the glitter term owns the crisp sparkles, not the cubemap. The lift
    // is GENTLE and tightly gated: strong lifts at grazing angles blur whole
    // sparkle rows into a banded white horizon film.
    float sunLift = 0.9 * pow(max(dot(R, L), 0.0), 8.0);
    // FOOTPRINT-AWARE ENV LOD (speckle root fix): the env sun disc is HDR-hot
    // and even blurred it has an EDGE in the cubemap. Ripple-jittered rays
    // straddle that edge between neighbouring pixels, so one pixel samples
    // the disc and its neighbour the sky — a screen-space cliff that reads as
    // an isolated bright dot with dark surroundings. Widening the LOD by the
    // per-pixel ray divergence (fwidth) filters the cubemap to the ray's own
    // footprint: the disc edge becomes a multi-pixel gradient, never a dot.
    vec3 rGrad = fwidth(R);
    float rayFoot = max(rGrad.x, max(rGrad.y, rGrad.z));
    float footLod = clamp(log2(1.0 + rayFoot * 512.0), 0.0, 3.0);
    vec3 reflColor = textureLod(uSkyEnvTex, R, clamp(1.5 + reflDist * 0.0012 + sunLift + footLod, 1.0, 6.0)).rgb;
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
    // 0.55 -> 0.42: the fresnel mix blends body+reflection to ~1.0 total
    // energy; starting from 0.55 inflated the whole sea ABOVE the sky's own
    // brightness. The lost body light returns as true subsurface glow below.
    vec3 body = uWaterColor * 0.42 + uHorizonColor * 0.03;

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

    // ---- subsurface scattering: light entering a thin crest transmits
    // through and scatters back out toward the eye. Strongest when looking
    // TOWARD the sun through the wave (forward scattering), fading with view
    // angle. This is the energy the old 0.55 body factor was faking.
    float forward = max(dot(dir, -L), 0.0);
    body += vec3(0.10, 0.42, 0.30) * forward * sunDiffuse * warmGate
          * shadow * max(hC + 0.6, 0.0) * 0.55;

    // ---- slope-gated crest foam ----
    // Foam only where the shading says waves actually BREAK: a height band
    // AND the slope facing the sun/light AND some ripple chaos — the noise
    // tile alone would foam uniformly everywhere, which reads fake.
    // Cloud shadows gate foam too: nothing breaks where light doesn't reach.
    //
    // ANTI-PLASTIC BREAKUP: real white water never forms a smooth sheet
    // hugging the wave contour — that read like moulded plastic. Three
    // breakups, largest to smallest:
    //   1. the height THRESHOLD itself is warped by erosion noise, so the
    //      foam contour is ragged instead of a clean iso-line,
    //   2. an irregular patch mask clumps the band and punches holes in it,
    //   3. a drifting grain octave crumbles the surface into bubble clumps
    //      near the camera (band-limited by microFade like every per-pixel
    //      noise term here).
    float microFade = exp(-dist * 0.0035);
    float patchNoise = cloudErosion(vUV * 90.0 + vec2(uTime * 0.0010, -uTime * 0.0006));
    patchNoise = patchNoise * 0.70 + 0.30 *
        cloudErosion(vUV * 330.0 + vec2(-uTime * 0.0012, uTime * 0.0008) + 41.7);
    float crest = smoothstep(0.55, 1.25, hC + (patchNoise - 0.5) * 0.70)
                * mix(0.25, 1.0, shadow);
    float patch = smoothstep(0.42, 0.80, patchNoise);
    // 0.45 floor (not 0): mid-distance foam still clumps visibly — patches
    // are ~45 m features, far above pixel scale, so no alias risk there.
    patch = mix(0.45, patch, microFade);
    float slopeFacing = max(dot(N, L), 0.0) * warmGate + 0.15;
    float chaos = texture(uRippleTex, vUV * 190.0 + vec2(uTime * 0.011, -uTime * 0.007)).g;
    // MICRO-GATE DISTANCE FADE (speckle root fix): chaos is sampled at 190
    // tiles/UV, so past a few hundred metres one pixel spans whole texels —
    // every term it gates (sparkle gate, roughness, foam break-up) then
    // flickers per-pixel and renders as salt-and-pepper dots on the mid sea.
    // Collapse its VARIANCE toward the mean with distance: near water keeps
    // the granular chop, the mid/far sea shades smoothly from the analytic
    // wave field. (This is the same band-limiting idea as the footprint GGX:
    // never sample sub-pixel stochastic detail per-pixel.)
    chaos = mix(0.5, chaos, microFade);
    float foam = texture(uFoamTex, vUV * 23.0 + vec2(uTime * 0.010, 0.0)).r;
    foam *= texture(uFoamTex, vUV * 41.0 - vec2(0.0, uTime * 0.013)).g;
    // the 41-tile foam octave aliases the same way — dim its contrast at range
    foam *= mix(0.45, 1.0, microFade);
    // bubble grain: slow-crawling crumble inside the foam mass; at range it
    // fades to the smooth streak texture so mid-sea never aliases
    float grain = cloudErosion((vUV + vec2(uTime * 0.00030, -uTime * 0.00022)) * 1400.0);
    foam *= mix(0.60, 0.62 + 0.75 * grain, microFade);
    // LIP BIAS: entrained air is densest right at the breaking lip and washes
    // out down the bank — a flat band is what read as moulded plastic.
    float lip = mix(0.70, 1.0, smoothstep(0.85, 1.55, hC));
    foam *= crest * patch * lip * slopeFacing * (0.35 + 0.65 * chaos);
    // Foam is white water lit by the SAME sky and sun as everything else:
    // warm cream inside the sun path (shadow-gated), cool grey-violet away
    // from it. The old constant warm-grey read dirty against the dusk.
    //
    // DENSITY SHADING: a single flat sheet colour is the other half of the
    // plastic look. Shade NONLINEARLY with local foam thickness so clumped
    // cores bloom bright while ragged thin wash dims toward the water —
    // brightness now varies inside the band instead of forming a sticker.
    float dense = clamp(foam * foam * 1.55, 0.0, 1.0);
    vec3 foamCol = mix(vec3(0.38, 0.37, 0.44), vec3(1.05, 0.74, 0.52),
                       clamp(warmGate * shadow, 0.0, 1.0));
    foamCol = mix(foamCol * 0.52, foamCol * 1.08, dense);
    body += foamCol * foam * 0.72;

    // subsurface glow against the light: THIN CRESTS transmit a dim jade-green
    // where sunlight actually passes through the water. Gated to the sun's
    // azimuth AND to un-shadowed sun — ungated it speckled pale-teal noise
    // across the dark off-path sea (the light-blue artifact).
    body += vec3(0.05, 0.18, 0.14) * sunAlign * warmGate * shadow * crest * 0.55;

    vec3 color = mix(body, reflColor, fresnel);

    // ---- horizon sheen: the far sea is a MIRROR of the sky band above it
    // (grazing fresnel -> nearly pure reflection up there), so by geometry
    // the water edge converges into the sky with no haze knob needed.
    vec2 dxdz = vWorld.xz - uEyePos.xz;
    vec3 skyAtHorizon = texture(uSkyEnvTex,
        normalize(vec3(dxdz.x, 0.012, dxdz.y))).rgb;
    float horizonMix = smoothstep(1500.0, 1900.0, dist);
    color = mix(color, skyAtHorizon, horizonMix * 0.85);

    // ---- aerial haze: near water stays dark and readable, while the far
    // sea melts into the horizon. The haze target is the ACTUAL sky colour
    // at this fragment's azimuth — sampled from the env cubemap just above
    // the horizon line — so the far edge of the patch converges into the sky
    // band above it (bright glow on the sun side, dark maroon away from it).
    float haze = 1.0 - exp(-dist * 0.00075);
    color = mix(color, skyAtHorizon, haze * (0.30 + 0.70 * haze));

    // ---- phase 3: address + blend - sun glitter path ----
    // tight sparkle core + broad soft sheen, gated to the sun's azimuth column
    // so the glow stays a NARROW path down the middle with dark water either
    // side. Killed entirely inside cloud shadows, tinted orange, and the
    // sparkle variance rides the ripple chaos.
    //
    // Firefly fix: the old pow(NdH, 520) core amplified 1-ULP normal noise
    // into isolated white pixels on lit water. The core is now a tight GGX
    // lobe (alpha bounded by the ripple chaos, so its peak is FINITE and its
    // width a smooth function of the same texture the eye already reads as
    // chop) plus a tempered micro-sparkle at half the old exponent.
    vec3 H = normalize(L + V);
    float NdH = max(dot(N, H), 0.0);
    float pathGate = pow(sunAlign, 10.0) * 0.96 + 0.04;
    float sparkleGate = (0.55 + 0.90 * chaos);
    float rough = clamp(0.45 + 0.45 * chaos, 0.0, 0.95);
    float aGGX = max(0.14, (1.0 - rough) * 0.42);
    float a2 = aGGX * aGGX;
    float dGGX = a2 / (PI * pow(NdH * NdH * (a2 - 1.0) + 1.0, 2.0));
    // tight sparkle core: small-alpha GGX with a gain chosen so the PEAK
    // (NdH=1) stays <= ~6 — as bright as the old pow() spike could ever get,
    // but spread over a few pixels and a smooth function of chaos, so no
    // 1-ULP normal flip can mint an isolated white pixel.
    // FOOTPRINT-AWARE GGX (the actual root fix): alpha 0.05-0.10 is a lobe
    // NARROWER than one pixel — sampling it per-pixel is aliasing, which is
    // where the isolated dots came from (the tonemap merely recoloured
    // them). Widen alpha by the per-pixel NdH gradient so the lobe covers
    // multiple pixels near the glint and the sparkle renders SMOOTH, while
    // near-camera pixels (tiny gradient) keep a crisp core.
    float ndhGrad = fwidth(NdH);
    float aCore = mix(0.10, 0.05, chaos);             // chaos sharpens glints
    aCore = sqrt(aCore * aCore + ndhGrad * ndhGrad * 6.0);
    float aCore2 = aCore * aCore;
    float glint = aCore2 / (PI * pow(NdH * NdH * (aCore2 - 1.0) + 1.0, 2.0));
    // the tight mid lobe aliases the same way — its pow(,90) falloff is even
    // narrower than the GGX core, so it dies HARD under footprint pressure:
    // 60x-gradient suppression (was 25) plus a lower gain leaves crisp
    // micro-sparkle only where the gradient is near zero (close camera).
    float glintMid = pow(NdH, 90.0) * 0.25 / (1.0 + ndhGrad * 60.0);
    float glintWide = dGGX * 0.045;                   // physically-tailed sheen
    // HARD CAP on the additive sparkle — and a LOW one: a high cap makes
    // plateaus of near-knee colour that read as whitish PAINT BLOBS in the
    // sun path. Capped low and tinted warm orange, clumps stay granular
    // glints instead of fusing into white patches. Final pass: 0.85 -> 0.45
    // per the user — glints shimmer gently instead of blazing white.
    float spark = min(glint * 0.047 + glintMid + glintWide * pathGate, 0.45);
    color += vec3(1.0, 0.55, 0.22)
           * spark
           * (0.25 + max(L.y, 0.0) * 1.2) * pathGate * shadow * sparkleGate;

    // HDR safety: flush negatives and bound the HDR range before the knee —
    // no single term can ever blow up to a white pixel, on any driver.
    color = max(color, vec3(0.0));
    color = min(color, vec3(16.0));

    // HDR tone map + gamma — per-channel Reinhard knee (its slight blue
    // bias is what gives the dusk sea its indigo character), then a
    // LUMINANCE-PRESERVING WARM SHOULDER: the knee desaturates hot pixels
    // into a whitish band that reads as white paint blobs. Above the
    // shoulder we re-tint toward soft sunset peach SCALED TO THE SAME
    // LUMINANCE — brightness kept, whiteness gone, no yellow-paint cast
    // (the deep-orange target of the previous pass is what turned the
    // blobs yellow).
    color = max(color, vec3(0.0));
    color = min(color, vec3(16.0));
    color = color / (color + vec3(1.0));
    float lum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float warmStart = 0.45;
    if (lum > warmStart) {
        float f = clamp((lum - warmStart) / 0.20, 0.0, 1.0);
        f = f * f;
        vec3 warm = vec3(1.0, 0.62, 0.36);
        warm *= lum / max(dot(warm, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
        color = mix(color, warm, f);
    }
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0);
}
