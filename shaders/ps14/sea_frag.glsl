#version 330 core
// Fragment shader for the sea surface.
//
// Structure intentionally mirrors the phases of a shader_model 1.4 pixel
// shader (as used by the 3DMark2001 SE "Nature" ocean):
//   1) addressing  : sample ripple gradient texture -> perturbation vector
//   2) dependent read: perturbed coords -> environment reflection lookup
//   3) blend/address: add sun glitter, blend with deep-water color
// Six texture fetches per phase is the PS1.4 budget; this uses far fewer.
//
// Realism layer (this pass): on top of the displaced swell banks the shading
// adds two octaves of ANALYTIC detail wavelets (extra normals, tiny amplitude,
// no vertex cost), slope-gated crest foam (it appears where waves actually
// face the light and break — not as a uniform tile), sun-tinted glitter, and
// a sky-tinted horizon sheen so the far sea is a mirror of the sky band above
// it rather than dark water with a haze knob.

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

out vec4 fragColor;

const float PI = 3.14159265359;

// NORMAL FIELD: smooth sines (finite differences of this drive the lighting).
// Scale/speed of each line must match the DISPLACEMENT field in sea_vert.glsl
// so crest banks light up where the geometry actually rises. The displacement
// shader additionally sharpens crests and adds two long swells; that asymmetry
// is deliberate (sharp banks, smooth lighting).
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

// Three octaves of small analytic wavelets: extra normal detail that would
// be wasted (and aliased) as vertex displacement, but sells micro-chop up
// close. Frequencies chosen so screen-space wavelength stays > ~4px at typical
// orbit distances — content finer than that reads as noise on real GPUs.
void detailNormals(vec2 p, float t, float dist, inout vec2 grad) {
    float fade = exp(-dist * 0.004);   // micro-chop is a NEAR-camera feature
    if (fade < 0.02) return;
    float w1 = sin(dot(p, vec2(0.86, 0.51)) * 1.15 + t * 5.10);
    float w2 = sin(dot(p, vec2(-0.44, 0.90)) * 2.30 + t * 6.80);
    float w3 = sin(dot(p, vec2(0.22, -0.97)) * 4.40 + t * 8.60);
    grad += vec2(0.86, 0.51) * 1.15 * w1 * 0.045;
    grad += vec2(-0.44, 0.90) * 2.30 * w2 * 0.022;
    grad += vec2(0.22, -0.97) * 4.40 * w3 * 0.010;
}

void main() {
    // ---- analytic wave normal (finite differences of the wave field) ----
    float dist = length(vWorld.xz - uEyePos.xz);
    float e = 0.35;
    float hC = waveHeight(vWorld.xz, uTime);
    float hX = waveHeight(vWorld.xz + vec2(e, 0.0), uTime);
    float hZ = waveHeight(vWorld.xz + vec2(0.0, e), uTime);
    vec2 grad = vec2(-(hX - hC) / e, -(hZ - hC) / e);
    detailNormals(vWorld.xz, uTime, dist, grad);
    vec3 N = normalize(vec3(grad.x, 1.0, grad.y));

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
    // Reflections stretch vertically (the classic flattened-reflection trick):
    // grazing rays would otherwise hug the bright horizon band and light the
    // whole sea up. Biasing the ray up makes off-sun water reflect the dark
    // upper sky while the sun glitter path stays put (it is a separate term).
    R.y = abs(R.y) * 0.30 + 0.45;
    R = normalize(R);
    // azimuthal smear toward the sun: only rays near the SUN azimuth keep
    // crisp reflections; off-path rays mirror-blend toward the dark upper sky
    // so the glow column stays narrow and the sides read deep blue/purple
    vec3 L = normalize(uSunDir);
    vec2 sunXZ = normalize(L.xz);
    vec2 dirXZ = normalize(vWorld.xz - uEyePos.xz + vec2(1e-4));
    float sunAlign = max(dot(dirXZ, sunXZ), 0.0);
    vec3 Rdark = normalize(vec3(R.x, abs(R.y) * 1.8 + 0.62, R.z)); // steep: upper sky
    R = normalize(mix(Rdark, R, pow(sunAlign, 6.0)));

    // Roughness-matched reflection LOD: the sun disc occupies a handful of
    // cubemap texels, and sampling them at LOD 0 mirrors as small SQUARE
    // patches on the water. Wave facets are rough at every distance, so the
    // reflection blurs with range — which also smears the sun into a soft
    // vertical glow (real water behaviour) instead of texel squares.
    float reflDist = dist;
    vec3 reflColor = textureLod(uSkyEnvTex, R, clamp(1.5 + reflDist * 0.0012, 1.0, 5.0)).rgb;

    // ---- fresnel: sea is a mirror at grazing angles, glass straight down ----
    float NdV = max(dot(N, V), 0.0);
    float fresnel = 0.022 + 0.978 * pow(1.0 - NdV, 5.0);

    // ---- water body: near-black purple deep, warmed by the sky band ----
    vec3 body = uWaterColor * 0.55 + uHorizonColor * 0.03;

    // directional sun lighting on the wave slopes: faces tilted toward the
    // low sun glow warm, backslopes fall to near-black — this is what makes
    // the sea read as lit by the same sun as the sky instead of pasted on.
    // Prefaced by an azimuth gate so the WARM slope light lives inside the
    // sun path; off-path water stays deep blue/purple.
    float sunDiffuse = max(dot(N, L), 0.0);
    float warmGate = pow(sunAlign, 3.0);
    body *= 0.50 + 0.55 * sunDiffuse * warmGate + 0.18 * sunDiffuse; // slope shading
    body *= mix(0.62, 1.0, warmGate);                                // dark off-path body
    body += vec3(1.05, 0.42, 0.20) * pow(sunDiffuse, 3.0) * warmGate * 0.42; // warm slopes in the path

    // ---- slope-gated crest foam ----
    // Foam only where the shading says waves actually BREAK: a height band
    // AND the slope facing the sun/light AND some ripple chaos — the noise
    // tile alone would foam uniformly everywhere, which reads fake.
    float crest = smoothstep(0.55, 1.25, hC);
    float slopeFacing = max(dot(N, L), 0.0) * warmGate + 0.15;
    float chaos = texture(uRippleTex, vUV * 190.0 + vec2(uTime * 0.011, -uTime * 0.007)).g;
    float foam = texture(uFoamTex, vUV * 23.0 + vec2(uTime * 0.010, 0.0)).r;
    foam *= texture(uFoamTex, vUV * 41.0 - vec2(0.0, uTime * 0.013)).g;
    foam *= crest * slopeFacing * (0.35 + 0.65 * chaos);
    body += vec3(0.55, 0.48, 0.58) * foam * 0.55; // dim warm-gray foam

    // subsurface glow against the light: thin wave crests shine turquoise
    float towardSun = max(dot(normalize(vec3(-V.x, 0.0, -V.z)), L), 0.0);
    body += vec3(0.05, 0.20, 0.16) * towardSun * crest * 0.8;

    vec3 color = mix(body, reflColor, fresnel);

    // ---- horizon sheen: the far sea is a MIRROR of the sky band above it
    // (grazing fresnel -> nearly pure reflection up there), so by geometry
    // the water edge converges into the sky with no haze knob needed.
    vec2 dxdz = vWorld.xz - uEyePos.xz;
    vec3 skyAtHorizon = texture(uSkyEnvTex,
        normalize(vec3(dxdz.x, 0.012, dxdz.y))).rgb;
    float horizonMix = smoothstep(1500.0, 1900.0, dist);
    color = mix(color, skyAtHorizon, horizonMix * 0.85);

    // ---- aerial haze: near water stays dark and readable, while the far
    // sea melts into the horizon. The haze target is the ACTUAL sky colour
    // at this fragment's azimuth — sampled from the env cubemap just above
    // the horizon line — so the far edge of the patch converges into the sky
    // band above it (bright glow on the sun side, dark maroon away from it).
    float haze = 1.0 - exp(-dist * 0.00075);
    color = mix(color, skyAtHorizon, haze * (0.30 + 0.70 * haze));

    // ---- phase 3: address + blend - sun glitter path ----
    // tight sparkle core + broad soft sheen, gated to the sun's azimuth column
    // so the glow stays a NARROW path down the middle with dark water either
    // side (the reference look) instead of a horizon-wide shine.
    // Tint: real dusk glitter is ORANGE — the path is compressed light from
    // the disc itself, so it inherits the sun colour rather than white.
    vec3 H = normalize(L + V);
    float NdH = max(dot(N, H), 0.0);
    float pathGate = pow(sunAlign, 6.0) * 0.90 + 0.10;
    float glint = pow(NdH, 520.0) * 6.0;              // pinpoint sparkles
    float glintMid = pow(NdH, 90.0) * 0.55;           // mid falloff keeps it grainy
    float glintWide = pow(NdH, 14.0) * 0.22;          // soft sheen around the path
    color += vec3(1.0, 0.56, 0.24) * (glint + glintMid + glintWide * pathGate)
             * (0.25 + max(L.y, 0.0) * 1.2) * pathGate;

    // HDR tone map + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0);
}
