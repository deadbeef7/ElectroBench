#version 330 core
// SCENE 4 (POWER LINES): the sky. A warm orange late-afternoon gradient —
// deep amber at the zenith through pale peach into a hazy cream horizon —
// with soft white cumulus drifting slowly, and a LOW SUN: a tight veiled
// disc sitting right on the haze band (wires and poles cross it — the
// serial-experiments silhouette money shot), not a diffuse glow.
// Fully analytic: the clouds are two octaves of smooth value noise carved
// into puffy cells, scrolled at two different speeds (parallax depth).
// A tiny hash dither kills the last banding on wide gradients.

in vec3 vDir;                 // world position on the dome (radius 400)

out vec4 fragColor;

uniform vec3  uEyePos;        // the ray origin: dir = vDir - uEyePos
uniform vec3  uSunDir;        // normalised, late-afternoon low sun
uniform float uTime;

// 2D value noise with quintic smoothing: cheap, seamless enough for clouds,
// no texture fetches (analytic-only rule for the bonus scenes).
float vhash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = vhash(i);
    float b = vhash(i + vec2(1.0, 0.0));
    float c = vhash(i + vec2(0.0, 1.0));
    float d = vhash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0;
    v += vnoise(p) * 0.62;
    v += vnoise(p * 2.13 + 17.7) * 0.26;
    v += vnoise(p * 4.41 + 41.3) * 0.12;
    return v;
}

void main() {
    vec3 dir = normalize(vDir - uEyePos);     // ray direction from the camera
    float h = dir.y;                          // -1 .. 1

    // ---- the orange gradient ------------------------------------------------
    // zenith amber -> peach -> pale cream horizon. Below the horizon the
    // gradient keeps darkening (the ground quad covers most of it anyway).
    vec3 zen   = vec3(0.86, 0.38, 0.10);
    vec3 mid   = vec3(0.98, 0.62, 0.28);
    vec3 horiz = vec3(1.00, 0.83, 0.58);
    vec3 sky;
    if (h >= 0.0) {
        float t = pow(clamp(h, 0.0, 1.0), 0.55);        // wide horizon band
        sky = mix(mix(horiz, mid, smoothstep(0.0, 0.35, t)),
                  zen, smoothstep(0.30, 0.95, t));
    } else {
        sky = mix(horiz, vec3(0.42, 0.22, 0.10), smoothstep(0.0, -0.35, h));
    }

    // ---- veiled sun: a TIGHT LOW DISC on the haze band. The camera dollies
    // straight toward it, so poles and wires cross the disc as silhouettes.
    // BUILD-P2: core/halo trimmed to pay for the disc (same clip budget as
    // build P1).
    float sunAmt = max(dot(dir, normalize(uSunDir)), 0.0);
    float disc = smoothstep(0.9996, 0.99985, sunAmt);           // ~1.6° veil
    sky += vec3(1.0, 0.86, 0.62) * disc * 0.62;
    sky += vec3(1.0, 0.80, 0.52) * pow(sunAmt, 30.0) * 0.24;   // tight core
    sky += vec3(1.0, 0.70, 0.38) * pow(sunAmt, 7.0) * 0.06;    // wide halo
    // two faint ray crossbars hugging the horizon (dusk diffusion) — gated
    // by height so they never draw full-height streaks through the clouds
    float bar1 = abs(dot(dir.xz, normalize(vec2(0.94, -0.34))));
    float bar2 = abs(dot(dir.xz, normalize(vec2(-0.34, 0.94))));
    sky += vec3(1.0, 0.74, 0.42) * pow(sunAmt, 8.0) * exp(-max(h, 0.0) * 10.0)
         * (exp(-bar1 * 22.0) + exp(-bar2 * 22.0)) * 0.04;

    // ---- clouds --------------------------------------------------------------
    // Project the ray onto a virtual cloud deck (like the ocean scene's
    // shadows): p = dir.xz / dir.y gives a natural perspective squashing.
    if (h > 0.015) {
        float deckH = 1.0;
        vec2 cp = dir.xz / h * deckH;
        vec2 drift = vec2(uTime * 0.006, -uTime * 0.0023);  // wind flows up-
                                                            // corridor, like
                                                            // the wires lean
        float n = fbm(cp * 0.85 + drift);
        float n2 = fbm(cp * 1.90 - drift * 1.7 + 31.0);      // second layer
        // carve puffy cells: smooth band of the fbm, second layer breaks it
        float cells = smoothstep(0.42, 0.66, n);
        cells *= 0.75 + 0.25 * smoothstep(0.36, 0.64, n2);
        // fade with height (thin at zenith) and near the horizon (haze eats
        // distant clouds) — also keeps the projection's blowup tamed
        float horizFade = smoothstep(0.015, 0.16, h);
        float zenFade   = 1.0 - smoothstep(0.55, 0.95, h);
        float cover = cells * horizFade * zenFade;

        // WHITE tops (HDR >1: clips to 255 through the output — the reference
        // look is pure white cumulus on orange), warm shadowed bellies under
        // them (light from the low sun)
        float bell = smoothstep(0.60, 0.48, n) * cover;
        vec3 cloudCol = mix(vec3(1.08, 1.02, 0.94), vec3(0.94, 0.60, 0.34), bell * 0.85);
        sky = mix(sky, cloudCol, clamp(cover * 1.18, 0.0, 1.0));
    }

    // horizon haze band: a bright cream strip right at eye level sells the
    // heavy late-afternoon atmosphere the wires silhouette against
    // (BUILD-P2: 0.20 -> 0.14 — the sun disc now supplies the eye-level glare)
    float band = exp(-abs(h) * 26.0);
    sky += vec3(1.0, 0.88, 0.66) * band * 0.14;

    // subtle dither: kills gradient banding on smooth drivers
    float dith = vhash(dir.xy * 1913.7 + fract(uTime) * 17.0);
    sky += (dith - 0.5) * (1.5 / 255.0);

    fragColor = vec4(sky, 1.0);
}
