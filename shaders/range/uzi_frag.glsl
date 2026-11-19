#version 330 core

// Scene 1 -- the UZI material.
//
// The whole point of this scene is that the guns are the only assets in it:
// all six maps of the model are used for what they are -- base colour,
// tangent-space normal, metallic, height (parallax), ambient occlusion and
// roughness -- under a moving three-light rig, so the surface detail is what
// the frame is actually showing.

in vec3 vWorldPos;
in vec3 vNormal;
in vec4 vTangent;
in vec2 vUV;

out vec4 fragColor;

uniform sampler2D uBaseColor;   // sRGB8, hardware-linearised
uniform sampler2D uNormalMap;
uniform sampler2D uMetallicMap;
uniform sampler2D uHeightMap;
uniform sampler2D uAOMap;
uniform sampler2D uRoughnessMap;
uniform sampler2D uShadowMap;
uniform mat4 uLightVP;
uniform vec2 uShadowTexel;

uniform vec3 uEye;
uniform vec3 uKeyDir;       // unit vector from surface towards the key light
uniform vec3 uKeyColor;
uniform vec3 uRimPos;
uniform vec3 uRimColor;
uniform vec3 uFillPos;
uniform vec3 uFillColor;
uniform vec3 uSkyAmb;
uniform vec3 uGroundAmb;
uniform vec3 uGlowAmb;
uniform float uParallax;
uniform float uTime;
uniform int uReflect;

const float kPi = 3.14159265;

// ------------------------------------------------------------------- BxDF
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

// Cook-Torrance direct lighting for one light. `rad` already carries the
// light's colour and its distance falloff.
vec3 BxDF(vec3 N, vec3 V, vec3 L, vec3 rad, vec3 diff, float rough, vec3 f0) {
    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float NoV = max(dot(N, V), 1e-4);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);
    float a = max(rough * rough, 0.0016);
    vec3 F = F_Schlick(f0, VoH);
    // clamp the GGX lobe: at a distance the normal map is minified far below
    // its texel size and unclamped NDF peaks turn into crawling fireflies
    vec3 spec = min(vec3(D_GGX(NoH, a) * VisSmith(NoV, NoL, a)), vec3(1.6)) * F;
    vec3 kd = (1.0 - F) * diff / kPi;
    return (kd + spec) * rad * NoL;
}

vec3 PointLight(vec3 N, vec3 V, vec3 pos, vec3 colour, vec3 diff, float rough, vec3 f0) {
    vec3 d = pos - vWorldPos;
    float dist = length(d);
    vec3 L = d / max(dist, 1e-4);
    float atten = 1.0 / (1.0 + 0.30 * dist + 0.24 * dist * dist);
    return BxDF(N, V, L, colour * atten, diff, rough, f0);
}

// Fake studio environment for the ambient specular: a dark hall with one
// bright strip overhead and a warm wash off the lit floor. Cheap, but it keeps
// the polished steel from going flat black between the direct highlights.
vec3 StudioEnv(vec3 R, float rough) {
    float up = clamp(R.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 col = mix(uGroundAmb * 0.75, uSkyAmb, smoothstep(0.34, 0.96, up));
    float sharp = mix(7.0, 2.0, rough);
    float strip = exp(-pow((R.y - 0.74) * sharp, 2.0)) * 0.35;
    float horizon = exp(-pow((R.y - 0.02) * sharp * 1.4, 2.0)) * 0.20;
    return col * 0.45 + uKeyColor * (strip + horizon) * 0.04 +
           uGlowAmb * horizon * 0.25;
}

float ShadowFactor(vec3 N) {
    vec4 lp = uLightVP * vec4(vWorldPos, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float ndl = clamp(dot(N, uKeyDir), 0.0, 1.0);
    float bias = max(0.0020 * (1.0 - ndl), 0.0006);
    float sum = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float d = texture(uShadowMap, p.xy + vec2(x, y) * uShadowTexel * 1.15).r;
            sum += (p.z - bias > d) ? 0.0 : 1.0;
        }
    }
    return sum / 9.0;
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent.xyz - N * dot(N, vTangent.xyz));
    vec3 B = cross(N, T) * vTangent.w;
    vec3 V = normalize(uEye - vWorldPos);
    float NoV = max(dot(N, V), 1e-4);

    // ---- parallax: the height map displaces the UVs along the view ray, so
    // the stamped receiver's slots and the magazine ridges have real depth
    // instead of reading as a flat print.
    vec2 uv = vUV;
    if (uReflect == 0) {
        vec3 Vt = vec3(dot(V, T), dot(V, B), dot(V, N));
        float h = texture(uHeightMap, uv).r - 0.5;
        uv -= Vt.xy * (h * uParallax) / max(abs(Vt.z) + 0.7, 0.7);
    }

    // The base colour is a very dark parkerised steel (mean ≈ 0.02 linear);
    // lift it so the shape and the normal/roughness detail are readable in a
    // night scene instead of collapsing to a silhouette.
    vec3 albedo = texture(uBaseColor, uv).rgb * 2.1 + vec3(0.014);
    float metal = clamp(texture(uMetallicMap, uv).r * 0.85, 0.0, 1.0);
    float rough = clamp(texture(uRoughnessMap, uv).r * 0.80 + 0.24, 0.30, 0.95);
    // widen the lobe with distance so far rings do not sparkle
    float camDist = length(uEye - vWorldPos);
    rough = mix(rough, 0.58, smoothstep(2.5, 9.0, camDist));
    float ao = clamp(texture(uAOMap, uv).r, 0.0, 1.0);

    // Blender exports OpenGL-style tangents (+Y up); green is used as-is.
    vec3 nTex = texture(uNormalMap, uv).rgb * 2.0 - 1.0;
    // keep the stamped detail, but a full-strength 2K normal map at close range
    // breaks the receiver up into hard shards of light
    nTex.xy *= mix(0.28, 0.70, NoV);
    N = normalize(T * nTex.x + B * nTex.y + N * nTex.z);
    NoV = max(dot(N, V), 1e-4);

    vec3 f0 = mix(vec3(0.045), albedo, metal);
    vec3 diff = albedo * (1.0 - metal);

    vec3 col = vec3(0.0);
    float shadow = ShadowFactor(N);
    col += BxDF(N, V, uKeyDir, uKeyColor * shadow, diff, rough, f0);
    col += PointLight(N, V, uRimPos, uRimColor, diff, rough, f0);
    col += PointLight(N, V, uFillPos, uFillColor, diff, rough, f0);

    // Ambient: hangar sky from above, warm slab bounce from below, plus the
    // amber glow that the wall washers throw onto everything.
    vec3 amb = mix(uGroundAmb, uSkyAmb, 0.5 + 0.5 * N.y) * ao;
    col += diff * amb * 1.2;
    // metals keep their body colour under the room's ambient instead of going
    // black between the direct highlights
    col += albedo * amb * metal * 1.4;
    vec3 R = reflect(-V, N);
    col += StudioEnv(R, rough) * F_Schlick(f0, NoV) * ao * 0.55;

    // A touch of grazing sheen keeps the parkerised edges from disappearing at
    // the silhouettes when the key is behind the gun.
    col += uGlowAmb * pow(1.0 - NoV, 4.0) * 0.10 * (1.0 - rough);

    if (uReflect == 1) {
        // The planar pass writes straight into a colour target that the slab
        // then reads back; keep it a little darker than the direct view.
        col *= 0.62;
    }
    fragColor = vec4(max(col, vec3(0.0)), 1.0);
}
