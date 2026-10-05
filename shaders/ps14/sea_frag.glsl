#version 330 core
// Fragment shader for the sea surface.
//
// Structure mirrors the phases of a shader_model 1.4 pixel shader (as used by
// the 3DMark2001 SE "Nature" ocean):
//   1) addressing    : ripple gradient texture -> perturbation vector
//   2) dependent read: perturbed coords -> environment reflection lookup
//   3) blend/address : sun glitter, blended with the deep-water colour
//
// Noise discipline: every term is analytic or texture-chaos MODULATED by an
// analytic gate. Free-floating glow terms show up as pale-teal speckle over the
// dark water — the "light blue noise" artifact — and must stay gated.

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

#define MAX_CLOUDS 9             // MUST match sky_frag.glsl and scene2.cxx
uniform int   uCloudCount;
uniform float uCloudAzim[MAX_CLOUDS];   // centre azimuth, radians
uniform float uCloudElev[MAX_CLOUDS];   // centre elevation, radians
uniform float uCloudRadius[MAX_CLOUDS]; // angular half-height, radians
uniform float uCloudStretch[MAX_CLOUDS];// azimuthal elongation (>1 = wider than tall)

out vec4 fragColor;

const float PI = 3.14159265359;

// BUILD-P21: ONE DISPLAY TRANSFORM, SHARED WITH THE SKY DOME.
// MUST stay byte-identical in shaders/ps14/sky_frag.glsl: the sea reflects the
// sky out of the HDR cubemap and runs the result through this function ITSELF,
// so any difference between the two curves shows up as a reflection that does
// not match the thing it reflects. Before P21 the dome used Reinhard plus a
// warm roll-off and the sea Reinhard without one, so the sun path came back
// visibly different from the sky above it.
vec3 encodeScene(vec3 hdr, float dith) {
    // ACES (Narkowicz) shoulder: rolls the sun disc and glitter path off
    // instead of letting them clip into flat paper.
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    // Exposure. ACES is not a like-for-like swap for Reinhard (at 0.2 it
    // returns 0.30 vs Reinhard's 0.17), so the curve change is paid for here,
    // once, rather than by walking every radiance in the scene.
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

    // ---- THE GRADE, applied in DISPLAY space on purpose. Gamma encoding
    // roughly HALVES apparent saturation (a 0.52 linear channel ratio arrives
    // near 0.28 on screen), so a lift applied before the gamma is mostly
    // cancelled by it. Pre-gamma, this frame measured a mean saturation of
    // 0.130 — a brown-grey mush rather than a sunset.
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
// (BUILD-P15). An octave whose wavelength is below the pixel that lands on it
// cannot be shaded, only aliased, so it fades out over exactly the band where
// it stops being resolvable. `foot` is the fragment's footprint in metres,
// `freq` the octave's frequency in cycles per metre.
float octaveRes(float foot, float freq) {
    return 1.0 - smoothstep(0.35, 1.10, foot * freq);
}

// NORMAL FIELD: smooth sines (finite differences of this drive the lighting).
// Scale/speed of each line MUST match the DISPLACEMENT field in sea_vert.glsl
// so crest banks light up where the geometry actually rises. The displacement
// shader additionally sharpens crests and adds two long swells; that asymmetry
// is deliberate (sharp banks, smooth lighting).
//
// BUILD-P21: each octave is gated on the fragment's own world footprint. The
// gate values are computed ONCE per fragment in main() and passed in, so all
// three finite-difference taps share them — and a fragment 200 m out gets the
// AVERAGE slope of the water inside its pixel, not the slope at one arbitrary
// point inside it. The second one is what made the far sea shimmer.
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
    // BUILD-P23: the slope SPREAD this pixel covers, from the curvature the
    // finite difference above already paid for. A point normal inside a pixel
    // that spans a slope range is not a smooth surface, and a mirror lobe
    // aimed with it hits the sun or misses it at random — the isolated-pixel
    // speckle the glitter path showed. This becomes roughness below.
    float curv = abs(hX + hZ - 2.0 * hC) / (e * e);
    // BUILD-P24: the slope SPREAD inside this pixel, not just at its centre.
    // The sea is shaded as if every fragment were a single mirror facet: the sun
    // term comes from one point normal. Past a few tens of metres a pixel is
    // half a metre to a couple of metres of water holding many facets that are
    // NOT parallel, so a point normal swings N.L from 0 to 0.79 between
    // neighbours -- every sun-keyed term (the warm slopes, the foam gate, the
    // subsurface lift) then paints whole sun-facing faces as flat tan patches
    // with hard edges. The one normal a pixel can honestly show is the MEAN of
    // the slopes it covers.
    float slopeSpread = curv * foot;
    // Relax toward flat water in proportion to how much slope the pixel mixes.
    // Mean max(N.L) over the field is 0.284 and flat water gives L.y = 0.287, so
    // this barely moves the AVERAGE of the sun term -- what it removes is its
    // CONTRAST, which is what made the blotches.
    // Only the sun-KEYED chain uses this. The mirror ray and the GGX lobe keep
    // the point normal on purpose: they already carry their own sub-pixel
    // treatment (roughness by Toksvig, the cubemap LOD), and relaxing them too
    // cost the sun column most of its mean luma for no extra smoothing.
    vec3 Nlit = normalize(mix(N, vec3(0.0, 1.0, 0.0),
                              clamp(slopeSpread * 1.6, 0.0, 1.0)));

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
    // Flattened-reflection trick: bias the ray upward so off-sun water mirrors
    // the dark upper sky instead of hugging the bright horizon band.
    R.y = abs(R.y) * 0.30 + 0.45;
    R = normalize(R);
    // Only rays near the SUN azimuth keep crisp reflections; off-path rays
    // blend toward the dark upper sky so the glow column stays narrow.
    vec3 L = normalize(uSunDir);
    vec2 sunXZ = normalize(L.xz);
    vec2 dirXZ = normalize(vWorld.xz - uEyePos.xz + vec2(1e-4));
    float sunAlign = max(dot(dirXZ, sunXZ), 0.0);
    // BUILD-P21: hoisted — needed as the foam's ambient light, the far-sea
    // mirror target and the haze target, and it was sampled after the foam.
    vec2 dxdz = vWorld.xz - uEyePos.xz;
    vec3 skyAtHorizon = texture(uSkyEnvTex,
        normalize(vec3(dxdz.x, 0.012, dxdz.y))).rgb;
    vec3 Rdark = normalize(vec3(R.x, abs(R.y) * 1.8 + 0.62, R.z)); // steep: upper sky
    R = normalize(mix(Rdark, R, pow(sunAlign, 6.0)));

    // Roughness-matched LOD: the sun disc is a handful of cubemap texels, and
    // sampling those at LOD 0 mirrors as SQUARE patches on the water. Blurring
    // with range also smears the sun into a soft vertical glow.
    float reflDist = dist;
    vec3 reflColor = textureLod(uSkyEnvTex, R, clamp(1.5 + reflDist * 0.0012, 1.0, 5.0)).rgb;
    float offSun = 1.0 - smoothstep(0.08, 0.45, sunAlign);
    reflColor *= mix(1.0, 0.46, offSun);

    // ---- fresnel: sea is a mirror at grazing angles, glass straight down ----
    // BUILD-P24: NdV was the only dot product here not clamped at the TOP. Two
    // unit vectors can return 1.0000001, which makes the base NEGATIVE, and
    // pow() of a negative base is undefined in GLSL -- it returns NaN, and
    // encodeScene maps NaN to 0, so the pixel is pure black. Clamped like NdH
    // and NoV, and the pow written as the same squared-cubed chain the
    // Fresnel-Schlick term further down already uses.
    float NdV = clamp(dot(N, V), 0.0, 1.0);
    float f1 = 1.0 - NdV;
    float f2 = f1 * f1;
    float fresnel = 0.022 + 0.978 * (f2 * f2 * f1);
    // At dusk the whole surface reads as a dark mirror of the sky: lift the
    // base reflectance above the physical 2%.
    fresnel = clamp(fresnel * 1.25 + 0.045, 0.0, 0.92);
    // BUILD-P23: that +0.045 lift overrides the physical 2% EVERYWHERE,
    // including for near-flat facets looking at the blue upper sky. On a
    // faceted surface it painted a bright blue dot wherever one facet happened
    // to sit flat, so it now fades out off-path.
    fresnel = clamp(fresnel - 0.030 * offSun, 0.0, 0.92);

    // ---- water body: near-black purple deep, warmed by the sky band ----
    vec3 body = uWaterColor * 0.55 + uHorizonColor * 0.03;

    // Directional sun lighting on the wave slopes: faces tilted toward the low
    // sun glow warm, backslopes fall to near-black. The azimuth gate keeps the
    // warm slope light inside the sun path; off-path stays deep blue/purple.
    float sunDiffuse = max(dot(Nlit, L), 0.0);
    float warmGate = pow(sunAlign, 3.0);

    // cloud shadows: the projected puffs gate ALL direct sun terms
    float shadow = cloudShadow(vWorld, sd);

    // Much darker water where the sun's light doesn't reach: off-path base
    // drops to near-black indigo, and cloud shadows multiply direct light.
    body *= 0.44 + 0.40 * sunDiffuse * warmGate * shadow + 0.12 * sunDiffuse * (0.35 + 0.65 * shadow);
    body *= mix(0.40, 1.0, warmGate * shadow + (1.0 - warmGate) * 0.25 * shadow); // dark off-path + shadowed body
    body *= mix(0.52, 1.0, 1.0 - offSun);
    body += vec3(1.05, 0.42, 0.20) * pow(sunDiffuse, 3.0) * warmGate * shadow * 0.42; // warm slopes in the path

    // ---- slope-gated crest foam ----
    // Foam only where the shading says waves BREAK: a height band AND the
    // slope facing the sun AND some ripple chaos. The noise tile alone would
    // foam uniformly everywhere. Cloud shadows gate it too.
    float crest = smoothstep(0.55, 1.25, hC) * mix(0.25, 1.0, shadow);
    float slopeFacing = sunDiffuse * warmGate + 0.15;
    float chaos = texture(uRippleTex, vUV * 190.0 + vec2(uTime * 0.011, -uTime * 0.007)).g;
    // BUILD-P23: a 190x tile, so past the near field one pixel covers more than
    // one texel and the fetch minifies into noise — and it was multiplying the
    // glitter lobe by up to 2.6x. Aliased noise on a razor lobe IS the
    // pixellated glitter path, so the tile fades out on its own footprint.
    vec2 cuv = vUV * 190.0;
    float cfoot = fwidth(cuv.x) + fwidth(cuv.y);
    float chaosRes = 1.0 - smoothstep(0.35, 1.10, cfoot);
    chaos = mix(0.5, chaos, chaosRes);
    float foam = texture(uFoamTex, vUV * 23.0 + vec2(uTime * 0.010, 0.0)).r;
    foam *= texture(uFoamTex, vUV * 41.0 - vec2(0.0, uTime * 0.013)).g;
    // BUILD-P21: a breaking crest is sub-pixel past a few hundred metres, so it
    // runs on the same footprint gate as the wave octaves instead of painting a
    // full-contrast speckle pattern to the horizon. Real whitecaps merge into a
    // pale band long before they stop being individually visible.
    foam *= octaveRes(foot, 0.62);
    foam *= crest * slopeFacing * (0.35 + 0.65 * chaos);
    // BUILD-P21: foam is a rough near-white surface a few centimetres thick, so
    // what it returns to the eye is mostly the sky above it plus whatever sun
    // reaches it. A hard-coded tint is what made crests read as confetti stuck
    // onto the water rather than water thrown into the air.
    vec3 foamLit = skyAtHorizon * 0.62
                 + vec3(1.0, 0.50, 0.24) * (sunDiffuse * shadow) * 0.40;
    body += foamLit * foam * 0.62;

    // subsurface glow against the light: THIN CRESTS transmit a dim jade-green
    // where sunlight actually passes through the water. Gated to the sun's
    // azimuth AND to un-shadowed sun — ungated it speckled pale-teal noise
    // across the dark off-path sea (the light-blue artifact).
    // BUILD-P23: the crest gate was a narrow band with hard shoulders, so this
    // landed as small hard-edged patches rather than a broad translucent lift,
    // and on blue-purple water they tipped into the light blue spots. Widened,
    // halved, shifted off cyan.
    body += vec3(0.07, 0.15, 0.09) * sunAlign * warmGate * shadow
            * smoothstep(0.35, 1.45, hC) * 0.30;

    vec3 color = mix(body, reflColor, fresnel);

    // ---- horizon sheen: the far sea is a MIRROR of the sky band above it
    // (grazing fresnel -> nearly pure reflection up there), so the water edge
    // converges into the sky geometrically, with no haze knob.
    float horizonMix = smoothstep(1500.0, 1900.0, dist);
    color = mix(color, skyAtHorizon, horizonMix * 0.85);

    // ---- aerial haze: near water stays dark, far sea melts into the horizon.
    // The target is the ACTUAL sky colour at this azimuth (env cubemap just
    // above the horizon line), so the far edge converges into the sky band.
    float haze = 1.0 - exp(-dist * 0.00075);
    color = mix(color, skyAtHorizon, haze * (0.30 + 0.70 * haze));

    // ---- phase 3: address + blend - sun glitter path, as a REAL MICROFACET
    // DISTRIBUTION. Before P21 this was three hand-fitted pow(N·H, n) lobes with
    // fixed exponents: a "pinpoint sparkle" 520 wide is sub-pixel past a few
    // hundred metres, so the far half was per-pixel noise the eye reads as
    // shimmer while the near half was blown-out white beads. One GGX + Smith +
    // Fresnel term does the work of all three.
    vec3 H = normalize(L + V);
    float NdH = clamp(dot(N, H), 0.0, 1.0);
    float NoV = clamp(dot(N, V), 1e-3, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float VoH = clamp(dot(V, H), 0.0, 1.0);
    float rough = clamp(0.085 + foot * 0.016, 0.085, 0.30);
    // BUILD-P23: normal-variance (Toksvig/LEAN) roughening. The distance term
    // alone says nothing about how ROUGH the water inside that pixel is; the
    // slope spread from the curvature above is the part of the roughness the
    // analytic lobe never knew about. Many facets inside one pixel then
    // average into a smooth glare, as real water does. P24 hoisted slopeSpread up
    // to main()'s normal so the same number could relax the lighting normal too.
    rough = clamp(sqrt(rough * rough + 0.65 * slopeSpread * slopeSpread),
                  0.085, 0.46);
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
    // BUILD-P23: the old ceiling was 1/0.235 = 4.26, which drove the whole
    // column onto a ~0.84 plateau — bright but FLAT. 0.46 puts it at 2.17.
    float sp = spR / (1.0 + spR * 0.46);
    // Gated to the sun's azimuth column so the glow stays a NARROW path with
    // dark water either side, killed inside cloud shadows, and its sparkle
    // variance rides the ripple chaos.
    // Gated to the sun's azimuth column so the glow stays a NARROW path with
    // dark water either side, killed inside cloud shadows, and its sparkle
    // variance rides the ripple chaos.
    // BUILD-P23: widening the lobe spreads the sun's energy over more water,
    // and the old 0.04 floor put 4% of it everywhere — measured as a 16% lift
    // in mean water luma, i.e. the dusk sea going pale. Glitter belongs in the
    // path, so the floor is nearly zero and the gain comes down to match.
    float pathGate = pow(sunAlign, 10.0) * 0.985 + 0.015;
    float sparkleGate = (0.55 + 0.90 * chaos);
    color += vec3(1.0, 0.56, 0.24) * sp * 0.36
             * (0.25 + max(L.y, 0.0) * 1.2) * pathGate * shadow * sparkleGate;

    // ---- SHARED DISPLAY TRANSFORM (identical to the sky dome's) ----------
    float dith = (hash21(gl_FragCoord.xy + fract(uTime) * 13.0)
                 + hash21(gl_FragCoord.xy * 1.7 + 41.0) - 1.0) * 0.5;
    fragColor = vec4(encodeScene(color, dith), 1.0);
}
