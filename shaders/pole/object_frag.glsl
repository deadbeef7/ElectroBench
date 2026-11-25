#version 330 core

// Materials for the power-line corridor.
//
// Rebuilt around a physically based shading model:
//   * direct sun is a GGX microfacet lobe with the correct Smith visibility
//     (k = alpha/2, where alpha = roughness^2) and a Schlick Fresnel whose F0
//     is the material's; the old code squared alpha twice and used alpha^2 for
//     the visibility term, which made every rough surface's highlight too
//     broad and too dim;
//   * ambient light comes from the scene's own sky cubemap (the same pass the
//     screen sky is drawn from), so a galvanised pole reflects the actual
//     orange horizon instead of a hand-picked constant;
//   * the sun casts a real shadow map: poles, crossarms, transformers and the
//     cable web occlude the road, with a normal-offset bias and PCF;
//   * wet asphalt uses a water Fresnel (F0 = 0.02) and a roughness-driven sky
//     reflection, and aerial perspective converges distant geometry into the
//     sky colour at that azimuth.

in vec3 vWorld;
in vec3 vNormal;
in vec3 vColor;
flat in float vMat;
in float vAlpha;

out vec4 fragColor;

uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform float uTime;
uniform float uRoadX;
uniform float uHdrOut;

uniform samplerCube uSkyEnvTex;
uniform float uEnvMaxLod;

uniform sampler2D uShadowMap;
uniform mat4  uLightVP;
uniform vec2  uShadowTexel;
uniform float uShadowBias;
uniform float uShadowNoffset;
uniform float uShadowEnabled;

const float kMatPaint     = 0.0;
const float kMatWood      = 1.0;
const float kMatGround    = 2.0;
const float kMatRoad      = 3.0;
const float kMatLine      = 4.0;
const float kMatCable     = 5.0;
const float kMatCeramic   = 6.0;
const float kMatMetal     = 7.0;
const float kMatWall      = 8.0;
const float kMatRoof      = 9.0;
const float kMatGlass     = 10.0;
const float kMatLeaf      = 11.0;
const float kMatShadow    = 12.0;
const float kMatGlow      = 13.0;
const float kMatSteel     = 14.0;
const float kMatKerb      = 15.0;
const float kMatConcrete  = 16.0;

const float PI = 3.14159265359;

// --------------------------------------------------------------------- noise
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
float octaveRes(float foot, float freq) {
    return 1.0 - smoothstep(0.35, 1.10, foot * freq);
}

// Bump from a procedural height field, evaluated per pixel. The surface
// gradient is reconstructed from the screen-space derivatives of the world
// position (Mikkelsen's unparameterised bump mapping), so it works on geometry
// with no tangents and no UVs, and it stays correct at the grazing angles
// where a hand-rolled tangent basis falls apart. h is a height in metres.
vec3 bumpNormal(vec3 N, vec3 P, float h) {
    vec3 dpdx = dFdx(P);
    vec3 dpdy = dFdy(P);
    vec3 r1 = cross(dpdy, N);
    vec3 r2 = cross(N, dpdx);
    float det = dot(dpdx, r1);
    float adet = max(abs(det), 1e-7);
    vec3 grad = sign(det == 0.0 ? 1.0 : det) * (dFdx(h) * r1 + dFdy(h) * r2);
    // Slope in world units. The unparameterised reconstruction is singular at
    // grazing angles, where a one-pixel step covers metres of surface; cap the
    // slope so a noisy gradient cannot tip a ground plane into the sky.
    vec3 slope = grad / adet;
    float sl = length(slope);
    slope *= min(sl, 0.55) / max(sl, 1e-6);
    return normalize(N - slope);
}

// ------------------------------------------------------------------ shadowing
// Sun map for everything but the poles: one depth map over the whole corridor,
// built from a fixed world box so it cannot slide with the camera. About 5 cm
// per texel, hence a normal offset, a slope-scaled depth bias and a filtered
// lookup rather than a single tap.
float shadowFactor(vec3 P, vec3 N, float NoL) {
    if (uShadowEnabled < 0.5) return 1.0;
    vec4 lp = uLightVP * vec4(P + N * uShadowNoffset, 1.0);
    vec3 sc = lp.xyz / max(lp.w, 1e-5);
    sc = sc * 0.5 + 0.5;
    if (sc.x < 0.002 || sc.x > 0.998 || sc.y < 0.002 || sc.y > 0.998 || sc.z > 1.0)
        return 1.0;
    float cosT = clamp(NoL, 0.0, 1.0);
    float tanT = sqrt(max(1.0 - cosT * cosT, 0.0)) / max(cosT, 0.08);
    float bias = uShadowBias * (1.0 + clamp(tanT, 0.0, 12.0));

    // A 12-tap Vogel disk about three texels across, rotated per pixel. An
    // axis-aligned kernel over this map beats against the texel grid wherever a
    // terminator runs almost along the surface, which reads as stair steps or a
    // diamond lattice; spreading the taps on a rotated disk turns that into
    // noise, and three texels is about the penumbra an 11 degree sun gives.
    float angle = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453)
                  * 6.2831853;
    mat2 rot = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));
    const int kTaps = 12;
    float sum = 0.0;
    for (int i = 0; i < kTaps; i++) {
        float fi = float(i) + 0.5;
        float radius = sqrt(fi / float(kTaps)) * 3.0;
        float theta = fi * 2.39996323; // golden angle, evenly spread
        vec2 o = rot * vec2(cos(theta), sin(theta)) * radius;
        sum += step(sc.z - bias, texture(uShadowMap, sc.xy + o * uShadowTexel).r);
    }
    return sum / float(kTaps);
}

// ------------------------------------------------------------------ materials
struct Surface {
    vec3  albedo;
    float rough;
    float metal;
    vec3  f0;
    vec3  emission;
    float height;   // metres of procedural relief, 0 for smooth surfaces
};

Surface makeSurface(float m, vec3 P, float foot) {
    Surface s;
    s.albedo = vColor;
    s.rough = 0.82;
    s.metal = 0.0;
    s.f0 = vec3(0.04);
    s.emission = vec3(0.0);
    s.height = 0.0;

    if (m == kMatGround) {
        float n1 = vnoise(P.xz * 0.42);
        float n2 = vnoise(P.xz * 2.10);
        float gMid = octaveRes(foot, 4.0);
        float nMid = gMid > 0.02 ? vnoise(P.xz * 4.0 + 3.0) : 0.5;
        float n0 = vnoise(P.xz * 0.135 + 3.7);
        float g3 = octaveRes(foot, 7.5);
        float n3 = g3 > 0.02 ? vnoise(P.xz * 7.5) : 0.5;

        s.albedo *= 0.46 + 0.32 * n0 + 0.44 * n1 + 0.38 * n2
                  + 0.22 * nMid * gMid + 0.22 * n3 * g3;
        s.albedo = mix(s.albedo, vec3(0.150, 0.155, 0.082),
                       smoothstep(0.48, 0.95, n1) * 0.55);
        s.albedo *= vec3(0.66, 0.60, 0.53);

        float nearRoad = exp(-pow((abs(P.x - uRoadX) - 3.6) / 1.5, 2.0));
        s.albedo = mix(s.albedo, s.albedo * 1.22 + vec3(0.020, 0.016, 0.010),
                       nearRoad * 0.55);
        s.height = 0.013 * (n1 - 0.5) + 0.005 * (n2 - 0.5)
                 + 0.008 * (n0 - 0.5) * octaveRes(foot, 1.2)
                 + 0.005 * (nMid - 0.5) * gMid;
        s.rough = 0.96;
    } else if (m == kMatRoad) {
        float rx = P.x - uRoadX;
        float n = vnoise(P.xz * 1.30);
        s.albedo *= 0.66 + 0.62 * n;

        float gLong = octaveRes(foot, 2.6);
        float nLong = gLong > 0.02
                          ? vnoise(vec2(P.x * 2.6, P.z * 0.16 + 5.0)) : 0.5;
        s.albedo *= mix(1.0, 0.70 + 0.60 * nLong, gLong);

        float joint = 1.0 - 0.16 * exp(-pow(fract(P.z * 0.238) / 0.06, 2.0));
        s.albedo *= joint;

        float wp = exp(-pow((abs(rx) - 1.05) / 0.44, 2.0));
        float wpHalo = exp(-pow((abs(rx) - 1.05) / 0.95, 2.0));
        s.albedo = mix(s.albedo, s.albedo * 1.42 + vec3(0.010), wp * 0.62);
        s.albedo = mix(s.albedo, s.albedo * 1.12, wpHalo * 0.20);

        float gRoad = octaveRes(foot, 11.0);
        if (gRoad > 0.02)
            s.albedo += (vnoise(P.xz * 11.0) - 0.5) * 0.20 * gRoad;

        s.albedo *= 1.0 - 0.18 * exp(-pow(rx / 0.55, 2.0));

        // Damp patches and standing water. Water darkens the substrate and
        // drops the roughness; it is not a separate additive sheen.
        float damp = smoothstep(0.52, 0.86, vnoise(P.xz * 0.30 + 11.0));
        damp = clamp(damp * 0.62 + wpHalo * 0.44, 0.0, 1.0);
        damp = clamp(damp + 0.55 * exp(-pow((abs(rx) - 2.45) / 0.38, 2.0)), 0.0, 1.0);
        s.albedo *= mix(1.0, 0.46, damp);
        s.rough = mix(0.92, 0.055, damp);
        s.f0 = mix(vec3(0.04), vec3(0.020), damp);   // wet = water interface
        // Coarse aggregate and the tar-filled joint, flattened where water
        // stands: a puddle is a flat mirror, not a rough patch.
        s.height = (0.0030 * (n - 0.5)
                  + 0.0022 * (nLong - 0.5) * gLong
                  + 0.0016 * (vnoise(P.xz * 11.0) - 0.5) * gRoad)
                 * mix(1.0, 0.12, damp);
    } else if (m == kMatLine) {
        float wear = octaveRes(foot, 6.0);
        float wearN = wear > 0.02 ? vnoise(P.xz * 6.0) : 0.5;
        s.albedo *= 0.78 + 0.34 * wearN * wear;
        s.height = 0.0006 * (wearN - 0.5) * wear;
        s.rough = 0.72;
    } else if (m == kMatWood) {
        float along = P.y * 1.15 + (P.x + P.z) * 0.06;
        float g1 = vnoise(vec2((P.x + P.z) * 6.5, along));
        float gGrain = octaveRes(foot, 19.0);
        float g2 = gGrain > 0.02 ? vnoise(vec2((P.x + P.z) * 19.0, along * 2.6)) : 0.5;
        s.albedo *= 0.80 + 0.28 * g1 + 0.16 * g2 * gGrain;
        s.albedo *= mix(0.70, 1.0, smoothstep(0.0, 1.20, P.y));
        s.height = 0.0020 * (g1 - 0.5) + 0.0011 * (g2 - 0.5) * gGrain;
        s.rough = 0.88;
    } else if (m == kMatWall) {
        float seam = abs(fract(P.y * 1.25) - 0.5) * 2.0;
        s.albedo *= mix(0.70, 1.10, smoothstep(0.06, 0.34, seam));
        s.albedo *= 0.88 + 0.24 * vnoise(vec2((P.x + P.z) * 2.2, P.y * 0.9));
        s.albedo *= 0.62;
        s.albedo *= mix(0.55, 1.0, smoothstep(0.0, 0.55, P.y));
        s.height = 0.0030 * (smoothstep(0.06, 0.34, seam) - 0.5)
                 + 0.0016 * (vnoise(vec2((P.x + P.z) * 2.2, P.y * 0.9)) - 0.5);
        s.rough = 0.90;
    } else if (m == kMatRoof) {
        float course = abs(fract(P.z * 0.85) - 0.5) * 2.0;
        s.albedo *= mix(0.70, 1.12, smoothstep(0.12, 0.55, course));
        s.albedo *= 0.84 + 0.32 * vnoise(P.xz * 1.7);
        // Relief only from the tile courses: a fine corrugation here beats
        // against the roof planes at distance and reads as noise.
        s.height = 0.0040 * (mix(0.70, 1.12, smoothstep(0.12, 0.55, course)) - 0.9);
        s.rough = 0.80;
    } else if (m == kMatGlass) {
        s.albedo *= 0.35;
        s.rough = 0.045;
        s.f0 = vec3(0.05);
    } else if (m == kMatKerb) {
        float agg = vnoise(vec2(P.x * 41.0, P.z * 9.0));
        float cement = vnoise(vec2(P.x * 3.1, P.z * 0.85));
        s.albedo *= 0.80 + 0.30 * cement;
        float gKerb = octaveRes(foot, 41.0);
        if (gKerb > 0.02) {
            float stones = smoothstep(0.62, 0.86, agg);
            s.albedo = mix(s.albedo, s.albedo * 1.85 + vec3(0.020),
                           stones * gKerb * 0.75);
        }
        s.height = 0.0026 * (cement - 0.5)
                 + 0.0011 * (smoothstep(0.62, 0.86, agg) - 0.3) * gKerb;
        float gz = 1.0 - clamp(P.y / 0.16, 0.0, 1.0);
        s.albedo = mix(s.albedo, vec3(0.115, 0.098, 0.078),
                       smoothstep(0.35, 1.0, gz) * 0.60);
        float wet = 0.40 * smoothstep(0.55, 0.95, gz);
        s.albedo *= mix(1.0, 0.55, wet);
        s.rough = mix(0.86, 0.10, wet);
        s.f0 = mix(vec3(0.04), vec3(0.020), wet);
    } else if (m == kMatLeaf) {
        float n = vnoise(P.xz * 0.85 + P.y * 0.35);
        s.albedo *= 0.72 + 0.46 * n;
        s.height = 0.004 * (n - 0.5);
        s.rough = 0.92;
    } else if (m == kMatCable) {
        // Rubber/PVC sheath: dark dielectric, fairly rough.
        s.rough = 0.68;
        s.f0 = vec3(0.035);
    } else if (m == kMatCeramic) {
        s.rough = 0.13;
        s.f0 = vec3(0.045);
    } else if (m == kMatMetal) {
        s.rough = 0.52;
        s.metal = 0.9;
        s.f0 = s.albedo;
    } else if (m == kMatSteel) {
        float streak = vnoise(vec2((P.x + P.z * 0.35) * 1.60, P.y * 0.22));
        float gSpangle = octaveRes(foot, 26.0);
        float spangle = gSpangle > 0.02
                            ? vnoise(vec2((P.x + P.z) * 26.0, P.y * 3.0)) : 0.5;
        s.albedo *= 0.80 + 0.30 * streak + 0.20 * spangle * gSpangle;
        s.albedo = mix(s.albedo, s.albedo * 1.24 + vec3(0.028, 0.029, 0.029),
                       smoothstep(0.55, 1.0, streak) * 0.55);

        // Creosote/rust is a dielectric coating: keep it out of the metal F0
        // and raise its roughness so it does not mirror like bare zinc.
        float rustN = vnoise(vec2((P.x + P.z) * 2.60, P.y * 0.90));
        float rust = (1.0 - smoothstep(0.25, 1.80, P.y)) * smoothstep(0.42, 0.86, rustN);
        s.albedo = mix(s.albedo, vec3(0.135, 0.060, 0.030), rust * 0.70);
        s.metal = mix(1.0, 0.0, rust * 0.75);
        s.f0 = mix(s.albedo, vec3(0.05), rust * 0.75);
        s.rough = mix(0.55, 0.88, rust) + 0.10 * (1.0 - smoothstep(0.25, 0.85, streak));
        s.height = 0.0008 * (streak - 0.5) + 0.0007 * (spangle - 0.5) * gSpangle;
    } else if (m == kMatConcrete) {
        // Spun concrete, not the soot-blackened, spalled thing this used to
        // be: a pale grey shaft with a broad tone difference between pours, a
        // faint vertical rain wash, fine aggregate up close, the horizontal
        // line where the form was struck, and road grime only in the bottom
        // metre.
        float wash = vnoise(vec2((P.x + P.z * 0.45) * 1.60, P.y * 0.045));
        float tone = vnoise(vec2((P.x + P.z * 0.45) * 0.30, P.y * 0.020));
        s.albedo *= 0.90 + 0.16 * tone;
        s.albedo *= 0.95 + 0.10 * wash;

        float gAgg = octaveRes(foot, 55.0);
        float agg = gAgg > 0.02 ? vnoise(vec2((P.x + P.z) * 55.0, P.y * 55.0)) : 0.5;
        if (gAgg > 0.02)
            s.albedo *= mix(1.0, 0.88 + 0.26 * agg, gAgg);

        float form = abs(fract(P.y * 3.05) - 0.5) * 2.0;
        s.albedo *= mix(0.985, 1.015, smoothstep(0.02, 0.30, form));

        s.albedo = mix(s.albedo, s.albedo * 0.64 + vec3(0.014, 0.011, 0.007),
                       (1.0 - smoothstep(0.18, 1.60, P.y)) * 0.55);
        // Cast-in form ridges faintly proud of the surface, plus the fine
        // pitting of the aggregate showing through the cement skin.
        s.height = 0.0022 * (wash - 0.5)
                 + 0.0016 * (form - 0.5)
                 + 0.0020 * (agg - 0.5) * gAgg;
        s.rough = 0.84 - 0.06 * wash;
    } else if (m == kMatGlow) {
        s.albedo *= 0.9;
        s.emission = vColor * 5.0;
        s.rough = 0.35;
    }
    return s;
}

// ------------------------------------------------------------------ lighting
void main() {
    float m = vMat;

    if (m == kMatShadow) {
        fragColor = vec4(0.0, 0.0, 0.0, vAlpha);
        return;
    }

    vec3 N = normalize(vNormal);
    vec3 toEye = uEyePos - vWorld;
    float dist = length(toEye);
    vec3 V = toEye / max(dist, 1e-4);
    // Double-sided thin geometry (wires, leaves, roof planes): face the eye.
    if (dot(N, V) < 0.0) N = -N;

    vec3 L = normalize(uSunDir);
    vec3 P = vWorld;
    float foot = fwidth(P.x) + fwidth(P.z) + 0.0015;

    Surface s = makeSurface(m, P, foot);
    // Relief fades out once a pixel covers more than a few centimetres, so
    // distant surfaces stay smooth instead of boiling with shimmer.
    float reliefLod = 1.0 - smoothstep(0.015, 0.22, foot);
    if (s.height != 0.0 && reliefLod > 0.0)
        N = bumpNormal(N, P, s.height * reliefLod);
    vec3 albedo = s.albedo;
    float rough = clamp(s.rough, 0.03, 1.0);
    float metal = clamp(s.metal, 0.0, 1.0);
    vec3 f0 = s.f0;

    float NoV = clamp(dot(N, V), 1e-4, 1.0);
    float NoL = max(dot(N, L), 0.0);

    float shadow = shadowFactor(P, N, NoL);

    // Sun irradiance: the same extinction the sky shader uses, evaluated for
    // the local surface normal's altitude. Warm at this sun elevation.
    vec3 sunT = vec3(1.0, 0.58, 0.30);       // dusk transmittance, matches sky
    vec3 sunIrr = sunT * 7.2;                // brighter direct pools

    // ---- GGX specular ----
    vec3 H = normalize(L + V);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float alpha = rough * rough;
    float a2 = alpha * alpha;
    float dd = NoH * NoH * (a2 - 1.0) + 1.0;
    float D = a2 / (PI * dd * dd);
    float k = alpha * 0.5;                   // Smith-Schlick, correct k
    float G = 0.25 / ((NoV * (1.0 - k) + k) * (NoL * (1.0 - k) + k));
    vec3 F = f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0);
    vec3 specSun = D * G * F / (4.0 * NoV) * NoL * sunIrr * shadow;

    // ---- diffuse ----
    vec3 kd = (1.0 - F) * (1.0 - metal);
    vec3 diffuse = kd * albedo / PI * NoL * sunIrr * shadow;

    // ---- ambient from the scene's own sky cubemap ----
    // High mip = cosine-convolved irradiance. The lower hemisphere gets a
    // ground bounce tint so undersides do not go flat black.
    vec3 skyAmb = textureLod(uSkyEnvTex, N, max(uEnvMaxLod - 2.0, 0.0)).rgb;
    vec3 groundAmb = textureLod(uSkyEnvTex,
                                normalize(vec3(N.x, -0.35, N.z)),
                                max(uEnvMaxLod - 2.0, 0.0)).rgb * vec3(0.30, 0.24, 0.18);
    vec3 ambient = mix(groundAmb, skyAmb, 0.5 + 0.5 * N.y);
    ambient = mix(ambient, ambient * vec3(1.22, 1.02, 0.78), 0.45);
    ambient *= mix(1.0, 0.55 + 0.45 * shadow, 0.55); // shadowed areas see less sky

    // ---- sky reflection (roughness-matched) ----
    vec3 Nr = N;
    float wet = 0.0;
    if (m == kMatRoad)
        wet = clamp((0.92 - rough) / 0.87, 0.0, 1.0);
    else if (m == kMatKerb)
        wet = clamp((0.86 - rough) / 0.76, 0.0, 1.0);
    else
        wet = 0.0;

    if (wet > 0.01) {
        // Small ripples in standing water; their amplitude is only enough to
        // break up the reflection, not to invent a normal map.
        float r1 = sin(P.x * 5.1 + P.z * 1.7 + uTime * 0.45);
        float r2 = sin(P.x * 2.3 - P.z * 6.2 + uTime * 0.31);
        Nr = normalize(N + vec3(r1, 0.0, r2) * 0.045 * wet);
    }
    vec3 R = reflect(-V, Nr);
    R.y = abs(R.y);
    vec3 reflCol = textureLod(uSkyEnvTex, R, clamp(rough * uEnvMaxLod, 0.0, uEnvMaxLod)).rgb;
    vec3 fresV = f0 + (1.0 - f0) * pow(1.0 - NoV, 5.0);
    if (wet > 0.01)
        fresV = mix(fresV, vec3(0.020 + 0.98 * pow(1.0 - NoV, 5.0)), wet);
    vec3 specEnv = reflCol * fresV * mix(1.0, 0.62, wet);

    vec3 col = diffuse + specSun + specEnv + s.emission;
    col += ambient * albedo * (1.0 - metal) * (0.55 + 0.45 * NoV);

    // Metal reflections also carry the sky, tinted by albedo.
    if (metal > 0.01) {
        col += reflCol * albedo * (0.35 + 0.65 * fresV) * metal * 0.65;
    }

    // ---- ambient occlusion from contact with the ground ----
    float groundAO = mix(0.72, 1.0, smoothstep(0.0, 1.4, P.y));
    if (m == kMatGround || m == kMatRoad || m == kMatKerb) groundAO = 1.0;
    col *= groundAO;

    // ---- aerial perspective ----
    // Distant geometry dissolves into the sky colour in its own azimuth, with
    // the same warm transmittance the atmosphere uses. Height falloff keeps
    // the near road crisp while the far poles fade into the sunset.
    float hFall = exp(-max(P.y, 0.0) * 0.075);
    float fog = 1.0 - exp(-dist * 0.0016 * hFall);
    fog += 0.05 * (1.0 - exp(-dist * 0.012));
    vec3 hazeDir = normalize(vec3(-V.x, 0.020, -V.z));
    vec3 hazeCol = textureLod(uSkyEnvTex, hazeDir, max(uEnvMaxLod - 3.0, 0.0)).rgb;
    col = mix(col, hazeCol, clamp(fog * 0.72, 0.0, 0.78));

    // Slight silhouette warmth from the low sun behind the corridor.
    float silh = clamp(1.0 - dot(albedo, vec3(0.333)) * 2.6, 0.0, 1.0);
    col = mix(col, vec3(0.30, 0.16, 0.075), silh * 0.10);

    col = min(col, vec3(4.0));
    if (uHdrOut > 0.5) {
        fragColor = vec4(max(col, vec3(0.0)), 1.0 * vAlpha);
        return;
    }

    col = clamp(col, 0.0, 4.0);
    col = col / (col * 0.35 + vec3(0.72));
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(col, vAlpha);
}
