#version 330 core
// Fragment shader for the sky dome: a dusk gradient plus two procedural cloud
// layers, captured once per frame into the environment cubemap so the sea
// below always reflects what is above.
//
// Layer 1 — high sheared cirrus/stratus: long thin bands, sampled
//           anisotropically so they stretch horizontally; they can cross the
//           sun disc.
// Layer 2 — broken cumulus masses: domain-warped fBm so they read as puffy,
//           lumpy clouds, lit warm on the sun-facing side and shaded cool on
//           their own shadow side (basic two-tone cloud shading).
//
// Palette follows the 3DMark2001 "Nature" dusk: deep blue-black zenith,
// warm orange horizon band around a setting sun, pink-to-gold cloud rims.

in vec3 vDir;

uniform sampler2D uNoiseTex;   // tileable RGBA fBm noise
uniform float uTime;
uniform float uTonemap;       // 1.0: on-screen pass (LDR out) / 0.0: HDR env cubemap
uniform vec3  uSunDir;
uniform vec3  uZenithColor;
uniform vec3  uMidColor;
uniform vec3  uHorizonColor;
uniform vec3  uSunColor;

out vec4 fragColor;

const float PI = 3.14159265359;

// 3-octave fBm with per-octave ADAPTIVE LOD. Each octave is sampled at the
// mip level where its frequency is properly band-limited for the current
// pass — sharp on the full-res screen dome, smooth on the smaller cubemap
// faces. 3 octaves is deliberate: with the layer frequencies below, a 4th
// octave lands at 3-5 px per cell on screen, which reads as noise grain —
// real dusk clouds have no content below ~9 px at this framing.
float fbm(vec2 p) {
    // spectrum deliberately biased low-frequency: 0.55/0.30/0.15 instead of
    // equal-half octaves. The 8-px octave keeps only 15% amplitude — enough
    // for soft ragged edges, not enough to read as grain/dashes.
    float sum = textureLod(uNoiseTex, p, clamp(log2(max(length(fwidth(p)), 1e-4)), 0.0, 6.0)).r * 0.55;
    p = p * 2.07 + vec2(0.37, 1.31);
    sum += textureLod(uNoiseTex, p, clamp(log2(max(length(fwidth(p)), 1e-4)), 0.0, 6.0)).r * 0.30;
    p = p * 2.07 + vec2(0.37, 1.31);
    sum += textureLod(uNoiseTex, p, clamp(log2(max(length(fwidth(p)), 1e-4)), 0.0, 6.0)).r * 0.15;
    return sum - 0.5;
}

// High cirrus / stratus: thin horizontal bands, strongest overhead, thinning
// toward the horizon. Returns density; light returns the sun-facing factor.
float cirrus(vec3 dir, out float light) {
    vec2 uv = vec2(atan(dir.z, dir.x) * (0.5 / PI) + 0.5, dir.y);

    float wind = uTime * 0.0045;
    // BIG SMOOTH BANKS: long wavelength in x (banks stretch across the sky),
    // modest in y. Single fBm — stacking streak layers doubles the effective
    // frequency and is what produced the dash/confetti look.
    vec2 q = vec2(uv.x * 1.8, uv.y * 3.2) + vec2(wind, 0.0);
    float n = fbm(q);

    // wide threshold ramp: merges the field into coherent banks
    float band = smoothstep(0.00, 0.50, n) * 0.85;

    // fade with altitude: thin veil near the horizon, denser overhead
    float fade = smoothstep(0.01, 0.14, dir.y) * (0.35 + 0.65 * smoothstep(0.0, 0.45, dir.y));
    float density = clamp(band, 0.0, 1.0) * fade;

    // sun-facing thin cloud lights up warm; thick stays dark
    float sunAmount = max(dot(dir, normalize(uSunDir)), 0.0);
    light = pow(sunAmount, 3.0) * 0.9 + pow(sunAmount, 24.0) * 1.1;
    return density;
}

// Broken cumulus masses: domain-warped fBm so the shapes get the lumpy,
// cauliflower edges real cumulus has, instead of smooth noise blobs.
// Returns the coverage density; light carries the sun-facing factor and rim
// the "thin edge catching light" factor used for the edge-lit look.
float cumulus(vec3 dir, out float light, out float rim) {
    vec2 uv = vec2(atan(dir.z, dir.x) * (0.5 / PI) + 0.5, dir.y);

    float wind = uTime * 0.0032;
    // big cumulus masses: low frequency + domain warp does the organic shaping
    vec2 q = vec2(uv.x * 3.2, uv.y * 3.6) + vec2(wind, wind * 0.25);
    // domain warp: two nested noise lookups bend the base field (adaptive LOD
    // same as fbm — these lookups stride multiple texels per pixel too)
    vec2 wq = q * 0.5 + 3.7;
    float wlod = clamp(log2(max(length(fwidth(wq)), 1e-4)), 0.0, 6.0);
    vec2 warp = vec2(textureLod(uNoiseTex, wq, wlod).r,
                     textureLod(uNoiseTex, wq + vec2(5.4, 0.0), wlod).g) - 0.5;
    q += warp * 0.65;

    float n = fbm(q); // puffy shapes

    // large broken masses with soft puffy edges
    float mass = smoothstep(0.02, 0.55, n);
    // bright rim light in a band at the mass edges — cloud cores stay dark,
    // exactly like backlit real-dusk cumulus
    rim = smoothstep(0.02, 0.16, n) * (1.0 - smoothstep(0.20, 0.46, n));

    // cumulus needs altitude: nothing at the horizon line
    float fade = smoothstep(0.06, 0.22, dir.y);
    float density = clamp(mass * 0.9 + rim * 0.5, 0.0, 1.0) * fade;

    // sun-facing faces catch warm low light; everything else stays dark
    float sunAmount = max(dot(dir, normalize(uSunDir)), 0.0);
    light = 0.06 + 0.94 * pow(sunAmount, 3.0);
    return density;
}

void main() {
    vec3 dir = normalize(vDir);
    float h = clamp(dir.y, 0.0, 1.0);
    vec3 sd = normalize(uSunDir);

    // Sunset gradient: deep blue-black zenith through mauve, into the warm
    // horizon band. Multiplied by a sun-direction falloff so the anti-sun sky
    // stays dark like the reference shot.
    vec3 sky = mix(uHorizonColor, uMidColor, smoothstep(0.0, 0.14, h));
    sky = mix(sky, uZenithColor, smoothstep(0.10, 0.38, h));

    float sunAmount = max(dot(dir, sd), 0.0);
    sky *= mix(0.22, 1.0, pow(sunAmount, 4.0));   // steep: dark sky away from the sun

    // warm horizon glow hugging the horizon around the sun azimuth
    float glowMask = pow(sunAmount, 14.0) * 0.55 + pow(sunAmount, 35.0) * 0.80;
    glowMask *= 1.0 - smoothstep(0.05, 0.55, h) * 0.85;  // strongest at the horizon
    sky = mix(sky, vec3(1.7, 0.66, 0.28), clamp(glowMask, 0.0, 0.65));

    // broader soft gold field above the horizon glow
    float goldMask = pow(sunAmount, 120.0) * 0.9 * (1.0 - smoothstep(0.0, 0.70, h) * 0.6);
    sky = mix(sky, vec3(2.6, 1.3, 0.62), clamp(goldMask, 0.0, 0.85));

    // hot core just behind the clouds where the sun sits
    float coreMask = pow(sunAmount, 250.0) * 0.95;
    sky = mix(sky, vec3(6.0, 2.8, 1.2), clamp(coreMask, 0.0, 0.97));

    // ---- clouds composite over the glow ----
    float cirLight, cumLight, cumRim;
    float cirD = cirrus(dir, cirLight);
    float cumD = cumulus(dir, cumLight, cumRim);

    // cirrus: thin veils, lit gold-pink where they face the sun
    vec3 cirCol = vec3(0.080, 0.065, 0.100)               // unlit band colour
                + vec3(2.1, 1.15, 0.68) * clamp(cirLight, 0.0, 1.0);
    sky = mix(sky, cirCol, clamp(cirD * 0.85, 0.0, 0.9));

    // cumulus: edge-lit like real dusk clouds — dark slate cores, blazing
    // gold-pink rims on the sun side, warm sunlit faces, faint cool fringes
    // away from the sun
    vec3 cumBase = vec3(0.075, 0.065, 0.100);
    vec3 rimCol = mix(vec3(0.14, 0.13, 0.18),             // away-from-sun fringes
                      vec3(2.3, 1.30, 0.85),              // near-sun rims
                      clamp(cumLight, 0.0, 1.0));
    vec3 faceCol = vec3(0.85, 0.50, 0.34) * clamp(cumLight, 0.0, 1.0);
    vec3 cumCol = cumBase + rimCol * clamp(cumRim, 0.0, 1.0)
                + faceCol * clamp(cumD, 0.0, 1.0) * 0.6;
    sky = mix(sky, cumCol, clamp(cumD * 0.95, 0.0, 0.94));

    // big soft disc: the sun is well above the horizon, so it reads as a
    // compact bright ball with a warm halo. Drawn last and attenuated by
    // cloud cover so thin bands can veil it without erasing it.
    float cover = max(cirD * 0.85, cumD * 0.95);
    float disc = smoothstep(0.9975, 0.9990, sunAmount) * (1.0 - 0.80 * clamp(cover, 0.0, 1.0));
    float halo = pow(sunAmount, 600.0) * 0.7 * (1.0 - 0.5 * clamp(cover, 0.0, 1.0));
    sky = mix(sky, vec3(9.0, 6.2, 3.6), clamp(disc + halo, 0.0, 1.0));

    // Env-cubemap pass keeps HDR values (the sea shader tone maps after adding
    // glitter). The on-screen dome pass tone maps + gammas right here so the
    // visible sky matches what the water reflects.
    vec3 outCol = mix(sky, pow(sky / (sky + vec3(1.0)), vec3(1.0 / 2.2)), uTonemap);
    fragColor = vec4(outCol, 1.0);
}
