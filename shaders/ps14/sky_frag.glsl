#version 330 core
// Fragment shader for the sky dome: gradient zenith -> horizon plus two layers
// of fBm clouds. The dome is captured once per frame into the environment
// cubemap, so the sea below always reflects what is above.

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

// One octave of drifting domain-warped clouds on the upper hemisphere
vec3 clouds(vec3 dir) {
    // cylindrical unwrap; poles are never visible at this camera height
    vec2 uv = vec2(atan(dir.z, dir.x) * (0.5 / PI) + 0.5, dir.y);

    float fade = smoothstep(0.01, 0.22, dir.y);       // thin out at the horizon
    if (fade <= 0.001) return vec3(0.0);

    float wind = uTime * 0.0055;
    float t1 = 0.0, t2 = 0.0;
    float amp = 0.5;
    vec2 flow1 = vec2(wind, wind * 0.35);
    vec2 flow2 = vec2(-wind * 0.6, wind * 0.5);

    for (int i = 0; i < 5; i++) {
        vec2 p1 = uv * (3.1 * amp * 2.0) + flow1 * amp * 2.0;
        vec2 p2 = uv * (5.7 * amp * 2.0) + flow2 * amp * 2.0 + 17.0;
        t1 += texture(uNoiseTex, p1).r * amp;
        t2 += texture(uNoiseTex, p2).g * amp;
        amp *= 0.5;
    }
    t1 = t1 / 0.96875 - 0.5;   // normalise 5-octave sum (1.0 - 0.5^5)
    t2 = t2 / 0.96875 - 0.5;

    float cover = smoothstep(0.12, 0.58, t1 * 0.9 + t2 * 0.35 + 0.02);
    float wisp  = smoothstep(0.30, 0.75, t2 + t1 * 0.5) * 0.55;

    // dusk clouds: dim blue-gray away from the sun, warm lining near it
    float sunAmount = max(dot(normalize(dir), normalize(uSunDir)), 0.0);
    vec3 base = mix(vec3(0.44, 0.52, 0.82), uSunColor * 1.5, pow(sunAmount, 10.0) * 0.6);
    vec3 c = base * (cover * 0.85 + wisp);
    return c;
}

void main() {
    vec3 dir = normalize(vDir);
    float h = clamp(dir.y, 0.0, 1.0);

    // Sunset gradient: warm gold band at the horizon, orange mid-sky,
    // dark blue overhead away from the sun. The camera only sees elevations
    // up to ~25 deg, so blue must take over by h~0.2.
    vec3 sky = mix(uHorizonColor, uMidColor, smoothstep(0.0, 0.10, h));
    sky = mix(sky, uZenithColor, smoothstep(0.08, 0.22, h));

    // orange glow + yellow sun around/above the clouds. HDR values are chosen
    // so that after the Reinhard tonemap + gamma they come out saturated:
    // additive terms get crushed to white, replace-style mixes keep their hue.
    vec3 sd = normalize(uSunDir);
    float sunAmount = max(dot(dir, sd), 0.0);
    vec3 c = clouds(dir);
    float shade = 0.55 + 0.35 * smoothstep(0.0, 0.50, dir.y);
    sky = mix(sky, c * shade, clamp(c.r * 0.7 + c.g * 0.7, 0.0, 0.70));

    // tight falloff: warm only within ~10-15 deg of the sun, dark blue elsewhere
    float glowMask = clamp(pow(sunAmount, 45.0) * 0.9 + pow(sunAmount, 160.0) * 0.55, 0.0, 0.95);
    sky = mix(sky, vec3(3.2, 1.0, 0.18), glowMask);   // -> ~(225,186,110) orange

    float disc = smoothstep(0.9955, 0.9985, sunAmount);
    sky = mix(sky, vec3(9.0, 2.2, 0.15), disc);       // -> ~(243,215,101) golden yellow

    // HDR-ish output for the env map (tone mapping happens in the sea shader)
    fragColor = vec4(sky, 1.0);
}
