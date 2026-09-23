#version 330 core
// Fragment shader for the sky dome: a dusk gradient plus a handful of explicit
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
// Each mass is ELONGATED along the azimuth (uCloudStretch): puff distances are
// evaluated in a tangent-plane space whose azimuth axis is divided by the
// stretch, turning every puff into a wide shallow lens — long stratus-cumulus
// banks instead of ball clusters.
//
// Puff interior shading is kept LOW-contrast so masses read as lit vapour:
// gentle base-to-top gradient, restrained rim light, soft feathered edges.
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
uniform float uCloudRadius[MAX_CLOUDS]; // angular half-height, radians
uniform float uCloudStretch[MAX_CLOUDS];// azimuthal elongation (>1 = wider than tall)

out vec4 fragColor;

const float PI = 3.14159265359;

// Assemble the explicit puff field. Returns density in [0,1] (1 = opaque core)
// plus lighting terms: upness (height inside the mass), sunness (how far the
// sample sits toward the SUN side of the mass — the key to the silver-lining
// look) and the silhouette-edge softness factor for the rim light.
float cloudField(vec3 dir, vec3 sd, out float upness, out float sunness, out float rim) {
    float density = 0.0;
    float heightSum = 0.0;
    float sunSum = 0.0;
    float weight = 0.0;
    float edge = 1.0;   // min puff-edge softness across the cloud = silhouette

    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (i >= uCloudCount) break;
        vec3 c = vec3(cos(uCloudElev[i]) * cos(uCloudAzim[i]),
                      sin(uCloudElev[i]),
                      cos(uCloudElev[i]) * sin(uCloudAzim[i]));
        float R = uCloudRadius[i];
        float stretch = max(uCloudStretch[i], 1.0);

        // local tangent frame at the cloud centre
        vec3 t1 = normalize(vec3(-sin(c.z), 0.0, cos(c.z)));      // along azimuth
        vec3 t2 = normalize(cross(c, t1));                        // along elevation

        // pixel direction in the cloud's SQUASHED tangent plane: dividing the
        // azimuth axis by 'stretch' maps a circle of radius R onto an ellipse
        // R*stretch wide — the elongated bank shape
        vec3 rel = dir - c * dot(dir, c);
        vec2 p = vec2(dot(rel, t1) / stretch, dot(rel, t2));
        if (dot(p, p) > R * R * 2.4) continue;

        // each cloud = 5 overlapping puffs (offsets in squashed space,
        // fraction of R) — a long loose chain of lumps, not a ball union
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
            float dj = length(p - off[j] * R);
            float rj = R * prad[j];
            // wide feathered falloff so silhouettes evaporate instead of popping
            local = max(local, 1.0 - smoothstep(rj * 0.68, rj * 1.28, dj));
        }
        // overall envelope fade so the long ends dissolve into the sky
        local *= 1.0 - smoothstep(R * 0.95, R * 1.55, length(p)) * 0.78;

        if (local > 0.001) {
            // "height" inside the cloud: squashed-plane elevation relative to
            // the mass centre — drives the volumetric top-lit gradient
            float hn = clamp(p.y / R + 0.5, 0.0, 1.0);
            // volumetric DEPTH: projection of the in-cloud offset onto the
            // sun direction in the cloud's tangent plane (+1 = sun-side edge,
            // -1 = far anti-sun bulk). Real clouds are lit THROUGH from the
            // sun side: bright translucent rim, dark body away.
            vec2 toSun = vec2(dot(sd, t1), dot(sd, t2));
            float dsun = dot(p, toSun) / (R * 1.5);
            heightSum += hn * local;
            sunSum += clamp(dsun, -1.0, 1.0) * local;
            weight += local;
            edge = min(edge, local);
        }
        density = max(density, local);
    }

    upness = weight > 0.001 ? heightSum / weight : 0.0;
    sunness = weight > 0.001 ? clamp(sunSum / weight, -1.0, 1.0) : 0.0;
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
    float upn, sunn, rimF;
    float cl = cloudField(dir, sd, upn, sunn, rimF);

    if (cl > 0.001) {
        float sunAmt = clamp(dot(dir, sd) * 0.5 + 0.5, 0.0, 1.0);

        // VOLUMETRIC LIGHT TRANSPORT (the cheap-but-correct trick): light
        // enters the sun-facing side and dies off with depth. sunward parts
        // glow warm and translucent; the anti-sun bulk stays cool grey-slate.
        float lit = clamp(sunn * 0.5 + 0.5, 0.0, 1.0);       // depth through the mass
        float lit2 = lit * lit;
        vec3 shadeCol = vec3(0.105, 0.105, 0.150);           // cool anti-sun bulk
        vec3 litCol   = vec3(1.15, 0.78, 0.52) * (0.55 + 0.45 * sunAmt); // sun-side vapour
        vec3 bodyCol = mix(shadeCol, litCol, 0.12 + 0.88 * lit2);
        // top surfaces catch extra light even off-sun (sky light from above)
        bodyCol = mix(bodyCol, bodyCol * 1.35 + vec3(0.10, 0.09, 0.08), upn * upn * 0.45);

        // silver lining: silhouette edges on the SUN side blaze, anti-sun
        // edges stay dark — asymmetric, like back-lit real clouds
        float rimSun = rimF * clamp(sunn * 0.5 + 0.35, 0.0, 1.0);
        vec3 rimCol = mix(vec3(0.22, 0.20, 0.28), vec3(1.9, 1.15, 0.68), sunAmt);
        bodyCol += rimCol * rimSun * rimSun * 0.9;

        // faint warm haze where the glow is strong behind the cloud edge
        bodyCol += vec3(1.5, 0.8, 0.45) * pow(sunAmount, 8.0) * (1.0 - cl) * 0.22;

        sky = mix(sky, bodyCol, clamp(cl * 1.10, 0.0, 0.97));
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
