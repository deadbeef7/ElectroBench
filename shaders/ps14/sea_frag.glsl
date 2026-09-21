#version 330 core
// Fragment shader for the sea surface.
//
// Structure intentionally mirrors the phases of a shader_model 1.4 pixel
// shader (as used by the 3DMark2001 SE "Nature" ocean):
//   1) addressing  : sample ripple gradient texture -> perturbation vector
//   2) dependent read: perturbed coords -> environment reflection lookup
//   3) blend/address: add sun glitter, blend with deep-water color
// Six texture fetches per phase is the PS1.4 budget; this uses far fewer.

in vec3 vWorld;
in vec2 vUV;

uniform sampler2D uRippleTex;    // two-channel ripple gradient (RG16F)
uniform samplerCube uSkyEnvTex;  // sky environment cubemap (RGBA16F)
uniform sampler2D uFoamTex;      // tileable foam / caustic detail
uniform float uTime;
uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform vec3  uHorizonColor;
uniform vec3  uWaterColor;

out vec4 fragColor;

const float PI = 3.14159265359;

// Must stay in sync with waveHeight() in sea_vert.glsl
// Chop everywhere: short wavelengths so several crests are always on screen.
float waveHeight(vec2 p, float t) {
    float h = 0.0;
    h += sin(dot(p, vec2( 0.98,  0.20)) * 0.170 + t * 1.30) * 0.68;
    h += sin(dot(p, vec2(-0.64,  0.77)) * 0.240 + t * 1.60) * 0.46;
    h += sin(dot(p, vec2( 0.36, -0.93)) * 0.380 + t * 2.10) * 0.34;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.22;
    h += sin(dot(p, vec2( 0.59,  0.81)) * 0.860 + t * 3.40) * 0.14;
    h += sin(dot(p, vec2(-0.20,  0.98)) * 1.450 + t * 4.40) * 0.08;
    return h;
}

void main() {
    // ---- analytic wave normal (finite differences of the wave field) ----
    float e = 0.35;
    float hC = waveHeight(vWorld.xz, uTime);
    float hX = waveHeight(vWorld.xz + vec2(e, 0.0), uTime);
    float hZ = waveHeight(vWorld.xz + vec2(0.0, e), uTime);
    vec3 N = normalize(vec3(-(hX - hC) / e, 1.0, -(hZ - hC) / e));

    // ---- phase 1: addressing - ripple normal map, scrolled over the surface
    float dist = length(vWorld.xz - uEyePos.xz);
    float rippleFade = exp(-dist * 0.0018);           // ripples die out far away
    vec2 uvA = vUV * 40.0 + vec2(uTime * 0.0040, uTime * 0.0031);
    vec2 uvB = vUV * 97.0 - vec2(uTime * 0.0026, uTime * 0.0042);
    vec2 ripA = texture(uRippleTex, uvA).rg;
    vec2 ripB = texture(uRippleTex, uvB).rg;
    vec2 pert = (ripA + ripB) * 0.5 * rippleFade;

    // ---- phase 2: dependent read - perturbed reflection of the sky ----
    vec3 V = normalize(uEyePos - vWorld);             // towards the eye
    vec3 R = reflect(-V, N);
    // add ripple perturbation in tangent space, THEN clamp: clamping before the
    // perturbation let downward-perturbed rays sample the never-rendered -Y
    // cubemap face (black band at the horizon + black spots on the water).
    R = normalize(R + vec3(pert.x, 0.0, pert.y) * 2.2);
    R.y = abs(R.y);                                   // never look below the horizon

    vec3 reflColor = texture(uSkyEnvTex, R).rgb;

    // ---- fresnel: sea is a mirror at grazing angles, glass straight down ----
    float NdV = max(dot(N, V), 0.0);
    float fresnel = 0.022 + 0.978 * pow(1.0 - NdV, 5.0);

    // ---- water body: deep colour + foam detail in the crests ----
    vec3 body = uWaterColor + uHorizonColor * 0.06;
    float crest = smoothstep(0.55, 1.25, hC);
    float foam = texture(uFoamTex, vUV * 23.0 + vec2(uTime * 0.010, 0.0)).r;
    foam *= texture(uFoamTex, vUV * 41.0 - vec2(0.0, uTime * 0.013)).g;
    body += vec3(0.75) * crest * foam * 0.85;

    // subsurface glow against the light: thin wave crests shine turquoise
    vec3 L = normalize(uSunDir);
    float towardSun = max(dot(normalize(vec3(-V.x, 0.0, -V.z)), L), 0.0);
    body += vec3(0.05, 0.20, 0.16) * towardSun * crest * 0.8;

    vec3 color = mix(body, reflColor, fresnel);

    // ---- phase 3: address + blend - sun glitter path ----
    vec3 H = normalize(L + V);
    float glint = pow(max(dot(N, H), 0.0), 300.0);
    float glintWide = pow(max(dot(N, H), 0.0), 24.0) * 0.22;
    color += vec3(1.0, 0.87, 0.62) * (glint * 4.5 + glintWide) * max(L.y, 0.0) * 1.4;

    // gentle aerial haze so distant water melts into the sky but the dark
    // water and glitter stay readable most of the way out (dark reference)
    float haze = 1.0 - exp(-dist * 0.00052);
    color = mix(color, uHorizonColor, haze * 0.30);

    // HDR tone map + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0);
}
