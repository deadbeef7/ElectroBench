#version 330 core

// Sea surface for the dusk-ocean scene.
//
// Shading model (all of it physically motivated, no hand-tuned pow() stacks):
//   * the wave normal is the exact analytic gradient of the same band-limited
//     field the vertex stage displaces, plus two sub-grid detail octaves and a
//     scrolling ripple gradient;
//   * reflection is the sky cubemap sampled through the water's own roughness,
//     so the surface returns the same air column the sky pass drew;
//   * the sun is a GGX microfacet lobe on the resolved normal plus a broad
//     statistical lobe for the slope variance of the sub-pixel surface - that
//     pair is what forms a continuous glitter path (one sharp lobe alone gives
//     isolated sparks, one broad lobe alone washes the whole sea white);
//   * the water body is deep absorption plus a crest transmission term;
//   * distant water fades into the sky's own horizon radiance, so the finite
//     patch melts into the sky instead of ending on a hard line.

in vec3  vWorld;
in vec2  vUV;
in float vDist;

uniform sampler2D   uRippleTex;
uniform samplerCube uSkyEnvTex;
uniform sampler2D   uFoamTex;
uniform float uTime;
uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform vec3  uWaterColor;
uniform float uHdrOut;

#define MAX_CLOUDS 9
uniform int   uCloudCount;
uniform float uCloudAzim[MAX_CLOUDS];
uniform float uCloudElev[MAX_CLOUDS];
uniform float uCloudRadius[MAX_CLOUDS];
uniform float uCloudStretch[MAX_CLOUDS];

out vec4 fragColor;

const float PI = 3.14159265359;
const float kWaterF0 = 0.020;       // ((1.33 - 1)/(1.33 + 1))^2
const float kWavePixelAngle = 0.0018;

// ------------------------------------------------------------------ wave field
// Same table as sea_vert.glsl. `dist` fades each component once its wavelength
// falls under a few pixels: that is the correct band-limit for the pixel
// footprint, and it is why the horizon stops shimmering.
float bandFade(float k, float dist) {
    float lambda = 6.2831853 / k;
    return smoothstep(1.5, 4.0, lambda / max(dist * kWavePixelAngle, 1e-3));
}

// One component: h = A (c + 0.35 c|c|) for sharp=true, plain sine otherwise.
// dh/dp = A (1 + 0.7|c|) cos(phase) d k for the sharp form.
void addWave(vec2 p, float t, vec2 d, float k, float a, float s, float fade,
             bool sharp, inout float h, inout vec2 g) {
    if (fade <= 0.0) return;
    float phase = dot(p, d) * k + t * s;
    float c = sin(phase);
    if (sharp) {
        h += a * fade * (c + 0.16 * c * abs(c));
        g += d * (k * a * fade * (1.0 + 0.32 * abs(c)) * cos(phase));
    } else {
        h += a * fade * c;
        g += d * (k * a * fade * cos(phase));
    }
}

vec2 waveGrad(vec2 p, float t, float dist, out float height) {
    vec2 g = vec2(0.0);
    float h = 0.0;
    addWave(p, t, vec2(0.52, 0.30),  0.045, 2.10, 0.42, 1.0, false, h, g);
    addWave(p, t, vec2(0.10, -0.49), 0.075, 1.30, 0.55, 1.0, false, h, g);
    addWave(p, t, vec2(0.98, 0.20),  0.170, 1.45, 1.30, bandFade(0.170, dist), true, h, g);
    addWave(p, t, vec2(-0.64, 0.77), 0.240, 0.85, 1.60, bandFade(0.240, dist), true, h, g);
    addWave(p, t, vec2(0.36, -0.93), 0.380, 0.55, 2.10, bandFade(0.380, dist), false, h, g);
    addWave(p, t, vec2(-0.91, -0.42), 0.540, 0.34, 2.70, bandFade(0.540, dist), false, h, g);
    addWave(p, t, vec2(0.71, -0.70), 0.620, 0.28, 2.95, bandFade(0.620, dist), false, h, g);
    addWave(p, t, vec2(0.59, 0.81),  0.860, 0.20, 3.40, bandFade(0.860, dist), false, h, g);
    addWave(p, t, vec2(-0.20, 0.98), 1.450, 0.11, 4.40, bandFade(1.450, dist), false, h, g);
    height = h;
    return g;
}

// Sub-grid detail: only where the pixel footprint can still resolve it, so it
// never aliases into crawling sparkle.
void detailGrad(vec2 p, float t, float dist, inout vec2 grad) {
    float fade = exp(-dist * 0.0035);
    if (fade < 0.02) return;
    vec2 d1 = vec2(0.86, 0.51), d2 = vec2(-0.44, 0.90);
    float ph1 = dot(p, d1) * 2.60 + t * 5.10;
    float ph2 = dot(p, d2) * 5.30 + t * 6.80;
    grad += d1 * (2.60 * 0.050 * cos(ph1)) * fade;
    grad += d2 * (5.30 * 0.018 * cos(ph2)) * fade;
}

// --------------------------------------------------------------------- clouds
// Same 1.5 km deck as sky_frag.glsl: the shadow on the water is sampled from
// the same noise field, so it stays locked to the visible bank.
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

float cloudCell(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    float d = 1.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 g = vec2(float(x), float(y));
            vec2 o = vec2(cloudHash21(i + g), cloudHash21(i + g + 17.3));
            d = min(d, length(g + o - f));
        }
    }
    return d;
}

float cloudErosion(vec2 p) {
    float n = 0.57 * cloudValueNoise(p);
    p = mat2(0.80, -0.60, 0.60, 0.80) * p * 2.03 + 17.1;
    n += 0.29 * cloudValueNoise(p);
    p = mat2(0.80, -0.60, 0.60, 0.80) * p * 2.01 + 11.7;
    return n + 0.14 * cloudValueNoise(p);
}

const float kDeckHeight = 1500.0;

float cloudCover(vec2 worldXZ, out float hf) {
    vec2 p = worldXZ * 0.00033;
    p.x *= 0.82;
    p += vec2(uTime * 0.00070, uTime * 0.00022);
    p += 0.42 * (vec2(cloudValueNoise(p * 2.1 + 7.0),
                      cloudValueNoise(p * 2.3 + 31.0)) - 0.5);
    float base = cloudErosion(p);
    float det = cloudErosion(p * 3.9 + 17.0);
    float fine = cloudValueNoise(p * 16.0 + 53.0);
    float micro = cloudValueNoise(p * 47.0 + 91.0);
    float cells = cloudCell(p * 1.9 + 11.0);
    float shape = base + 0.17 * (det - 0.5) + 0.090 * (fine - 0.5)
                + 0.035 * (micro - 0.5) + 0.20 * (0.46 - cells);
    float cover = smoothstep(0.565, 0.690, shape);
    cover *= 0.66 + 0.34 * smoothstep(0.38, 0.72, det);
    hf = clamp((shape - 0.55) * 4.0, 0.0, 1.0);
    return cover;
}

float cloudShadow(vec3 world, vec3 sd) {
    if (sd.y < 0.015) return 1.0;
    float t = (kDeckHeight - world.y) / sd.y;
    if (t < 0.0 || t > 260000.0) return 1.0;
    float hf;
    float c = cloudCover(world.xz + sd.xz * t, hf);
    return clamp(exp(-c * 2.8), 0.10, 1.0);
}

// -------------------------------------------------------------- microfacet
float GgxD(float NoH, float a2) {
    float denom = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + 1e-7);
}

float SmithG(float NoV, float NoL, float a2) {
    float k = a2 * 0.5;
    return (NoL / (NoL * (1.0 - k) + k)) * (NoV / (NoV * (1.0 - k) + k));
}

void main() {
    vec3 L = normalize(uSunDir);
    vec3 V = normalize(uEyePos - vWorld);
    float dist = vDist;

    // ---- normal: exact gradient of the drawn field + band-limited detail ----
    float hC;
    vec2 grad = waveGrad(vWorld.xz, uTime, dist, hC);
    detailGrad(vWorld.xz, uTime, dist, grad);

    // ---- ripple normal: two scrolling samples, faded before they alias ----
    float rippleFade = exp(-dist * 0.0011);
    vec2 uvA = vUV * 420.0 + vec2(uTime * 0.0040, uTime * 0.0031);
    vec2 uvB = vUV * 1100.0 - vec2(uTime * 0.0026, uTime * 0.0042);
    vec2 ripA = texture(uRippleTex, uvA).rg * 2.0 - 1.0;
    vec2 ripB = texture(uRippleTex, uvB).rg * 2.0 - 1.0;
    vec2 pert = (ripA + ripB) * 0.5 * rippleFade;
    grad += pert * 0.140;

    vec3 N = normalize(vec3(-grad.x, 1.0, -grad.y));

    float shadow = cloudShadow(vWorld, L);
    float NoL = max(dot(N, L), 0.0);
    float NoV = clamp(dot(N, V), 1e-3, 1.0);

    // ---- water body: absorption + crest transmission -----------------------
    vec3 body = uWaterColor * 1.35 * (0.24 + 0.76 * NoL)
              + vec3(0.006, 0.013, 0.026);
    float crestTop = smoothstep(1.10, 3.60, hC);
    // Sunlight scattered through a thin crest toward the eye; strongest when
    // the sun is behind the wave, which is exactly when real water glows green.
    float through = pow(clamp(dot(-V, L), 0.0, 1.0), 3.0);
    body += vec3(0.035, 0.070, 0.050) * crestTop * (0.20 + 0.80 * through)
          * (0.35 + 0.65 * shadow) * 0.60;

    // ---- sky reflection: the cubemap, through the water's roughness --------
    vec3 R = reflect(-V, N);
    R.y = max(R.y, 0.002);
    // Real water never mirrors the sky like glass: centimetre-scale ripples,
    // which no mesh resolves, smear the reflection over several degrees. That
    // is a roughness, so it is applied as a cubemap mip level here. Without it
    // the sea reads as polished chrome, not water.
    float rough = clamp(0.22 + dist * 0.00055, 0.22, 0.90);
    float lod = 2.0 + rough * 8.0;
    vec3 skySharp = textureLod(uSkyEnvTex, R, lod).rgb;
    vec3 skyWide  = textureLod(uSkyEnvTex, R, lod + 2.0).rgb;
    vec3 reflColor = mix(skySharp, skyWide, 0.55);
    // Facets tilted away from the eye are shadowed by the waves in front of
    // them, so the reflected sky loses a good part of its energy on open water.
    float reflVis = 0.55 / (1.0 + 2.6 * dot(N.xz, N.xz));
    reflColor *= reflVis;
    reflColor = min(reflColor, vec3(0.22));

    float fresnel = kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - NoV, 5.0);
    fresnel = clamp(fresnel, 0.0, 0.80);

    // ---- sun: sharp lobe on the resolved normal + statistical broad lobe ---
    vec3 H = normalize(L + V);
    float sunE = 2.0;
    vec3 sunTint = vec3(1.0, 0.52, 0.21);

    vec3 Ns = normalize(N + vec3(pert.x, 0.0, pert.y) * 0.22);
    float NoHs = max(dot(Ns, H), 0.0);
    float NoLs = max(dot(Ns, L), 1e-3);
    float alphaS = clamp(0.045 + dist * 0.00020, 0.045, 0.30);
    float aS = alphaS * alphaS;
    float Fs = kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - max(dot(V, H), 0.0), 5.0);
    float sharp = GgxD(NoHs, aS) * Fs * SmithG(NoV, NoLs, aS)
                / (4.0 * NoV * NoLs + 1e-4) * NoLs;

    // Unresolved slope variance: a wide lobe on the smooth normal gives the
    // path its body; the ripples break it into the shimmer on top.
    float aB = 0.012;
    float NoHb = max(dot(N, H), 0.0);
    float Fb = kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - NoV, 5.0);
    float broad = GgxD(NoHb, aB) * Fb * SmithG(NoV, NoL, aB)
                / (4.0 * NoV * NoL + 1e-4) * NoL;

    // The sharp lobe is the resolved facet; the sparkle texture is the slope
    // population of everything below the pixel, so it scatters the highlight
    // into points along the path instead of a solid ribbon.
    float sp1 = texture(uRippleTex, vUV * 1200.0 + vec2(uTime * 0.017, -uTime * 0.013)).r;
    float sp2 = texture(uRippleTex, vUV * 2100.0 - vec2(uTime * 0.011, uTime * 0.019)).g;
    float sparkle = 0.30 + 0.95 * smoothstep(0.28, 0.80, sp1 * 0.6 + sp2 * 0.6);
    // Only a small fraction of the sea's solid angle can return the sun to the
    // eye, and what does return is a spike the exposure clips, not a sheet.
    // Weights and the cap below were set against the reference: the path must
    // sit on dark water, not replace it.
    // Soft warm glow along the path: the unresolved forward-scattered body of
    // the reflection, which keeps the column readable between sparkle points.
    float sunAlign = max(dot(R, L), 0.0);
    float pathGlow = pow(sunAlign, 26.0) * 0.10;
    vec3 glintCol = sunTint * sunE * (sharp * 0.40 * sparkle + broad * 0.05
                                    + pathGlow) * shadow;
    glintCol = min(glintCol, vec3(1.6));

    // ---- foam: only where crests really break ------------------------------
    // A whitecap is the tip of a sharp crest, not a wash over a whole face:
    // require a high crest AND a steep face, then let the noise thin it to
    // patches, which is what keeps this from painting the swell.
    float crest = smoothstep(3.95, 4.75, hC);
    float steep = smoothstep(0.85, 1.45, length(grad));
    float chaos = texture(uRippleTex, vUV * 640.0 + vec2(uTime * 0.011, -uTime * 0.007)).g;
    float foamTex = texture(uFoamTex, vUV * 31.0 + vec2(uTime * 0.010, 0.0)).r;
    foamTex *= texture(uFoamTex, vUV * 53.0 - vec2(0.0, uTime * 0.013)).g;
    float foam = crest * steep * smoothstep(0.20, 0.85, NoL)
               * (0.15 + 0.85 * chaos) * foamTex;
    foam *= mix(0.45, 1.0, shadow) * exp(-dist * 0.0013);
    vec3 foamColor = vec3(0.55, 0.52, 0.52) * (0.20 + 0.80 * NoL)
                   + sunTint * 0.06 * NoL;
    body = mix(body, foamColor, clamp(foam * 0.42, 0.0, 0.30));

    // ---- combine -----------------------------------------------------------
    vec3 color = mix(body, reflColor, fresnel) + glintCol;

    // ---- aerial perspective: melt into the sky's own horizon radiance ------
    vec3 horizonDir = normalize(vec3(vWorld.x - uEyePos.x, 0.020,
                                     vWorld.z - uEyePos.z));
    vec3 skyAtHorizon = textureLod(uSkyEnvTex, horizonDir, 3.0).rgb;
    float haze = 1.0 - exp(-dist * 0.00115);
    color = mix(color, skyAtHorizon, haze * (0.22 + 0.78 * haze));

    color = min(color, vec3(4.0));
    if (uHdrOut > 0.5) {
        fragColor = vec4(max(color, vec3(0.0)), 1.0);
        return;
    }
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0);
}
