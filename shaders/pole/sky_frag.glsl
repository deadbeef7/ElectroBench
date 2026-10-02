#version 330 core
// SCENE 4 (POWER LINES): the sky. A warm orange late-afternoon gradient —
// deep amber at the zenith through pale peach into a hazy cream horizon —
// with soft white cumulus drifting slowly, and a LOW SUN: a tight veiled
// disc sitting right on the haze band (wires and poles cross it — the
// serial-experiments silhouette money shot), not a diffuse glow.
// Fully analytic: the clouds are smooth value noise carved into puffy cells,
// scrolled at two different speeds (parallax depth). No textures.
//
// BUILD-P7: the dome is drawn LAST, depth-tested, so it only shades pixels
// the world did not cover (about half the frame). That bought the budget for
// a better cloud: the old projection dir.xz/h exploded as h -> 0 and smeared
// the noise into horizontal streaks along the horizon — the single ugliest
// thing in build P6. The deck is now clamped, and the last few degrees above
// the horizon are left to the haze, which is what actually happens at dusk.

in vec3 vDir;                 // world position on the dome (radius 400)

out vec4 fragColor;

uniform vec3  uEyePos;        // the ray origin: dir = vDir - uEyePos
uniform vec3  uSunDir;        // normalised, late-afternoon low sun
uniform float uTime;

// 2D value noise with cubic smoothing. The hash is a fract/dot chain, NOT
// the sin-based one from the pool scene: this dome covers half the frame on
// a pre-SSE CPU and sin() costs ~20 cycles there.
float vhash(vec2 p) {
    vec3 q = fract(vec3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = vhash(i);
    float b = vhash(i + vec2(1.0, 0.0));
    float c = vhash(i + vec2(0.0, 1.0));
    float d = vhash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm2(vec2 p) {
    return vnoise(p) * 0.66 + vnoise(p * 2.17 + 19.3) * 0.34;
}

void main() {
    vec3 dir = normalize(vDir - uEyePos);     // ray direction from the camera
    float h = dir.y;                          // -1 .. 1

    // ---- the orange gradient ------------------------------------------------
    // zenith amber -> peach -> pale cream horizon. Below the horizon the
    // gradient keeps darkening (the ground quad covers most of it anyway).
    vec3 zen   = vec3(0.80, 0.31, 0.07);
    vec3 mid   = vec3(0.97, 0.58, 0.24);
    vec3 horiz = vec3(1.00, 0.84, 0.60);
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
    // BUILD-P6 MOVING SUN: uSunDir drifts in azimuth and sinks over the run
    // (the C++ side advances it); disc, bloom and glow all follow.
    float sunAmt = max(dot(dir, normalize(uSunDir)), 0.0);
    float disc = smoothstep(0.9996, 0.99985, sunAmt);           // ~1.6 deg veil
    sky += vec3(1.0, 0.86, 0.62) * disc * 0.44;
    sky += vec3(1.0, 0.80, 0.52) * pow(sunAmt, 30.0) * 0.20;   // tight core
    sky += vec3(1.0, 0.70, 0.38) * pow(sunAmt, 7.0) * 0.055;   // wide halo

    // ---- clouds --------------------------------------------------------------
    // Project the ray onto a virtual cloud deck (like the ocean scene's
    // shadows): p = dir.xz / dir.y gives a natural perspective squashing.
    // BUILD-P7: the deck height is CLAMPED (hh) — dividing by a near-zero h
    // is what smeared the old clouds into horizon streaks.
    if (h > 0.035) {
        float hh = max(h, 0.085);
        vec2 cp = dir.xz / hh * 0.32;
        vec2 drift = vec2(uTime * 0.006, -uTime * 0.0023);  // wind flows up-
                                                            // corridor, like
                                                            // the wires lean
        float n = fbm2(cp * 0.55 + drift);
        float n2 = fbm2(cp * 1.25 - drift * 1.7 + 31.0);   // second layer
        // carve puffy cells: a tight smooth band of the fbm, the second
        // layer chewing holes in it so the edges billow
        float cells = smoothstep(0.44, 0.63, n);
        cells *= 0.72 + 0.28 * smoothstep(0.34, 0.62, n2);
        // the haze eats the last few degrees above the horizon: that is the
        // whole reason the old deck streaked, and leaving it clear reads
        // correctly (distant cloud dissolves into the band)
        float horizFade = smoothstep(0.040, 0.28, h);
        float zenFade   = 1.0 - smoothstep(0.60, 0.98, h);
        float cover = cells * horizFade * zenFade;

        // WHITE tops (HDR >1: clips to 255 through the output — the reference
        // look is pure white cumulus on orange), warm shadowed bellies under
        // them (light from the low sun)
        float bell = smoothstep(0.62, 0.46, n) * cover;
        vec3 cloudCol = mix(vec3(1.10, 1.04, 0.95), vec3(0.90, 0.54, 0.30),
                            bell * 0.90);
        sky = mix(sky, cloudCol, clamp(cover * 1.20, 0.0, 1.0));
    }

    // horizon haze band: a bright cream strip right at eye level sells the
    // heavy late-afternoon atmosphere the wires silhouette against
    float band = exp(-abs(h) * 26.0);
    sky += vec3(1.0, 0.88, 0.66) * band * 0.10;

    // subtle dither: kills gradient banding on smooth drivers
    float dith = vhash(dir.xy * 1913.7 + fract(uTime) * 17.0);
    sky += (dith - 0.5) * (1.5 / 255.0);

    fragColor = vec4(sky, 1.0);
}
