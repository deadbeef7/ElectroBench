#version 330 core
// Fragment shader for the sky dome: a dusk gradient plus a handful of
// "volumetric" cumulus clouds, captured once per frame into the environment
// cubemap so the sea below always reflects what is above.
//
// The clouds are NOT noise fields. Every earlier attempt derived coverage from
// an fBm texture, and on real GPUs that always ended one of two ways: the
// threshold let too much of the mean-zero field through (whole sky mottled)
// or the texture grid itself showed as small blocks. Instead each cloud is a
// fixed chain of overlapping analytic puffs (smooth falloff ellipses in
// azimuth/elevation space) placed by the CPU. Coverage is exact by
// construction — real clear-sky gaps between individual clouds — and the
// silhouettes are pure screen-resolution math: no texel-based content left to
// show blocks, no threshold statistics to drift between GPUs.
//
// REALISM MODEL (this pass): shading is no longer a painted gradient. Each
// visible cloud point ray-marches FOUR density samples toward the sun and
// integrates optical depth; light is then
//     sun_light = exp(-k * depth) * (ambient + HG_phase(dot(dir, sun)))
// which produces the real phenomena for free:
//   * silver linings — thin sun-side edges have low depth AND high forward
//     Henyey-Greenstein phase, so they blaze; thick cores at the same angle
//     stay dark because transmission dies
//   * dark anti-sun bulk — grazing/backward phase is tiny
//   * cool blue skylight from above, warm transmission near the sun
//   * powder darkening in dense cores
// There is zero texture content anywhere, so nothing can grid or block.

in vec3 vDir;

uniform float uTime;
uniform float uTonemap;       // 1.0: on-screen pass (LDR out) / 0.0: HDR env cubemap
uniform vec3  uSunDir;
uniform vec3  uZenithColor;
uniform vec3  uMidColor;
uniform vec3  uHorizonColor;
uniform vec3  uSunColor;

#define MAX_CLOUDS 6
uniform int   uCloudCount;
uniform float uCloudAzim[MAX_CLOUDS];   // centre azimuth, radians
uniform float uCloudElev[MAX_CLOUDS];   // centre elevation, radians
uniform float uCloudRadius[MAX_CLOUDS]; // angular half-height, radians
uniform float uCloudStretch[MAX_CLOUDS];// azimuthal elongation (>1 = wider than tall)

out vec4 fragColor;

const float PI = 3.14159265359;

// Cheap density-only evaluation used by the sunward march (no lighting outs).
float cloudDensityOnly(vec3 dir) {
    float density = 0.0;
    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (i >= uCloudCount) break;
        vec3 c = vec3(cos(uCloudElev[i]) * cos(uCloudAzim[i]),
                      sin(uCloudElev[i]),
                      cos(uCloudElev[i]) * sin(uCloudAzim[i]));
        float R = uCloudRadius[i];
        float stretch = max(uCloudStretch[i], 1.0);
        vec3 t1 = normalize(vec3(-sin(c.z), 0.0, cos(c.z)));
        vec3 t2 = normalize(cross(c, t1));
        vec3 rel = dir - c * dot(dir, c);
        vec2 p = vec2(dot(rel, t1) / stretch, dot(rel, t2));
        if (dot(p, p) > R * R * 2.4) continue;
        const int PUFFS = 5;
        vec2 off[PUFFS];
        off[0] = vec2( 0.00,  0.00);
        off[1] = vec2( 0.78,  0.14);
        off[2] = vec2(-0.72,  0.20);
        off[3] = vec2( 0.34, -0.20);
        off[4] = vec2(-0.34, -0.16);
        float prad[PUFFS];
        prad[0] = 0.60; prad[1] = 0.42; prad[2] = 0.38; prad[3] = 0.34; prad[4] = 0.32;
        for (int j = 0; j < PUFFS; j++) {
            vec2 q = p - off[j] * R;
            float ang = atan(q.y, q.x);
            float rj = R * prad[j] * (1.0
                + 0.15 * sin(ang * 3.0 + uCloudAzim[i] * 7.0 + float(j) * 2.1)
                + 0.09 * sin(ang * 5.0 - uCloudAzim[i] * 11.0 + float(j) * 4.7));
            float dj = length(q);
            density = max(density, 1.0 - smoothstep(rj * 0.68, rj * 1.28, dj));
        }
        density *= 1.0 - smoothstep(R * 0.95, R * 1.55, length(p)) * 0.78;
    }
    return clamp(density, 0.0, 1.0);
}

// Full evaluation at the visible point. Returns density in [0,1] plus the
// height-inside-mass factor for the ambient gradient and the silhouette-edge
// softness for edge detail.
float cloudField(vec3 dir, out float upness, out float edge) {
    float density = 0.0;
    float heightSum = 0.0;
    float weight = 0.0;
    float e = 1.0;

    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (i >= uCloudCount) break;
        vec3 c = vec3(cos(uCloudElev[i]) * cos(uCloudAzim[i]),
                      sin(uCloudElev[i]),
                      cos(uCloudElev[i]) * sin(uCloudAzim[i]));
        float R = uCloudRadius[i];
        float stretch = max(uCloudStretch[i], 1.0);
        vec3 t1 = normalize(vec3(-sin(c.z), 0.0, cos(c.z)));
        vec3 t2 = normalize(cross(c, t1));
        vec3 rel = dir - c * dot(dir, c);
        vec2 p = vec2(dot(rel, t1) / stretch, dot(rel, t2));
        if (dot(p, p) > R * R * 2.4) continue;

        const int PUFFS = 5;
        vec2 off[PUFFS];
        off[0] = vec2( 0.00,  0.00);
        off[1] = vec2( 0.78,  0.14);
        off[2] = vec2(-0.72,  0.20);
        off[3] = vec2( 0.34, -0.20);
        off[4] = vec2(-0.34, -0.16);
        float prad[PUFFS];
        prad[0] = 0.60; prad[1] = 0.42; prad[2] = 0.38; prad[3] = 0.34; prad[4] = 0.32;

        float local = 0.0;
        for (int j = 0; j < PUFFS; j++) {
            vec2 q = p - off[j] * R;
            // gentle angular wobble: bulges each puff's silhouette organically
            // (two LOW-frequency sine harmonics of the angle around the puff
            // centre — integer harmonics stay continuous through atan's
            // branch, the shapes stay big and smooth at screen resolution,
            // and there is zero texture content so nothing can grid/block).
            float ang = atan(q.y, q.x);
            float rj = R * prad[j] * (1.0
                + 0.15 * sin(ang * 3.0 + uCloudAzim[i] * 7.0 + float(j) * 2.1)
                + 0.09 * sin(ang * 5.0 - uCloudAzim[i] * 11.0 + float(j) * 4.7));
            float dj = length(q);
            local = max(local, 1.0 - smoothstep(rj * 0.68, rj * 1.28, dj));
        }
        // overall envelope fade so the long ends dissolve into the sky
        local *= 1.0 - smoothstep(R * 0.95, R * 1.55, length(p)) * 0.78;

        if (local > 0.001) {
            // height inside the mass: drives the skylight-from-above gradient
            float hn = clamp(p.y / R + 0.5, 0.0, 1.0);
            heightSum += hn * local;
            weight += local;
            e = min(e, local);
        }
        density = max(density, local);
    }

    upness = weight > 0.001 ? heightSum / weight : 0.0;
    edge = e;
    return clamp(density, 0.0, 1.0);
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

    // thin bright glow line hugging the horizon itself — real dusks have a
    // last sliver of lit atmosphere between the darkening sea and sky
    sky += vec3(0.16, 0.075, 0.055)
         * (1.0 - smoothstep(0.0, 0.05, h))
         * (0.45 + 0.55 * pow(sunAmount, 2.0));

    // warm horizon glow hugging the horizon around the sun azimuth — pulled
    // tighter and dimmer so the glow is a compact band, not a sky-wide wash
    float glowMask = pow(sunAmount, 22.0) * 0.42 + pow(sunAmount, 50.0) * 0.62;
    glowMask *= 1.0 - smoothstep(0.05, 0.55, h) * 0.85;  // strongest at the horizon
    sky = mix(sky, vec3(1.55, 0.52, 0.18), clamp(glowMask, 0.0, 0.52));

    // broader soft gold field above the horizon glow — tighter and dimmer
    float goldMask = pow(sunAmount, 170.0) * 0.62 * (1.0 - smoothstep(0.0, 0.70, h) * 0.6);
    sky = mix(sky, vec3(2.3, 1.05, 0.42), clamp(goldMask, 0.0, 0.62));

    // hot core just behind the clouds where the sun sits
    float coreMask = pow(sunAmount, 320.0) * 0.85;
    sky = mix(sky, vec3(5.2, 2.1, 0.72), clamp(coreMask, 0.0, 0.90));

    // ---- explicit volumetric clouds composite over the glow ----
    float upn, edgeF;
    float cl = cloudField(dir, upn, edgeF);

    if (cl > 0.004) {
        // ---- self-shadowing: integrate optical depth toward the sun ----
        vec3 sunTan = sd - dir * dot(sd, dir);
        float stLen = length(sunTan);
        float depth = 0.0;
        if (stLen > 0.02) {
            sunTan /= stLen;
            // angular step sized to the current cloud scale (radii ~0.03-0.05 rad)
            for (int s = 1; s <= 4; s++) {
                depth += cloudDensityOnly(normalize(dir + sunTan * (0.021 * float(s))));
            }
        } else {
            depth = cl * 4.0;   // looking straight through the mass at the sun
        }
        depth *= 0.30;          // -> optical depth scale

        float transmit = exp(-2.4 * depth);
        float ms = 1.0 - exp(-depth * 1.7);   // multiple-scatter energy floor

        // Henyey-Greenstein forward-scatter phase (g = 0.72): viewing rays
        // toward the sun through thin vapour blaze; backscatter is dead.
        float g = 0.72;
        float cosT = clamp(dot(dir, sd), -1.0, 1.0);
        float phase = (1.0 - g * g)
                    / (4.0 * PI * pow(1.0 + g * g - 2.0 * g * cosT, 1.5));
        phase *= 4.0 * PI;      // normalise: 1.0 == isotropic

        // light transport
        vec3 sunCol = vec3(1.30, 0.74, 0.42);          // warm low-sun beam
        vec3 skyAmb = vec3(0.34, 0.40, 0.55);          // cool skylight from above
        float amb = mix(0.20, 0.85, upn);              // undersides dark, tops lit
        float sunLight = transmit * (0.45 + 2.1 * phase);

        vec3 col = vec3(0.84, 0.86, 0.90) * skyAmb * amb * 0.95
                 + sunCol * sunLight * 0.85
                 + sunCol * ms * 0.12;                 // in-scattered ambient beam

        // powder effect: dense cores read slightly darker even when lit
        col *= 1.0 - 0.20 * cl * cl;

        // aerial perspective: banks near the horizon sink into the haze colour
        float apFade = (1.0 - smoothstep(0.015, 0.22, h)) * 0.40;
        col = mix(col, vec3(0.30, 0.17, 0.13), apFade);

        sky = mix(sky, col, clamp(cl * 1.12, 0.0, 0.97));
    }

    // compact orange disc with a TIGHT halo — the reference sun is a defined
    // ball, not a bloom blob. Drawn last, attenuated by cloud cover.
    float cover = cl;
    float disc = smoothstep(0.9977, 0.9992, sunAmount) * (1.0 - 0.80 * clamp(cover, 0.0, 1.0));
    float halo = pow(sunAmount, 900.0) * 0.45 * (1.0 - 0.5 * clamp(cover, 0.0, 1.0));
    sky = mix(sky, vec3(8.5, 4.6, 1.7), clamp(disc + halo, 0.0, 1.0));

    // Env-cubemap pass keeps HDR values (the sea shader tone maps after adding
    // glitter). The on-screen dome pass tone maps + gammas right here so the
    // visible sky matches what the water reflects.
    vec3 outCol = mix(sky, pow(sky / (sky + vec3(1.0)), vec3(1.0 / 2.2)), uTonemap);
    fragColor = vec4(outCol, 1.0);
}
