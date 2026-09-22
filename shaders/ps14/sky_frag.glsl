#version 330 core
// Fragment shader for the sky dome: gradient zenith -> horizon plus two layers
// of fBm clouds. The dome is captured once per frame into the environment
// cubemap, so the sea below always reflects what is above.
//
// Styled after the moody dusk reference: the sky is nearly black away from the
// sun, a broad salmon field surrounds the sun, and heavy horizontal stratus
// streaks cross it — dark charcoal where unlit, pale pink where the sun catches
// their underside.

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

// One octave set of drifting domain-warped clouds on the upper hemisphere.
// The noise is sampled anisotropically (compressed vertically) so the masses
// shear into long horizontal stratus streaks like the reference.
// Returns the lit cloud color; outDensity carries the coverage so main() can
// blend it over the sky gradient.
vec3 clouds(vec3 dir, out float outDensity) {
    // cylindrical unwrap; poles are never visible at this camera height
    vec2 uv = vec2(atan(dir.z, dir.x) * (0.5 / PI) + 0.5, dir.y);

    // streaks persist down to the horizon (at reduced opacity) so thin bands
    // veil the sun disc itself, like the reference
    float fade = 0.25 + 0.75 * smoothstep(0.0, 0.15, dir.y);
    if (fade <= 0.001) return vec3(0.0);

    float wind = uTime * 0.0055;
    float t1 = 0.0, t2 = 0.0;
    float amp = 0.5;
    vec2 flow1 = vec2(wind, wind * 0.30);
    vec2 flow2 = vec2(-wind * 0.6, wind * 0.40);

    for (int i = 0; i < 5; i++) {
        // stretch: x frequency stays high (texture along the streak),
        // y frequency is compressed (slow variation with height) -> bands
        vec2 s1 = vec2(3.4, 1.05) * amp * 2.0;
        vec2 s2 = vec2(6.3, 1.90) * amp * 2.0;
        vec2 p1 = uv * s1 + flow1 * amp * 2.0;
        vec2 p2 = uv * s2 + flow2 * amp * 2.0 + 17.0;
        p1.y *= 0.55;
        p2.y *= 0.55;
        t1 += texture(uNoiseTex, p1).r * amp;
        t2 += texture(uNoiseTex, p2).g * amp;
        amp *= 0.5;
    }
    t1 = t1 / 0.96875 - 0.5;   // normalise 5-octave sum (1.0 - 0.5^5)
    t2 = t2 / 0.96875 - 0.5;

    // heavy overcast: large solid masses plus shredded streaky wisps
    float cover = smoothstep(0.02, 0.52, t1 * 0.95 + t2 * 0.40 + 0.10);
    float wisp  = smoothstep(0.26, 0.72, t2 + t1 * 0.5) * 0.75;
    float density = clamp(cover * 0.9 + wisp, 0.0, 1.0) * fade;
    outDensity = density;

    // lighting: near the sun the masses go dark (backlit silhouettes) while
    // their thin edges and wisps catch pale pink rim light — like the reference
    float sunAmount = max(dot(normalize(dir), normalize(uSunDir)), 0.0);
    float shine = pow(sunAmount, 5.0) * 0.85 + pow(sunAmount, 18.0) * 0.9;
    vec3 base = vec3(0.085, 0.070, 0.095);            // charcoal-mauve shadow side
    vec3 lit  = vec3(1.55, 0.95, 0.78);               // salmon-pink rim light
    float edgeBand = smoothstep(0.05, 0.30, density) * (1.0 - smoothstep(0.45, 0.85, density));
    vec3 c = base * density * (1.0 - 0.55 * shine) + lit * shine * (0.20 * density + 0.85 * edgeBand);
    return c;
}

void main() {
    vec3 dir = normalize(vDir);
    float h = clamp(dir.y, 0.0, 1.0);

    // Sunset gradient: warm dark band at the horizon, deep mauve mid-sky.
    vec3 sky = mix(uHorizonColor, uMidColor, smoothstep(0.0, 0.10, h));
    sky = mix(sky, uZenithColor, smoothstep(0.08, 0.30, h));

    // Fade the whole gradient toward black away from the sun: on the anti-sun
    // side the dusk sky collapses into near-black like the reference.
    vec3 sd = normalize(uSunDir);
    float sunAmount = max(dot(dir, sd), 0.0);
    sky *= mix(0.30, 1.0, pow(sunAmount, 0.35));

    // broad salmon field around the sun reaching well above the horizon,
    // then a hotter core just behind the clouds
    float glowMask = clamp(pow(sunAmount, 14.0) * 0.80 + pow(sunAmount, 34.0) * 0.85, 0.0, 0.97);
    glowMask *= 1.0 - smoothstep(0.06, 0.85, h);      // glow fades overhead, not at the horizon
    sky = mix(sky, vec3(2.6, 0.95, 0.50), glowMask);  // salmon-orange field

    float coreMask = clamp(pow(sunAmount, 110.0) * 0.9, 0.0, 0.95);
    sky = mix(sky, vec3(6.5, 2.6, 1.15), coreMask);   // hot center behind clouds

    // big bright disc sitting just above the horizon
    float disc = smoothstep(0.9950, 0.9982, sunAmount);
    sky = mix(sky, vec3(10.0, 6.0, 3.0), disc);       // -> warm white core, soft edge

    // clouds composite LAST, over the glow and the disc: thin stratus veiling
    // the sun (like the reference) instead of sitting behind it
    float cloudDensity;
    vec3 c = clouds(dir, cloudDensity);
    float shade = 0.60 + 0.30 * smoothstep(0.0, 0.50, dir.y);
    sky = mix(sky, c * shade, clamp(cloudDensity, 0.0, 0.85));

    // HDR-ish output for the env map (tone mapping happens in the sea shader)
    fragColor = vec4(sky, 1.0);
}
