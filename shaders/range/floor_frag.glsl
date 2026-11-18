#version 330 core

// Scene 1 -- the carousel slab.
//
// A single dark polished-concrete disc: radial control joints, fine aggregate,
// wet patches that mirror the guns and the light bars, thin amber inlay rings
// between the gun rings, and the key light's contact shadows. The mirror is a
// real planar reflection pass (half resolution), sampled projectively through
// the reflected camera and blurred by the local roughness.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 fragColor;

uniform sampler2D uReflectTex;
uniform sampler2D uShadowMap;
uniform mat4 uReflectVP;
uniform mat4 uLightVP;
uniform vec2 uShadowTexel;

uniform vec3 uEye;
uniform vec3 uKeyDir;
uniform vec3 uKeyColor;
uniform vec3 uRimPos;
uniform vec3 uRimColor;
uniform vec3 uFillPos;
uniform vec3 uFillColor;
uniform vec3 uSkyAmb;
uniform vec3 uGroundAmb;
uniform vec3 uGlowAmb;
uniform vec3 uBarPos[3];
uniform vec3 uBarColor[3];
uniform float uTime;

const float kPi = 3.14159265;

float Hash(vec2 p) {
    vec3 q = fract(vec3(p.xyx) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}

float Noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = Hash(i), b = Hash(i + vec2(1.0, 0.0));
    float c = Hash(i + vec2(0.0, 1.0)), d = Hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float Fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 4; i++) {
        s += a * Noise(p);
        p = p * 2.03 + vec2(11.3, 7.7);
        a *= 0.5;
    }
    return s;
}

float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d + 1e-7);
}

float VisSmith(float NoV, float NoL, float a) {
    float a2 = a * a;
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-6);
}

vec3 F_Schlick(vec3 f0, float u) {
    return f0 + (1.0 - f0) * pow(1.0 - u, 5.0);
}

vec3 Spec(vec3 N, vec3 V, vec3 L, vec3 rad, float rough, vec3 f0) {
    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float a = max(rough * rough, 0.002);
    vec3 F = F_Schlick(f0, max(dot(V, H), 0.0));
    return vec3(D_GGX(max(dot(N, H), 0.0), a) * VisSmith(NoV, NoL, a)) * F * rad * NoL;
}

float ShadowFactor(vec3 N) {
    vec4 lp = uLightVP * vec4(vWorldPos, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float ndl = clamp(dot(N, uKeyDir), 0.0, 1.0);
    float bias = max(0.0022 * (1.0 - ndl), 0.0007);
    float sum = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float d = texture(uShadowMap, p.xy + vec2(x, y) * uShadowTexel * 1.6).r;
            sum += (p.z - bias > d) ? 0.0 : 1.0;
        }
    }
    return sum / 9.0;
}

void main() {
    vec2 p = vWorldPos.xz;
    float r = length(p);
    float ang = atan(p.y, p.x);

    // ---- wetness: broad puddles plus a damp film over everything ----------
    float puddle = Fbm(p * 0.55 + vec2(3.1, 9.4));
    float wet = smoothstep(0.44, 0.68, puddle);
    wet = max(wet, 0.28);

    // ---- concrete base ----------------------------------------------------
    float grain = Noise(p * 34.0) * 0.5 + Noise(p * 90.0) * 0.5;
    vec3 dry = vec3(0.060, 0.056, 0.052) * (0.72 + 0.55 * grain);
    vec3 wetCol = vec3(0.016, 0.016, 0.018) * (0.80 + 0.40 * grain);
    vec3 albedo = mix(dry, wetCol, wet);

    // radial control joints every metre and a half, plus 24 sector joints
    float ringJoint = abs(fract(r / 1.5 + 0.5) - 0.5) * 1.5;
    float sector = abs(fract(ang / kPi * 12.0 + 0.5) - 0.5);
    float joint = smoothstep(0.014, 0.0, ringJoint) + smoothstep(0.020, 0.0, sector);
    albedo *= 1.0 - 0.82 * clamp(joint, 0.0, 1.0);

    // ---- surface normal: aggregate, joints, and water ripple --------------
    float e = 0.018;
    float hL = Fbm((p - vec2(e, 0.0)) * 0.55);
    float hR = Fbm((p + vec2(e, 0.0)) * 0.55);
    float hD = Fbm((p - vec2(0.0, e)) * 0.55);
    float hU = Fbm((p + vec2(0.0, e)) * 0.55);
    vec2 tilt = vec2(hR - hL, hU - hD) * 0.16;
    tilt += vec2(Noise(p * 26.0) - 0.5, Noise(p * 26.0 + 5.0) - 0.5) * 0.03;
    float ripple = sin(p.x * 7.0 + uTime * 1.1) * cos(p.y * 6.3 - uTime * 0.8);
    tilt += vec2(ripple * 0.006, ripple * 0.004) * wet;
    vec3 N = normalize(vec3(tilt.x, 1.0, tilt.y));

    vec3 V = normalize(uEye - vWorldPos);
    float NoV = max(dot(N, V), 1e-4);

    // ---- planar reflection ------------------------------------------------
    vec4 rp = uReflectVP * vec4(vWorldPos, 1.0);
    vec2 ruv = rp.xy / max(rp.w, 1e-4) * 0.5 + 0.5;
    ruv += vec2(ripple * 0.0018, ripple * 0.0011) * wet;
    float rough = mix(0.42, 0.14, wet);
    float spread = mix(0.0035, 0.0007, wet);
    vec3 mirror = vec3(0.0);
    for (int i = 0; i < 7; i++) {
        float o = (float(i) / 6.0 - 0.5) * 2.0;
        vec2 off = vec2(o * spread * 0.65, o * spread * 1.1);
        mirror += texture(uReflectTex, ruv + off).rgb;
    }
    // cap the mirror: the emissive bars reflected at grazing angles are far
    // brighter than anything else in the reflection and read as fog otherwise
    mirror = min(mirror / 7.0, vec3(1.10)) * 0.80;

    // ---- lighting ---------------------------------------------------------
    float shadow = ShadowFactor(N);
    vec3 f0 = vec3(0.045);
    vec3 diff = albedo;

    vec3 col = diff * uSkyAmb * (0.55 + 0.45 * N.y);
    col += diff * uGroundAmb * 0.55;
    col += diff * uKeyColor * shadow * max(dot(N, uKeyDir), 0.0) / kPi;

    // the three light bars: visible pools where their haze reaches the slab
    for (int i = 0; i < 3; i++) {
        vec3 d = uBarPos[i] - vWorldPos;
        float dist = length(d);
        vec3 L = d / max(dist, 1e-4);
        float atten = 1.0 / (1.0 + 0.10 * dist + 0.075 * dist * dist);
        float pool = pow(max(L.y, 0.0), 3.0);
        col += diff * uBarColor[i] * atten * pool * 0.70;
        // a broad, soft sheen only: a tight lobe from these big area lights
        // broke up over the wet/dry normal into cloudy blotches
        col += Spec(N, V, L, uBarColor[i] * atten * 0.35, 0.42, f0);
    }

    col += Spec(N, V, uKeyDir, uKeyColor * shadow * 0.6, rough, f0);
    {
        vec3 d = uRimPos - vWorldPos;
        float dist = length(d);
        float atten = 1.0 / (1.0 + 0.25 * dist + 0.20 * dist * dist);
        col += Spec(N, V, d / max(dist, 1e-4), uRimColor * atten * 0.5, rough, f0);
    }
    {
        vec3 d = uFillPos - vWorldPos;
        float dist = length(d);
        float atten = 1.0 / (1.0 + 0.25 * dist + 0.20 * dist * dist);
        col += Spec(N, V, d / max(dist, 1e-4), uFillColor * atten * 0.5, rough, f0);
    }

    // Fresnel-weighted mirror over the diffuse base.
    float fres = 0.035 + 0.965 * pow(1.0 - NoV, 5.0);
    float mixAmount = clamp(fres * 1.25 + wet * 0.32, 0.0, 0.90);
    col = mix(col, mirror, mixAmount);

    // ---- glowing inlay rings between the gun rings ------------------------
    float inlay = 0.0;
    for (int i = 0; i < 5; i++) {
        float rr = 1.45 + 0.70 * float(i);
        inlay += exp(-pow((r - rr) / 0.008, 2.0));
    }
    inlay = clamp(inlay, 0.0, 1.0) * (0.75 + 0.25 * sin(ang * 6.0));
    col += vec3(1.0, 0.46, 0.16) * inlay * 1.0;

    // ---- haze towards the wall -------------------------------------------
    float fog = 1.0 - exp(-max(0.0, r - 5.0) * 0.075);
    col = mix(col, uGlowAmb * 0.08, clamp(fog, 0.0, 0.85));

    fragColor = vec4(max(col, vec3(0.0)), 1.0);
}
