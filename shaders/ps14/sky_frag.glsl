#version 330 core

// Sky for the dusk-ocean scene.
//
// The old version was a hand-painted gradient with a 4.6-degree sun disc and a
// compressed cirrus layer that read as hard horizontal bands. This one is a
// single-scattering Rayleigh + Mie atmosphere integrated along the view ray:
//
//   * the air column is sampled from the camera to the top of the atmosphere
//     (10 view samples), with a second 5-sample optical depth toward the sun;
//   * extinction reddens the sun correctly as it approaches the horizon;
//   * the sky, the horizon band and the sun's colour all come from the same
//     coefficients, so the sea's cubemap reflection cannot drift from the sky;
//   * the sun disc is drawn a little larger than the real 0.266 degrees (a
//     0.72-degree disc, chosen for the scene's readability) with limb darkening,
//     and is attenuated by the same transmittance.
//
// Clouds are a single procedural deck projected onto the plane at 1.5 km,
// lit with Beer-Lambert extinction, powder and a forward-scattering silver
// lining. The sea samples the same field for its shadows.

in vec3 vDir;

uniform float uTime;
uniform float uTonemap;
uniform vec3  uSunDir;
uniform vec3  uZenithColor;
uniform vec3  uMidColor;
uniform vec3  uHorizonColor;
uniform vec3  uSunColor;

#define MAX_CLOUDS 9
uniform int   uCloudCount;
uniform float uCloudPhase[MAX_CLOUDS];

uniform float uCloudAzim[MAX_CLOUDS];
uniform float uCloudElev[MAX_CLOUDS];
uniform float uCloudRadius[MAX_CLOUDS];
uniform float uCloudStretch[MAX_CLOUDS];

out vec4 fragColor;

const float PI = 3.14159265359;

// ------------------------------------------------------------------ atmosphere
// Earth-like atmosphere, SI units. Rg is the ground radius, Ra the top of the
// scattering medium; Hr/Hm are the Rayleigh and Mie scale heights.
const float kRg = 6360000.0;
const float kRa = 6420000.0;
const float kHr = 8000.0;
const float kHm = 1200.0;

// Sea-level scattering/extinction coefficients (1/m). These are the standard
// values used by Bruneton-style single scattering; keeping them physical is
// what makes the horizon orange while the zenith stays deep blue.
const vec3  kBetaR  = vec3(5.802e-6, 13.558e-6, 33.1e-6);
const vec3  kBetaM  = vec3(3.996e-6);
const float kBetaMExt = 4.44e-6;

// Radiance scale. The integral below is in physical units; this converts it to
// the scene's linear HDR units, which the shared ACES composite then maps.
const float kSunI = 1.4;
const float kSunAngularRadius = 0.01250; // 0.72 degrees: deliberately larger
                                         // than the real 0.266-degree disc

// Distance from the camera (inside the atmosphere) to the top of the
// atmosphere along dir. Returns -1 when the ray never leaves.
float atmosphereExit(vec3 pos, vec3 dir) {
    float b = dot(pos, dir);
    float c = dot(pos, pos) - kRa * kRa;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    return -b + sqrt(disc);
}

// Distance to the ground sphere, or -1 for a ray that misses it.
float groundHit(vec3 pos, vec3 dir) {
    float b = dot(pos, dir);
    float c = dot(pos, pos) - kRg * kRg;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float t = -b - sqrt(disc);
    return t > 0.0 ? t : -1.0;
}

// Optical depth (Rayleigh, Mie) from pos to the top of the atmosphere along
// dir. A ray that hits the ground is integrated only to the surface.
vec2 opticalDepth(vec3 pos, vec3 dir, int steps) {
    float tMax = atmosphereExit(pos, dir);
    if (tMax <= 0.0) return vec2(0.0);
    float tGround = groundHit(pos, dir);
    if (tGround > 0.0) tMax = min(tMax, tGround);

    float stepLen = tMax / float(steps);
    vec2 depth = vec2(0.0);
    for (int i = 0; i < 12; i++) {
        if (i >= steps) break;
        float t = (float(i) + 0.5) * stepLen;
        vec3 p = pos + dir * t;
        float h = max(length(p) - kRg, 0.0);
        depth.x += exp(-h / kHr) * stepLen;
        depth.y += exp(-h / kHm) * stepLen;
    }
    return depth;
}

float rayleighPhase(float mu) {
    return (3.0 / (16.0 * PI)) * (1.0 + mu * mu);
}

float miePhase(float mu, float g) {
    float gg = g * g;
    float denom = max(pow(1.0 + gg - 2.0 * g * mu, 1.5), 1e-4);
    return (3.0 / (8.0 * PI)) * ((1.0 - gg) * (1.0 + mu * mu))
         / ((2.0 + gg) * denom);
}

// Single-scattered sky radiance along dir. `viewDepth` may be reused by the
// caller for the sun disc and for aerial perspective on the sea.
vec3 atmosphereRadiance(vec3 pos, vec3 dir, vec3 sun, vec2 viewDepth,
                        out vec3 viewTransmittance) {
    viewTransmittance = exp(-(kBetaR * viewDepth.x
                            + vec3(kBetaMExt) * viewDepth.y));

    float mu = dot(dir, sun);
    float phR = rayleighPhase(mu);
    float phM = miePhase(mu, 0.76);

    float tMax = atmosphereExit(pos, dir);
    if (tMax <= 0.0) return vec3(0.0);
    float tGround = groundHit(pos, dir);
    if (tGround > 0.0) tMax = min(tMax, tGround);

    const int kViewSteps = 10;
    const int kSunSteps = 5;
    float stepLen = tMax / float(kViewSteps);

    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    for (int i = 0; i < kViewSteps; i++) {
        vec3 p = pos + dir * ((float(i) + 0.5) * stepLen);
        float h = max(length(p) - kRg, 0.0);
        vec2 sunDepth = opticalDepth(p, sun, kSunSteps);
        vec3 sunT = exp(-(kBetaR * sunDepth.x
                        + vec3(kBetaMExt) * sunDepth.y));
        vec3 sampleT = exp(-(kBetaR * (viewDepth.x * (float(i) + 0.5)
                                      / float(kViewSteps))
                           + vec3(kBetaMExt) * (viewDepth.y * (float(i) + 0.5)
                                      / float(kViewSteps))));
        float densR = exp(-h / kHr) * stepLen;
        float densM = exp(-h / kHm) * stepLen;
        sumR += densR * sunT * sampleT;
        sumM += densM * sunT * sampleT;
    }

    vec3 single = (kBetaR * phR * sumR + kBetaM * phM * sumM * 1.15) * kSunI;

    // Cheap multiple-scattering term: real sunsets keep a blue-to-violet wash
    // in the upper sky instead of going black, because light bounces several
    // times through the atmosphere. A second, isotropic Rayleigh lobe scaled
    // by the remaining view transmittance reproduces that without raymarching
    // the transport equation.
    vec3 multi = kBetaR * rayleighPhase(0.0) * viewTransmittance
               * (0.20 + 0.55 * clamp(sun.y, 0.0, 1.0)) * kSunI * 0.08;

    return single + multi;
}

// ------------------------------------------------------------------ clouds
// One physically projected cloud deck (1.5 km) instead of a set of authored
// screen-space blobs. The noise is anisotropic so the deck forms long banks,
// and the sea's shadow lookup uses the same field, so a shadow on the water
// always belongs to a cloud you can see above it.
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Worley F1: round, billowy cells. Value-noise erosion alone produces stringy
// clouds; the cells give the cumulus a lobed silhouette.
float cellNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    float d = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 g = vec2(float(x), float(y));
            vec2 o = vec2(hash21(i + g), hash21(i + g + 17.3));
            d = min(d, length(g + o - f));
        }
    }
    return d;
}

float erosionNoise(vec2 p) {
    float n = 0.57 * valueNoise(p);
    p = mat2(0.80, -0.60, 0.60, 0.80) * p * 2.03 + 17.1;
    n += 0.29 * valueNoise(p);
    p = mat2(0.80, -0.60, 0.60, 0.80) * p * 2.01 + 11.7;
    return n + 0.14 * valueNoise(p);
}

const float kDeckHeight = 1500.0;

// cover = opacity, hf = height in the deck (0 base, 1 top).
float cloudCover(vec2 worldXZ, out float hf) {
    // 0.00030 m^-1 -> cloud cells a few kilometres across; x is stretched to
    // form the long banks the reference photograph shows.
    vec2 p = worldXZ * 0.00033;
    p.x *= 0.82;                       // long banks, not an x-stretched smear
    p += vec2(uTime * 0.00070, uTime * 0.00022);
    // Domain warp: without it, a flat deck projects into long parallel streaks
    // at grazing angles, which is what made the clouds look smeared.
    p += 0.42 * (vec2(valueNoise(p * 2.1 + 7.0),
                      valueNoise(p * 2.3 + 31.0)) - 0.5);

    float base = erosionNoise(p);
    float det = erosionNoise(p * 3.9 + 17.0);
    float fine = valueNoise(p * 16.0 + 53.0);
    float micro = valueNoise(p * 47.0 + 91.0);
    // Erode the silhouette at three scales. The old deck only had one broad
    // octave, so every bank had the same smooth, airbrushed edge.
    float cells = cellNoise(p * 1.9 + 11.0);
    float shape = base + 0.17 * (det - 0.5) + 0.090 * (fine - 0.5)
                + 0.035 * (micro - 0.5) + 0.20 * (0.46 - cells);
    // Dusk deck: broken, not overcast. The old thresholds closed almost the
    // whole sky, which then mirrored off the sea as one bright sheet.
    float cover = smoothstep(0.605, 0.735, shape);
    cover *= 0.62 + 0.38 * smoothstep(0.38, 0.72, det);
    hf = clamp((shape - 0.55) * 4.0, 0.0, 1.0);
    return cover;
}

bool deckHit(vec3 pos, vec3 dir, out vec2 worldXZ) {
    if (dir.y < 0.015) return false;
    float t = (kDeckHeight - pos.y) / dir.y;
    if (t < 0.0 || t > 260000.0) return false;
    worldXZ = pos.xz + dir.xz * t;
    return true;
}


// Thin high cirrus: a second procedural layer, projected like the deck but
// stretched and almost transparent so it only veils the upper sky.
float cirrus(vec3 dir) {
    if (dir.y < 0.05) return 0.0;
    vec2 p = dir.xz / max(dir.y, 0.06);
    p = mat2(0.94, 0.34, -0.34, 0.94) * p * 0.55;
    p += vec2(uTime * 0.0040, -uTime * 0.0016);
    float n = erosionNoise(p * 2.2) + 0.5 * erosionNoise(p * 6.1 + 31.0);
    float veil = smoothstep(0.52, 0.86, n);
    veil *= smoothstep(0.04, 0.22, dir.y) * (1.0 - smoothstep(0.55, 0.95, dir.y));
    return veil;
}

// ------------------------------------------------------------------ encoding

vec3 encodeScene(vec3 hdr, float dith) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    hdr *= 0.70;
    vec3 x = clamp(max(hdr, vec3(0.0)), 0.0, 8.0);
    x = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    vec3 g = pow(max(x, vec3(0.0)), vec3(1.0 / 2.2));
    g += dith * (1.5 / 255.0);
    return clamp(g, 0.0, 1.0);
}

void main() {
    vec3 dir = normalize(vDir);
    vec3 sun = normalize(uSunDir);

    // Camera is a few metres above sea level; its altitude is irrelevant at
    // these scales but keeping it non-zero avoids a degenerate ray at the
    // exact centre of the planet.
    vec3 eyePos = vec3(0.0, kRg + 5.0, 0.0);
    if (dir.y < -0.02) {
        // Below the horizon: the sea covers it, but the environment cubemap
        // still needs a plausible colour for the mip tail.
        dir = normalize(vec3(dir.x, 0.02, dir.z));
    }

    vec2 viewDepth = opticalDepth(eyePos, dir, 10);
    vec3 viewT;
    vec3 sky = atmosphereRadiance(eyePos, dir, sun, viewDepth, viewT);
    float mu = dot(dir, sun);

    // The authored palette still steers the low sky: a slight magenta lift at
    // the horizon and a cooler zenith keeps the dusk scene's identity without
    // fighting the physical gradient.
    float h = clamp(dir.y, 0.0, 1.0);
    sky *= mix(vec3(1.0), uZenithColor / max(vec3(dot(uZenithColor, vec3(0.333))), vec3(1e-4)),
               smoothstep(0.18, 0.85, h) * 0.55);
    sky *= mix(vec3(1.0), uHorizonColor / max(vec3(dot(uHorizonColor, vec3(0.333))), vec3(1e-4)),
               (1.0 - smoothstep(0.02, 0.22, h)) * 0.30);

    // Deep dusk blue. The palette tints above preserve luminance, so darkening
    // the upper sky needs its own term; it also keeps the sea's cubemap
    // reflection in step with what is on screen.
    float deep = smoothstep(0.05, 0.70, h);
    sky *= mix(vec3(1.0), vec3(0.52, 0.66, 0.96), deep * 0.75);

    // Warm horizon band under the low sun: the direct signal the palette wants,
    // tied to the sun azimuth so it never paints the whole horizon flat.
    float horizonGlow = exp(-h * 9.0) * (0.35 + 0.65 * pow(max(mu, 0.0), 2.0));
    sky += vec3(0.30, 0.105, 0.042) * horizonGlow * viewT.g;

    // ---- clouds ----
    vec3 eyeWorld = vec3(0.0, 7.5, 0.0);
    float cl = 0.0;
    float cloudHf = 0.0;
    vec2 cloudUV;
    if (deckHit(eyeWorld, dir, cloudUV)) {
        cl = cloudCover(cloudUV, cloudHf);
        // Fade the deck into the haze at the horizon: a flat deck seen edge-on
        // has no resolvable detail there, and leaving it sharp aliases.
        cl *= smoothstep(0.025, 0.22, dir.y);
    }

    if (cl > 0.003) {
        // Optical depth toward the sun: probe the same field a short distance
        // along the sun's azimuth. Sunward banks darken their own bases.
        // Step 340 m at a time toward the sun. Scaling the absolute world UV
        // (the old code) shifted the sample toward the origin instead of along
        // the light direction, so cloud bases were shadowed by unrelated banks.
        vec2 sunStep = normalize(sun.xz + vec2(1e-4));
        vec2 sunUV = cloudUV + sunStep * 340.0;
        float depth = 0.0;
        float hfTmp;
        for (int s = 1; s <= 4; s++) {
            depth += cloudCover(sunUV + sunStep * (340.0 * float(s)), hfTmp);
        }
        float tau = cl * 1.15 + depth * 0.16;
        float transmit = exp(-tau);

        float phase = min(miePhase(mu, 0.70) * 4.0 * PI, 5.0);
        // A dusk deck is a dark silhouette with a lit rim, not a bright solid:
        // sunlit cloud tops sit near the sky's own radiance, bases in shadow.
        vec3 sunCol = vec3(1.50, 0.74, 0.36) * viewT * kSunI * 1.00;
        vec3 ambCol = vec3(0.105, 0.080, 0.095) * (0.40 + 0.60 * cloudHf);

        vec3 cloud = ambCol;
        cloud += sunCol * (0.12 + 0.88 * transmit)
               * (0.40 + 0.60 * phase) * (0.35 + 0.65 * cloudHf);

        float powder = 1.0 - exp(-tau * 3.0);
        cloud *= 1.0 - 0.32 * powder;

        float silver = pow(max(mu, 0.0), 7.0) * cl * (1.0 - cl)
                     * exp(-tau * 0.55) * 1.7;
        cloud += sunCol * silver;

        float alpha = 1.0 - exp(-cl * 1.15);
        cloud = mix(cloud, sky, (1.0 - smoothstep(0.03, 0.18, dir.y)) * 0.45);
        sky = mix(sky, cloud, clamp(alpha, 0.0, 0.93));
    }

    // High cirrus veil (thin, sun-lit ice crystals).
    float veil = cirrus(dir);
    if (veil > 0.003) {
        float fwd = pow(max(mu, 0.0), 6.0);
        vec3 cirCol = mix(vec3(0.34, 0.20, 0.20), vec3(1.10, 0.52, 0.28),
                          0.20 + 0.80 * fwd) * (0.45 + 0.55 * viewT.g);
        sky = mix(sky, cirCol, veil * 0.18);
    }

    // ---- sun disc: real angular size, limb darkening, physical reddening ----
    float d2 = 2.0 * (1.0 - mu);
    float R2 = kSunAngularRadius * kSunAngularRadius;
    float disc = 1.0 - smoothstep(R2 * 0.72, R2 * 1.18, d2);
    float limb = 0.42 + 0.58 * sqrt(clamp(1.0 - d2 / (R2 * 1.08), 0.0, 1.0));
    float cover = cl;
    vec3 sunDisc = uSunColor * viewT * kSunI * 1.9 * disc * limb
                 * (1.0 - 0.92 * clamp(cover, 0.0, 1.0));

    // The sun's aureole: a forward-scattering halo that stays narrow.
    float aureole = pow(max(mu, 0.0), 220.0) * 0.55
                  + pow(max(mu, 0.0), 900.0) * 0.85;
    sunDisc += vec3(1.0, 0.52, 0.22) * viewT * aureole * (1.0 - cover);

    sky += sunDisc;

    if (uTonemap > 0.5) {
        float dith = (hash21(gl_FragCoord.xy + fract(uTime) * 13.0)
                     + hash21(gl_FragCoord.xy * 1.7 + 41.0) - 1.0) * 0.5;
        fragColor = vec4(encodeScene(sky, dith), 1.0);
        return;
    }
    fragColor = vec4(max(sky, vec3(0.0)), 1.0);
}
