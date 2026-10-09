#version 330 core


in vec3 vWorld;
in vec2 vUV;

uniform sampler2D uRippleTex;
uniform samplerCube uSkyEnvTex;
uniform sampler2D uFoamTex;
uniform float uTime;
uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform vec3  uHorizonColor;
uniform vec3  uWaterColor;

#define MAX_CLOUDS 9
uniform int   uCloudCount;
uniform float uCloudAzim[MAX_CLOUDS];
uniform float uCloudElev[MAX_CLOUDS];
uniform float uCloudRadius[MAX_CLOUDS];
uniform float uCloudStretch[MAX_CLOUDS];

out vec4 fragColor;

const float PI = 3.14159265359;


float waveHeight(vec2 p, float t) {
    float h = 0.0;
    h += sin(dot(p, vec2(0.98,  0.20)) * 0.170 + t * 1.30) * 1.55;
    h += sin(dot(p, vec2(-0.64,  0.77)) * 0.240 + t * 1.60) * 1.00;
    h += sin(dot(p, vec2( 0.36, -0.93)) * 0.380 + t * 2.10) * 0.55;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.34;
    h += sin(dot(p, vec2( 0.59,  0.81)) * 0.860 + t * 3.40) * 0.20;
    h += sin(dot(p, vec2(-0.20,  0.98)) * 1.450 + t * 4.40) * 0.11;
    return h;
}


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


void detailNormals(vec2 p, float t, float dist, inout vec2 grad) {
    float fade = exp(-dist * 0.004);
    if (fade < 0.02) return;
    float w1 = sin(dot(p, vec2(0.86, 0.51)) * 1.15 + t * 5.10);
    float w2 = sin(dot(p, vec2(-0.44, 0.90)) * 2.30 + t * 6.80);
    float w3 = sin(dot(p, vec2(0.22, -0.97)) * 4.40 + t * 8.60);
    grad += vec2(0.86, 0.51) * 1.15 * w1 * 0.045;
    grad += vec2(-0.44, 0.90) * 2.30 * w2 * 0.022;
    grad += vec2(0.22, -0.97) * 4.40 * w3 * 0.010;
}

void main() {
    vec3 sd = normalize(uSunDir);
    vec3 dir = normalize(vWorld - uEyePos);


    float dist = length(vWorld.xz - uEyePos.xz);
    float e = 0.35;
    float hC = waveHeight(vWorld.xz, uTime);
    float hX = waveHeight(vWorld.xz + vec2(e, 0.0), uTime);
    float hZ = waveHeight(vWorld.xz + vec2(0.0, e), uTime);
    vec2 grad = vec2(-(hX - hC) / e, -(hZ - hC) / e);
    detailNormals(vWorld.xz, uTime, dist, grad);
    vec3 N = normalize(vec3(grad.x, 1.0, grad.y));


    float rippleFade = exp(-dist * 0.0018);
    vec2 uvA = vUV * 40.0 + vec2(uTime * 0.0040, uTime * 0.0031);
    vec2 uvB = vUV * 97.0 - vec2(uTime * 0.0026, uTime * 0.0042);
    vec2 ripA = texture(uRippleTex, uvA).rg;
    vec2 ripB = texture(uRippleTex, uvB).rg;
    vec2 pert = (ripA + ripB) * 0.5 * rippleFade;


    vec3 V = normalize(uEyePos - vWorld);
    vec3 R = reflect(-V, N);
    R = normalize(R + vec3(pert.x, 0.0, pert.y) * 1.25);


    R.y = abs(R.y) * 0.30 + 0.45;
    R = normalize(R);


    vec3 L = normalize(uSunDir);
    vec2 sunXZ = normalize(L.xz);
    vec2 dirXZ = normalize(vWorld.xz - uEyePos.xz + vec2(1e-4));
    float sunAlign = max(dot(dirXZ, sunXZ), 0.0);
    vec3 Rdark = normalize(vec3(R.x, abs(R.y) * 1.8 + 0.62, R.z));
    R = normalize(mix(Rdark, R, pow(sunAlign, 6.0)));


    float reflDist = dist;
    vec3 reflColor = textureLod(uSkyEnvTex, R, clamp(1.5 + reflDist * 0.0012, 1.0, 5.0)).rgb;
    float offSun = 1.0 - smoothstep(0.08, 0.45, sunAlign);
    reflColor *= mix(1.0, 0.46, offSun);


    float NdV = max(dot(N, V), 0.0);
    float fresnel = 0.022 + 0.978 * pow(1.0 - NdV, 5.0);


    fresnel = clamp(fresnel * 1.25 + 0.045, 0.0, 0.92);


    vec3 body = uWaterColor * 0.55 + uHorizonColor * 0.03;


    float sunDiffuse = max(dot(N, L), 0.0);
    float warmGate = pow(sunAlign, 3.0);


    float shadow = cloudShadow(vWorld, sd);


    body *= 0.44 + 0.40 * sunDiffuse * warmGate * shadow + 0.12 * sunDiffuse * (0.35 + 0.65 * shadow);
    body *= mix(0.40, 1.0, warmGate * shadow + (1.0 - warmGate) * 0.25 * shadow);
    body *= mix(0.52, 1.0, 1.0 - offSun);
    body += vec3(1.05, 0.42, 0.20) * pow(sunDiffuse, 3.0) * warmGate * shadow * 0.42;


    float crest = smoothstep(0.55, 1.25, hC) * mix(0.25, 1.0, shadow);
    float slopeFacing = max(dot(N, L), 0.0) * warmGate + 0.15;
    float chaos = texture(uRippleTex, vUV * 190.0 + vec2(uTime * 0.011, -uTime * 0.007)).g;
    float foam = texture(uFoamTex, vUV * 23.0 + vec2(uTime * 0.010, 0.0)).r;
    foam *= texture(uFoamTex, vUV * 41.0 - vec2(0.0, uTime * 0.013)).g;
    foam *= crest * slopeFacing * (0.35 + 0.65 * chaos);
    body += vec3(0.55, 0.48, 0.58) * foam * 0.55;


    body += vec3(0.05, 0.18, 0.14) * sunAlign * warmGate * shadow * crest * 0.55;

    vec3 color = mix(body, reflColor, fresnel);


    vec2 dxdz = vWorld.xz - uEyePos.xz;
    vec3 skyAtHorizon = texture(uSkyEnvTex,
        normalize(vec3(dxdz.x, 0.012, dxdz.y))).rgb;
    float horizonMix = smoothstep(1500.0, 1900.0, dist);
    color = mix(color, skyAtHorizon, horizonMix * 0.85);


    float haze = 1.0 - exp(-dist * 0.00075);
    color = mix(color, skyAtHorizon, haze * (0.30 + 0.70 * haze));


    vec3 H = normalize(L + V);
    float NdH = max(dot(N, H), 0.0);
    float pathGate = pow(sunAlign, 10.0) * 0.96 + 0.04;
    float sparkleGate = (0.55 + 0.90 * chaos);
    float glint = pow(NdH, 520.0) * 6.0;
    float glintMid = pow(NdH, 90.0) * 0.55;
    float glintWide = pow(NdH, 14.0) * 0.22;
    color += vec3(1.0, 0.56, 0.24) * (glint + glintMid + glintWide * pathGate)
             * (0.25 + max(L.y, 0.0) * 1.2) * pathGate * shadow * sparkleGate;


    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0);
}
