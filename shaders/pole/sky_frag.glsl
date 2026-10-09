#version 330 core


in vec3 vDir;

out vec4 fragColor;

uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform float uTime;


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


const vec3  kBetaR = vec3(0.058, 0.135, 0.331);


const float kBetaM = 0.0092;
const float kSunI  = 330.0;


const float kSunPath = 14.0;


const float kRayGain = 13.0;


vec3 atmosphere(vec3 dir, vec3 sun) {
    float h = dir.y;
    float mu = dot(dir, sun);
    float mView = 1.0 / (max(h, 0.0) + 0.14);
    float mSun  = 1.0 / (max(sun.y, 0.0) + 0.14);


    vec3 Tview = exp(-(kBetaR + vec3(kBetaM)) * mView * 0.92);


    vec3 Tsun = exp(-kBetaR * mSun * kSunPath - vec3(kBetaM * mSun * 1.15));

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
    float mu = dot(dir, sun);


    vec3 sky;
    if (h >= -0.02) {
        sky = atmosphere(vec3(dir.x, max(h, 0.0), dir.z), sun);


        float hz = exp(-max(h, 0.0) * 7.0);


        sky += (vec3(0.450, 0.121, 0.031)
              + vec3(0.480, 0.147, 0.038) * pow(max(mu, 0.0), 3.0))
             * exp(-max(h, 0.0) * 5.6);
    } else {


        sky = atmosphere(vec3(dir.x, 0.015, dir.z), sun) * 0.42
            + vec3(0.030, 0.018, 0.010);
    }


    float cover = 0.0;
    vec3 cloudCol = vec3(0.0);
    if (h > 0.030) {
        float hh = max(h, 0.075);
        vec2 cp = dir.xz / hh * 0.30;
        vec2 drift = vec2(uTime * 0.0060, -uTime * 0.0023);


        float base = fbm3(cp * 1.75 + drift);


        float det  = fbm2(cp * 6.50 - drift * 2.4 + 31.0);


        float dens = base - 0.32 * (det - 0.5);


        cover = smoothstep(0.560, 0.780, dens) * 0.94;
        cover *= 0.80 + 0.20 * smoothstep(0.40, 0.66, det);

        cover *= smoothstep(0.035, 0.26, h);
        cover *= 1.0 - smoothstep(0.62, 0.99, h);


        vec3 crown = vec3(1.42, 0.775, 0.400);
        vec3 belly = vec3(0.40, 0.106, 0.058);
        float lift = pow(clamp(cover, 0.0, 1.0), 0.55);
        cloudCol = mix(belly, crown, lift);
        float silver = pow(max(mu, 0.0), 14.0) * (1.0 - cover) * 1.35;
        cloudCol += vec3(1.00, 0.62, 0.30) * silver;

        cloudCol += vec3(0.22, 0.105, 0.062) * (1.0 - lift) * max(mu, 0.0);
    }


    {


        float ch = max(h, 0.055);
        vec2 vp = dir.xz / ch * 0.30;


        vec2 cir = vec2(vp.x * 9.0 - uTime * 0.010, vp.y * 4.5 + uTime * 0.004);
        float cn = fbm2(cir);
        float veil = smoothstep(0.48, 0.82, cn);
        veil *= smoothstep(0.035, 0.20, h) * (1.0 - smoothstep(0.55, 0.95, h));


        float toward = pow(max(mu, 0.0), 2.2);


        vec3 cirCol = mix(vec3(0.145, 0.070, 0.058), vec3(0.78, 0.32, 0.19),
                          0.24 + 0.76 * toward);
        sky = mix(sky, cirCol, clamp(veil * 0.26, 0.0, 0.26));
    }


    float d2 = 2.0 * (1.0 - mu);
    float R2 = 0.0026;
    float disc = smoothstep(R2 * 1.10, R2 * 0.82, d2);
    float limb = 0.55 + 0.45 * sqrt(clamp(1.0 - d2 / (R2 * 1.05), 0.0, 1.0));


    vec3 sunTrans = exp(-kBetaR * (1.0 / (max(sun.y, 0.0) + 0.14)) * kSunPath
                        - vec3(kBetaM * 1.15));


    sky += vec3(1.0, 0.72, 0.42) * sunTrans * disc * limb * 1.05;

    sky = mix(sky, cloudCol, clamp(cover, 0.0, 1.0));


    float dith = vhash(dir.xy * 1913.7 + fract(uTime) * 17.0);
    sky += (dith - 0.5) * (1.5 / 255.0);

    fragColor = vec4(encodeSky(sky), 1.0);
}
