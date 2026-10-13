// GENERATED from shaders/pole/object_frag.glsl by vitaport/tools/make_vita_shaders.py -- do not edit.
// GLSL 1.00-style rewrite for vitaGL/SceShaccCg (see that script's RULES); the
// math is identical to the desktop shader except for the documented rewrites.
#version 100
#extension GL_OES_standard_derivatives : enable
precision mediump float;


varying vec3 vWorld;
varying vec3 vNormal;
varying vec3 vColor;
varying float vMat;
varying float vAlpha;



uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform float uTime;
uniform float uRoadX;


const float kMatPaint  = 0.0;
const float kMatWood   = 1.0;
const float kMatGround = 2.0;
const float kMatRoad   = 3.0;
const float kMatLine   = 4.0;
const float kMatCable  = 5.0;
const float kMatCeramic= 6.0;
const float kMatMetal  = 7.0;
const float kMatWall   = 8.0;
const float kMatRoof   = 9.0;
const float kMatGlass  = 10.0;
const float kMatLeaf   = 11.0;
const float kMatShadow = 12.0;
const float kMatGlow   = 13.0;
const float kMatSteel  = 14.0;
const float kMatKerb   = 15.0;
const float kMatConcrete = 16.0;


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


vec3 hazeColFor(vec3 base, vec3 L, vec3 V) {
    float toSun = max(dot(-V, L), 0.0);


    float t3 = toSun * toSun * toSun;
    return mix(vec3(0.70, 0.52, 0.37), vec3(0.98, 0.75, 0.50), t3);
}


float octaveRes(float foot, float freq) {
    return 1.0 - smoothstep(0.35, 1.10, foot * freq);
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


    if (abs(m - kMatShadow) < 0.5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, vAlpha);
        return;
    }

    vec3 N = normalize(vNormal);
    vec3 toEye = uEyePos - vWorld;
    float dist = length(toEye);
    vec3 V = toEye / max(dist, 1e-4);

    if (dot(N, V) < 0.0) N = -N;

    vec3 L = normalize(uSunDir);
    vec3 P = vWorld;
    float near = 1.0 - smoothstep(16.0, 64.0, dist);


    float foot = (abs(dFdx(P.x)) + abs(dFdy(P.x))) + (abs(dFdx(P.z)) + abs(dFdy(P.z))) + 0.0015;

    vec3 base = vColor;
    float rough = 0.85;
    float sheen = 0.0;
    float wet = 0.0;

    if (abs(m - kMatGround) < 0.5) {


        float n1 = vnoise(P.xz * 0.42);
        float n2 = vnoise(P.xz * 2.10);


        float gMid = octaveRes(foot, 4.0);
        float nMid = gMid > 0.02 ? vnoise(P.xz * 4.0 + 3.0) : 0.5;


        float n0 = vnoise(P.xz * 0.135 + 3.7);
        float g3 = octaveRes(foot, 7.5);
        float n3 = g3 > 0.02 ? vnoise(P.xz * 7.5) : 0.5;
        base *= 0.46 + 0.32 * n0 + 0.44 * n1 + 0.38 * n2 + 0.22 * nMid * gMid
                + 0.22 * n3 * g3;
        base = mix(base, vec3(0.150, 0.155, 0.082),
                   smoothstep(0.48, 0.95, n1) * 0.55);

        float nearRoad = exp(-pow((abs(P.x - uRoadX) - 3.6) / 1.5, 2.0));
        base = mix(base, base * 1.22 + vec3(0.020, 0.016, 0.010),
                   nearRoad * 0.55);
        rough = 1.0;
    } else if (abs(m - kMatRoad) < 0.5) {
        float rx = P.x - uRoadX;
        float n = vnoise(P.xz * 1.30);


        base *= 0.66 + 0.62 * n;


        float gLong = octaveRes(foot, 2.6);
        float nLong = gLong > 0.02
                          ? vnoise(vec2(P.x * 2.6, P.z * 0.16 + 5.0))
                          : 0.5;
        base *= mix(1.0, 0.70 + 0.60 * nLong, gLong);


        float joint = 1.0 - 0.16 * exp(-pow(fract(P.z * 0.238) / 0.06, 2.0));
        base *= joint;


        float wp = exp(-pow((abs(rx) - 1.05) / 0.44, 2.0));
        float wpHalo = exp(-pow((abs(rx) - 1.05) / 0.95, 2.0));
        base = mix(base, base * 1.42 + vec3(0.010), wp * 0.62);
        base = mix(base, base * 1.12, wpHalo * 0.20);


        float gRoad = octaveRes(foot, 11.0);
        if (gRoad > 0.02)
            base += (vnoise(P.xz * 11.0) - 0.5) * 0.20 * gRoad;

        base *= 1.0 - 0.18 * exp(-pow(rx / 0.55, 2.0));
        rough = mix(0.95, 0.62, wp);


        float damp = smoothstep(0.52, 0.86, vnoise(P.xz * 0.30 + 11.0));


        damp = clamp(damp * 0.62 + wpHalo * 0.44, 0.0, 1.0);

        damp = clamp(damp + 0.55 * exp(-pow((abs(rx) - 2.45) / 0.38, 2.0)), 0.0, 1.0);
        base *= mix(1.0, 0.55, damp);
        rough = mix(rough, 0.20, damp);
        wet = damp;
    } else if (abs(m - kMatLine) < 0.5) {

        float wear = octaveRes(foot, 6.0);
        float wearN = wear > 0.02 ? vnoise(P.xz * 6.0) : 0.5;
        base *= 0.78 + 0.34 * wearN * wear + 0.18 * (1.0 - near);
        rough = 0.85;
    } else if (abs(m - kMatWood) < 0.5) {

        float along = P.y * 1.15 + (P.x + P.z) * 0.06;
        float g1 = vnoise(vec2((P.x + P.z) * 6.5, along));
        float gGrain = octaveRes(foot, 19.0);
        float g2 = gGrain > 0.02 ? vnoise(vec2((P.x + P.z) * 19.0, along * 2.6))
                                 : 0.5;
        base *= 0.80 + 0.28 * g1 + 0.16 * g2 * gGrain;

        base *= mix(0.70, 1.0, smoothstep(0.0, 1.20, P.y));
        rough = 0.95;
    } else if (abs(m - kMatWall) < 0.5) {

        float seam = abs(fract(P.y * 1.25) - 0.5) * 2.0;
        base *= mix(0.70, 1.10, smoothstep(0.06, 0.34, seam));
        base *= 0.88 + 0.24 * vnoise(vec2((P.x + P.z) * 2.2, P.y * 0.9));

        base *= mix(0.55, 1.0, smoothstep(0.0, 0.55, P.y));
        rough = 0.9;
    } else if (abs(m - kMatRoof) < 0.5) {

        float course = abs(fract(P.z * 0.85 + P.x * 0.0) - 0.5) * 2.0;
        base *= mix(0.70, 1.12, smoothstep(0.12, 0.55, course));
        base *= 0.84 + 0.32 * vnoise(P.xz * 1.7);
        rough = 0.85;
    } else if (abs(m - kMatGlass) < 0.5) {

        base *= 0.42;
        rough = 0.06;
        sheen = 0.55;
    } else if (abs(m - kMatKerb) < 0.5) {


        float agg = vnoise(vec2(P.x * 41.0, P.z * 9.0));
        float cement = vnoise(vec2(P.x * 3.1, P.z * 0.85));
        base *= 0.80 + 0.30 * cement;
        float gKerb = octaveRes(foot, 41.0);
        if (gKerb > 0.02) {


            float stones = smoothstep(0.62, 0.86, agg);
            base = mix(base, base * 1.85 + vec3(0.020), stones * gKerb * 0.75);
        }

        float gz = 1.0 - clamp(P.y / 0.16, 0.0, 1.0);
        base = mix(base, vec3(0.115, 0.098, 0.078), smoothstep(0.35, 1.0, gz) * 0.60);

        wet = 0.40 * smoothstep(0.55, 0.95, gz);
        base *= mix(1.0, 0.55, wet);
        rough = mix(0.88, 0.30, wet);
        sheen = 0.10;
    } else if (abs(m - kMatLeaf) < 0.5) {

        float n = vnoise(P.xz * 0.85 + P.y * 0.35);
        base *= 0.72 + 0.46 * n;
        rough = 1.0;
    } else if (abs(m - kMatCable) < 0.5) {


        rough = 0.33;
        sheen = 0.48;
    } else if (abs(m - kMatCeramic) < 0.5) {
        rough = 0.22;
        sheen = 0.30;
    } else if (abs(m - kMatMetal) < 0.5) {
        rough = 0.38;
        sheen = 0.22;
    } else if (abs(m - kMatSteel) < 0.5) {


        float streak = vnoise(vec2((P.x + P.z * 0.35) * 1.60, P.y * 0.22));


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


        sheen = 0.24;
    } else if (abs(m - kMatConcrete) < 0.5) {


        float streak = vnoise(vec2((P.x + P.z * 0.45) * 2.10, P.y * 0.10));


        float band = vnoise(vec2((P.x + P.z * 0.45) * 0.35, P.y * 0.045));
        base *= 0.62 + 0.52 * streak;
        base *= 0.80 + 0.34 * band;


        float board = abs(fract(P.y * 1.62) - 0.5) * 2.0;
        base *= mix(0.94, 1.05, smoothstep(0.04, 0.26, board));


        float gSpall = octaveRes(foot, 7.5);
        if (gSpall > 0.02) {
            float spall = smoothstep(0.70, 0.93,
                                     vnoise(vec2((P.x + P.z) * 7.5, P.y * 1.7)));
            base = mix(base, vec3(0.180, 0.172, 0.162), spall * 0.55 * gSpall);
        }


        base = mix(base, vec3(0.168, 0.158, 0.140),
                   smoothstep(2.4, 0.10, P.y) * 0.42);
        base = mix(base, base * 0.82,
                   smoothstep(0.55, 0.95, streak) * 0.30);


        float board2 = abs(fract(P.y * 1.62 + band * 0.35) - 0.5) * 2.0;
        base *= mix(0.95, 1.04, smoothstep(0.04, 0.26, board2));
        rough = 0.90 - 0.10 * streak;
        sheen = 0.06;
    } else if (abs(m - kMatGlow) < 0.5) {


        float distG = dist;
        float hz = 1.0 - exp(-distG * 0.0016);
        vec3 g = base * 1.05;
        g = mix(g, hazeColFor(g, L, V), clamp(hz, 0.0, 0.45));
        gl_FragColor = vec4(pow(max(g, vec3(0.0)), vec3(1.0 / 2.2)), 1.0);
        return;
    }


    float up = N.y * 0.5 + 0.5;
    vec3 ambGround = vec3(0.125, 0.086, 0.064);
    vec3 ambSky    = vec3(0.395, 0.340, 0.360);
    vec3 amb = mix(ambGround, ambSky, up);


    float wrap = (abs(m - kMatCable) < 0.5) ? 0.35 : 0.0;


    if (abs(m - kMatSteel) < 0.5) wrap = 0.13;
    float diff = clamp((dot(N, L) + wrap) / (1.0 + wrap), 0.0, 1.0);
    vec3 sunTint = vec3(1.00, 0.66, 0.34);


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


    float fu = 1.0 - VoH;
    float fu2 = fu * fu;
    float Fs = 0.045 + 0.955 * (fu2 * fu2 * fu);


    float spR = D * G * Fs / (4.0 * NoV) * NoL;
    float sp = 1.35 * spR / (1.0 + spR * 0.2470);


    vec3 skyRefl = vec3(0.0);
    float wetF = 0.0;
    if (wet > 0.01) {
        vec3 Np = N;
        float rip = vnoise(P.xz * 2.7) - 0.5;
        float rip2 = vnoise(P.xz * 2.7 + 19.0) - 0.5;
        Np = normalize(N + vec3(rip, 0.0, rip2) * (0.16 * wet));
        vec3 R = reflect(-V, Np);
        R.y = abs(R.y);
        skyRefl = atmosphere(normalize(R), L);


        wetF = (0.030 + 0.970 * fu * fu * fu * fu * fu) * wet * 0.62;

        diff *= mix(1.0, 0.55, wet);
    }


    float edge = 1.0 - abs(dot(N, V));
    float e2 = edge * edge;
    float e3 = e2 * edge;
    float rim = e3 * e3 * sheen;

    vec3 col = base * (amb + sunTint * diff * 1.15) + sunTint * (sp + rim)
             + skyRefl * wetF;


    if (abs(m - kMatKerb) < 0.5)
        col *= mix(0.80, 1.0, smoothstep(0.0, 0.16, P.y));
    else if (abs(m - kMatGround) >= 0.5 && abs(m - kMatRoad) >= 0.5 && abs(m - kMatLine) >= 0.5)
        col *= mix(0.60, 1.0, smoothstep(0.0, 1.30, P.y));


    float hFall = exp(-max(P.y, 0.0) * 0.11);
    float fog = 1.0 - exp(-dist * 0.0018 * hFall);
    fog += 0.09 * (1.0 - exp(-dist * 0.020));
    float toSun = max(dot(-V, L), 0.0);
    vec3 hazeCol = hazeColFor(base, L, V);
    col = mix(col, hazeCol, clamp(fog, 0.0, 0.70));


    float silh = clamp(1.0 - dot(base, vec3(0.333)) * 2.6, 0.0, 1.0);
    col = mix(col, vec3(0.52, 0.30, 0.14), silh * 0.16);


    col = clamp(col, 0.0, 4.0);
    col = col / (col * 0.35 + vec3(0.72));


    float lum = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = mix(col, col * vec3(0.93, 0.98, 1.13),
              (1.0 - smoothstep(0.02, 0.34, lum)) * 0.50);
    col = mix(col, col * vec3(1.07, 1.01, 0.92),
              smoothstep(0.52, 1.00, lum) * 0.45);
    col = clamp(mix(vec3(lum), col, 1.12), 0.0, 1.0);


    col += (hash21(gl_FragCoord.xy + fract(uTime) * 13.0)
            + hash21(gl_FragCoord.xy * 1.7 + 41.0) - 1.0) * (1.5 / 255.0);
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    gl_FragColor = vec4(col, 1.0);
}
