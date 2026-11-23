// Hand-ported from ElectroBench v0.6 shaders/pole/sky_frag.glsl for vitaGL /
// SceShaccCg (GLSL ES 1.00, shared by the Vita and Android GLES builds).
//
// Physically integrated Rayleigh + Mie atmosphere (single scattering with a
// multiple-scattering fill), the real sun with limb darkening, and a
// perspective-correct cumulus layer lit by the same transmittance. ES 1.00
// rewrites only: in -> varying, out fragColor -> gl_FragColor, #version 100 +
// precision. The integration loops were already constant-bounded.
#version 100
precision highp float;

varying highp vec3 vDir;

uniform highp vec3  uEyePos;
uniform highp vec3  uSunDir;
uniform highp float uTime;
uniform float uHdrOut;

const float PI = 3.14159265359;

// ---------------------------------------------------------------- atmosphere
const float kRg = 6360000.0;
const float kRa = 6420000.0;
const float kHr = 8000.0;
const float kHm = 1200.0;
const vec3  kBetaR = vec3(5.802e-6, 13.558e-6, 33.1e-6);
const vec3  kBetaM = vec3(3.996e-6);
const float kBetaMExt = 4.44e-6;
const float kSunI = 1.80;
const float kSunAngularRadius = 0.01400; // ~0.8 deg, visibly larger dusk sun

float atmosphereExit(vec3 pos, vec3 dir) {
    float b = dot(pos, dir);
    float c = dot(pos, pos) - kRa * kRa;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    return -b + sqrt(disc);
}

float groundHit(vec3 pos, vec3 dir) {
    float b = dot(pos, dir);
    float c = dot(pos, pos) - kRg * kRg;
    float disc = b * b - c;
    if (disc < 0.0) return -1.0;
    float t = -b - sqrt(disc);
    return t > 0.0 ? t : -1.0;
}

vec2 opticalDepth(vec3 pos, vec3 dir, int steps) {
    float tMax = atmosphereExit(pos, dir);
    if (tMax <= 0.0) return vec2(0.0);
    float tGround = groundHit(pos, dir);
    if (tGround > 0.0) tMax = min(tMax, tGround);

    float stepLen = tMax / float(steps);
    vec2 depth = vec2(0.0);
    for (int i = 0; i < 8; i++) {
        if (i >= steps) break;
        vec3 p = pos + dir * ((float(i) + 0.5) * stepLen);
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

vec3 skyRadiance(vec3 pos, vec3 dir, vec3 sun, out vec3 viewT) {
    vec2 viewDepth = opticalDepth(pos, dir, 8);
    viewT = exp(-(kBetaR * viewDepth.x + vec3(kBetaMExt) * viewDepth.y));
    float mu = dot(dir, sun);
    float phR = rayleighPhase(mu);
    float phM = miePhase(mu, 0.76);

    float tMax = atmosphereExit(pos, dir);
    if (tMax <= 0.0) return vec3(0.0);
    float tGround = groundHit(pos, dir);
    if (tGround > 0.0) tMax = min(tMax, tGround);

    const int kSteps = 8;
    float stepLen = tMax / float(kSteps);
    vec3 sumR = vec3(0.0);
    vec3 sumM = vec3(0.0);
    for (int i = 0; i < kSteps; i++) {
        float f = (float(i) + 0.5) / float(kSteps);
        vec3 p = pos + dir * (f * tMax);
        float h = max(length(p) - kRg, 0.0);
        vec2 sd = opticalDepth(p, sun, 4);
        vec3 st = exp(-(kBetaR * sd.x + vec3(kBetaMExt) * sd.y));
        vec3 vt = exp(-(kBetaR * (viewDepth.x * f)
                      + vec3(kBetaMExt) * (viewDepth.y * f)));
        sumR += exp(-h / kHr) * st * vt * stepLen;
        sumM += exp(-h / kHm) * st * vt * stepLen;
    }

    vec3 single = (kBetaR * phR * sumR + kBetaM * phM * sumM * 1.45) * kSunI;
    vec3 multi = kBetaR * rayleighPhase(0.0) * viewT
               * (0.16 + 0.58 * clamp(sun.y, 0.0, 1.0)) * kSunI * 0.10;
    return single + multi;
}

// ------------------------------------------------------------------- clouds
float vhash(vec2 p) {
    vec3 q = fract(vec3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = vhash(i);
    float b = vhash(i + vec2(1.0, 0.0));
    float c = vhash(i + vec2(0.0, 1.0));
    float d = vhash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm2(vec2 p) {
    return vnoise(p) * 0.66 + vnoise(p * 2.17 + 19.3) * 0.34;
}
float fbm3(vec2 p) {
    return vnoise(p) * 0.54 + vnoise(p * 2.13 + 11.7) * 0.29
         + vnoise(p * 4.31 + 41.2) * 0.17;
}

// Intersection of a ray with the flat cumulus deck at kCloudHeight.
const float kCloudHeight = 1350.0;
bool cloudPlane(vec3 pos, vec3 dir, out vec2 uv) {
    if (dir.y < 0.012) return false;
    float t = (kCloudHeight - pos.y) / dir.y;
    if (t < 0.0 || t > 220000.0) return false;
    uv = (pos.xz + dir.xz * t) * 0.00055;
    return true;
}

float cumulusDensity(vec2 p, out float heightFrac) {
    vec2 drift = vec2(uTime * 0.0018, -uTime * 0.0006);
    float base = fbm3(p * 0.72 + drift);
    float det = fbm2(p * 2.9 - drift * 2.2 + 31.0);
    float shape = base - 0.30 * (det - 0.5);
    float cover = smoothstep(0.600, 0.780, shape);
    cover *= 0.78 + 0.22 * smoothstep(0.38, 0.68, det);
    heightFrac = clamp((shape - 0.55) * 3.0, 0.0, 1.0);
    return cover;
}

float cirrus(vec3 dir) {
    if (dir.y < 0.03) return 0.0;
    vec2 p = dir.xz / max(dir.y, 0.04);
    p = mat2(0.92, 0.39, -0.39, 0.92) * p * 0.36;
    p += vec2(uTime * 0.0030, -uTime * 0.0011);
    float n = fbm3(p * 1.8 + 7.0);
    float veil = smoothstep(0.54, 0.84, n);
    veil *= smoothstep(0.05, 0.26, dir.y) * (1.0 - smoothstep(0.62, 0.98, dir.y));
    return veil;
}

vec3 encodeSky(vec3 hdr) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    vec3 x = clamp(hdr, 0.0, 8.0);
    x = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    return pow(x, vec3(1.0 / 2.2));
}

void main() {
    vec3 dir = normalize(vDir - uEyePos);
    float h = dir.y;
    vec3 sun = normalize(uSunDir);

    // Camera altitude, not the dome centre: the sky is evaluated at the eye.
    vec3 eyePos = vec3(0.0, kRg + max(uEyePos.y, 1.0), 0.0);
    if (h < -0.015) dir = normalize(vec3(dir.x, -0.015, dir.z));

    vec3 viewT;
    vec3 sky = skyRadiance(eyePos, dir, sun, viewT);

    // Amber dusk grade. Grading the whole sky here also grades the cubemap,
    // so ambient light and reflections stay coherent.
    float duskW = 0.92;
    float skyLum = dot(sky, vec3(0.2126, 0.7152, 0.0722));
    vec3 amber = vec3(2.05, 0.82, 0.26) * skyLum + vec3(0.075, 0.022, 0.003);
    sky = mix(sky, amber, duskW);

    // Below the horizon the ground geometry covers the frame; give the dome a
    // dark warm ground colour for the cubemap/reflection fallback.
    if (h < -0.015) {
        sky = sky * 0.35 + vec3(0.020, 0.012, 0.008);
    }

    float mu = dot(dir, sun);

    // ---- cumulus deck ----
    vec2 cp;
    if (cloudPlane(uEyePos, dir, cp)) {
        float hf;
        float cover = cumulusDensity(cp, hf);
        float heightFade = smoothstep(0.020, 0.16, h) * (1.0 - smoothstep(0.80, 0.995, h));
        cover *= heightFade;

        if (cover > 0.002) {
            // Self-shadowing: one shifted density sample toward the sun gives
            // the lit top / dark base separation without a full march.
            vec2 sunStep = normalize(sun.xz + vec2(1e-4)) * 0.055;
            float hfDummy;
            float base2 = cumulusDensity(cp + sunStep, hfDummy);
            float tau = max(cover - base2 * 0.45, 0.0) * 3.2 + cover * 1.2;
            float transmit = exp(-tau);

            float phM = min(miePhase(mu, 0.72) * 4.0 * PI, 6.0);
            vec3 sunCol = vec3(1.85, 0.82, 0.34) * viewT * kSunI * 0.20;
            vec3 ambCol = vec3(0.44, 0.25, 0.135) * (0.55 + 0.45 * hf);

            vec3 cloud = ambCol * (0.55 + 0.45 * hf);
            cloud += sunCol * (0.10 + 0.90 * transmit)
                   * (0.35 + 0.85 * phM) * (0.45 + 0.55 * hf);

            float powder = 1.0 - exp(-tau * 2.5);
            cloud *= 1.0 - 0.30 * powder;

            float silver = pow(max(mu, 0.0), 10.0) * (1.0 - cover) * transmit * 1.8;
            cloud += sunCol * silver;

            float alpha = 1.0 - exp(-cover * 1.6);
            cloud = mix(cloud, sky, (1.0 - exp(-h * 5.5)) * 0.30);
            sky = mix(sky, cloud, clamp(alpha, 0.0, 0.94));
        }
    }

    // ---- high cirrus ----
    float veil = cirrus(dir);
    if (veil > 0.002) {
        float fwd = pow(max(mu, 0.0), 5.0);
        vec3 cirCol = mix(vec3(0.34, 0.22, 0.18), vec3(1.20, 0.56, 0.26),
                          0.18 + 0.82 * fwd);
        sky = mix(sky, cirCol * (0.55 + 0.45 * viewT.g), veil * 0.22);
    }

    // ---- sun: real angular radius, limb darkening, physical transmittance ----
    float d2 = 2.0 * (1.0 - mu);
    float R2 = kSunAngularRadius * kSunAngularRadius;
    float disc = 1.0 - smoothstep(R2 * 0.74, R2 * 1.20, d2);
    float limb = 0.40 + 0.60 * sqrt(clamp(1.0 - d2 / (R2 * 1.10), 0.0, 1.0));
    vec3 sunDisc = vec3(1.0, 0.88, 0.74) * viewT * kSunI * 2.6
                 * disc * limb;
    // Occlusion by the cumulus deck.
    vec2 cp2;
    float cover2 = 0.0;
    if (cloudPlane(uEyePos, dir, cp2)) {
        float hf2;
        cover2 = cumulusDensity(cp2, hf2) * smoothstep(0.020, 0.16, h);
    }
    sunDisc *= 1.0 - 0.94 * clamp(cover2, 0.0, 1.0);

    float aureole = pow(max(mu, 0.0), 260.0) * 0.50
                  + pow(max(mu, 0.0), 1100.0) * 1.0;
    sunDisc += vec3(1.0, 0.58, 0.28) * viewT * aureole * (1.0 - cover2);
    sky += sunDisc;

    if (uHdrOut > 0.5) {
        gl_FragColor = vec4(max(sky, vec3(0.0)), 1.0);
        return;
    }

    float dith = vhash(dir.xy * 1913.7 + fract(uTime) * 17.0);
    sky += (dith - 0.5) * (1.5 / 255.0);
    gl_FragColor = vec4(encodeSky(sky), 1.0);
}
