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
uniform vec3  uSunDir;
uniform vec3  uZenithColor;
uniform vec3  uMidColor;
uniform vec3  uHorizonColor;
uniform vec3  uSunColor;

out vec4 fragColor;

const float PI = 3.14159265359;

// 4-octave fBm helper: octaves double in frequency (proper detail cascade).
// Sampled with textureLod 0: the big coordinate multipliers would otherwise
// push the implicit derivatives into high mip levels and erase the detail
// octaves (flat, cloudless sky).
float fbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; i++) {
        sum += textureLod(uNoiseTex, p, 0.0).r * amp;
        p = p * 2.07 + vec2(0.37, 1.31);
        amp *= 0.5;
    }
    return sum / 0.9375 - 0.5; // normalise 4-octave sum (1.0 - 0.5^4)
}

// High cirrus / stratus: thin horizontal bands, strongest overhead, thinning
// toward the horizon. Returns density; light returns the sun-facing factor.
float cirrus(vec3 dir, out float light) {
    vec2 uv = vec2(atan(dir.z, dir.x) * (0.5 / PI) + 0.5, dir.y);

    float wind = uTime * 0.0045;
    vec2 q = vec2(uv.x * 2.6, uv.y * 14.0) + vec2(wind, 0.0); // long soft horizontal bands
    float n = fbm(q);

    // banded coverage: a couple of overlapping streak layers, soft thresholds
    // (hard smoothsteps at high frequency read as noise on real hardware)
    float band = smoothstep(0.02, 0.38, n) * 0.80
               + smoothstep(0.10, 0.55, fbm(vec2(uv.x * 3.6, uv.y * 20.0) - vec2(wind * 0.6, 3.7))) * 0.50;

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
    vec2 q = vec2(uv.x * 6.0, uv.y * 8.5) + vec2(wind, wind * 0.25);
    // domain warp: two nested noise lookups bend the base field
    vec2 warp = vec2(textureLod(uNoiseTex, q * 0.5 + 3.7, 0.0).r,
                     textureLod(uNoiseTex, q * 0.5 + 9.1, 0.0).g) - 0.5;
    q += warp * 0.65;

    float n = fbm(q); // puffy shapes

    // large broken masses with soft organic edges
    float mass = smoothstep(0.06, 0.42, n);
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

    // HDR-ish output for the env map (tone mapping happens in the sea shader)
    fragColor = vec4(sky, 1.0);
}
