#version 330 core
// Fragment shader for the sky dome: a dusk gradient plus a handful of explicit
// "volumetric" cumulus clouds, captured once per frame into the environment
// cubemap so the sea below always reflects what is above.
//
// The clouds are NOT noise fields. Every earlier attempt derived coverage from
// an fBm texture, and on real GPUs that always ended one of two ways: the
// threshold let too much of the mean-zero field through (whole sky mottled)
// or the texture grid itself showed as small blocks. Instead each cloud is a
// fixed cluster of overlapping analytic puffs (smooth falloff spheres in
// azimuth/elevation space) placed by the CPU. Coverage is exact by
// construction — real clear-sky gaps between individual clouds — and the
// silhouettes are pure screen-resolution math: no texel-based content left to
// show blocks, no threshold statistics to drift between GPUs.
//
// Puff interior shading is the cheap volumetric look: each puff contributes
// density by distance from its centre, and the cloud's lighting is driven by
// height inside the mass (bright toward the sunlit top, near-black base) plus
// rim light on the sun-facing silhouette edge.
//
// Palette follows the 3DMark2001 "Nature" dusk: deep blue-black zenith, warm
// orange horizon band around a low sun, gold-pink rims on the clouds.

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
uniform float uCloudRadius[MAX_CLOUDS]; // angular half-size, radians

out vec4 fragColor;

const float PI = 3.14159265359;

// Angular distance from a direction to a point on the sphere (radians).
// dir and p must be normalised.
float angDist(vec3 dir, vec3 p) {
    return acos(clamp(dot(dir, p), -1.0, 1.0));
}

// Assemble the explicit puff field. Returns density in [0,1] (1 = opaque core)
// plus lighting terms: how far up inside the mass we are (top-lit volumetric
// gradient) and the sun-facing rim factor for the silhouette edge.
float cloudField(vec3 dir, out float upness, out float rim) {
    float azim = atan(dir.z, dir.x);
    float elev = asin(clamp(dir.y, -1.0, 1.0));

    float density = 0.0;
    float heightSum = 0.0;
    float weight = 0.0;
    float edge = 1.0;   // min puff-edge softness across the cloud = silhouette

    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (i >= uCloudCount) break;
        vec3 c = vec3(cos(uCloudElev[i]) * cos(uCloudAzim[i]),
                      sin(uCloudElev[i]),
                      cos(uCloudElev[i]) * sin(uCloudAzim[i]));
        float d = angDist(dir, c);
        float R = uCloudRadius[i];
        if (d > R * 1.45) continue;

        // each cloud = 5 overlapping puffs (offsets in angular space,
        // fraction of R) — the lumps that make a cloud read as one body
        const int PUFFS = 5;
        vec2 off[PUFFS];
        off[0] = vec2( 0.00,  0.00);
        off[1] = vec2( 0.55,  0.18);
        off[2] = vec2(-0.48,  0.26);
        off[3] = vec2( 0.18, -0.30);
        off[4] = vec2(-0.22, -0.22);
        float prad[PUFFS];
        prad[0] = 0.62; prad[1] = 0.44; prad[2] = 0.40; prad[3] = 0.36; prad[4] = 0.34;

        // cloud-plane basis for puff offsets (local tangent frame)
        vec3 t1 = normalize(vec3(-sin(c.z), 0.0, cos(c.z)));      // along azimuth
        vec3 t2 = normalize(cross(c, t1));                        // along elevation

        float local = 0.0;
        for (int j = 0; j < PUFFS; j++) {
            vec3 pj = normalize(c + t1 * (off[j].x * R) + t2 * (off[j].y * R));
            float dj = angDist(dir, pj);
            float rj = R * prad[j];
            // smooth analytic falloff: wide core, soft 25% fringe
            local = max(local, 1.0 - smoothstep(rj * 0.75, rj * 1.12, dj));
        }
        // gentle overall softening so masses don't read as ball unions
        local *= 1.0 - smoothstep(R * 0.95, R * 1.45, d) * 0.55;

        if (local > 0.001) {
            // "height" inside the cloud: elevation relative to the mass
            // centre, normalised — drives the volumetric top-lit gradient
            float hn = clamp((elev - uCloudElev[i]) / R + 0.5, 0.0, 1.0);
            heightSum += hn * local;
            weight += local;
            edge = min(edge, local);
        }
        density = max(density, local);
    }

    upness = weight > 0.001 ? heightSum / weight : 0.0;
    rim = (1.0 - edge) * density;   // strongest at the soft silhouette edge
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

    // ---- explicit volumetric clouds composite over the glow ----
    float upn, rimF;
    float cl = cloudField(dir, upn, rimF);

    if (cl > 0.001) {
        float sunAmt = clamp(dot(dir, sd) * 0.5 + 0.5, 0.0, 1.0);

        // volumetric gradient: near-black base -> warm mid -> sunlit top
        vec3 baseCol = vec3(0.045, 0.040, 0.070);           // shaded underside
        vec3 topCol  = vec3(0.98, 0.62, 0.42) * (0.25 + 0.75 * sunAmt);
        vec3 bodyCol = mix(baseCol, topCol, upn * upn);     // quadratic: darker low

        // rim light on the silhouette, strongest toward the sun
        vec3 rimCol = mix(vec3(0.20, 0.17, 0.24), vec3(2.4, 1.35, 0.85), sunAmt);
        bodyCol += rimCol * rimF * 0.9;

        // faint warm haze where the glow is strong behind the cloud edge
        bodyCol += vec3(1.5, 0.8, 0.45) * pow(sunAmount, 8.0) * (1.0 - cl) * 0.35;

        sky = mix(sky, bodyCol, clamp(cl * 1.25, 0.0, 0.985));
    }

    // big soft disc: the sun is low, so it reads as a compact bright ball with
    // a warm halo. Drawn last and attenuated by cloud cover so a cloud can
    // veil it without erasing it.
    float cover = cl;
    float disc = smoothstep(0.9975, 0.9990, sunAmount) * (1.0 - 0.80 * clamp(cover, 0.0, 1.0));
    float halo = pow(sunAmount, 600.0) * 0.7 * (1.0 - 0.5 * clamp(cover, 0.0, 1.0));
    sky = mix(sky, vec3(9.0, 6.2, 3.6), clamp(disc + halo, 0.0, 1.0));

    // Env-cubemap pass keeps HDR values (the sea shader tone maps after adding
    // glitter). The on-screen dome pass tone maps + gammas right here so the
    // visible sky matches what the water reflects.
    vec3 outCol = mix(sky, pow(sky / (sky + vec3(1.0)), vec3(1.0 / 2.2)), uTonemap);
    fragColor = vec4(outCol, 1.0);
}
