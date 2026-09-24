#version 330 core
// Fragment shader for the sky dome: a dusk gradient plus a handful of
// "volumetric" cumulus clouds, captured once per frame into the environment
// cubemap so the sea below always reflects what is above.
//
// The cloud pass is a hybrid procedural volume: eight anisotropic 3D lobes
// define each hand-placed cumulus bank, while low-frequency value noise erodes
// only the outer density shell. That keeps exact clear-sky gaps and avoids the
// texture-grid blocking of the old sampled-noise approach, but gives the
// silhouette cauliflower-scale breakup instead of overlapping flat ellipses.
//
// Lighting integrates seven density probes toward the sun and combines three
// Beer/energy-conserving scattering octaves. The result has the phenomena that
// matter at sunset: translucent silver edges, warm light bleeding through thin
// shoulders, blue skylight on upward faces, cool dense bases, and dark powder
// in optically thick cores. No cloud texture is sampled anywhere.

in vec3 vDir;

uniform float uTime;
uniform float uTonemap;       // 1.0: on-screen pass (LDR out) / 0.0: HDR env cubemap
uniform vec3  uSunDir;
uniform vec3  uZenithColor;
uniform vec3  uMidColor;
uniform vec3  uHorizonColor;
uniform vec3  uSunColor;

#define MAX_CLOUDS 7
uniform int   uCloudCount;
uniform float uCloudAzim[MAX_CLOUDS];   // centre azimuth, radians
uniform float uCloudElev[MAX_CLOUDS];   // centre elevation, radians
uniform float uCloudRadius[MAX_CLOUDS]; // angular half-height, radians
uniform float uCloudStretch[MAX_CLOUDS];// azimuthal elongation (>1 = wider than tall)

out vec4 fragColor;

const float PI = 3.14159265359;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float erosionNoise(vec2 p) {
    float n = 0.57 * valueNoise(p);
    p = mat2(0.80, -0.60, 0.60, 0.80) * p * 2.03 + 17.1;
    n += 0.29 * valueNoise(p);
    p = mat2(0.80, -0.60, 0.60, 0.80) * p * 2.01 + 11.7;
    n += 0.14 * valueNoise(p);
    return n;
}

float smoothMax(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (a - b) / k, 0.0, 1.0);
    return mix(b, a, h) + k * h * (1.0 - h);
}

// One shared density probe is used by the visible surface, the sun march and
// (with the same constants) the sea's projected cloud shadows. x=density,
// y=height within the mass, z=flat-base coverage, w=thin-edge coverage.
vec4 cloudSample(vec3 dir) {
    dir = normalize(dir);
    float density = 0.0;
    float heightSum = 0.0;
    float baseSum = 0.0;
    float weight = 0.0;
    float edge = 0.0;

    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (i >= uCloudCount) break;
        vec3 c = vec3(cos(uCloudElev[i]) * cos(uCloudAzim[i]),
                      sin(uCloudElev[i]),
                      cos(uCloudElev[i]) * sin(uCloudAzim[i]));
        vec3 delta = dir - c;
        vec3 t1 = normalize(vec3(-sin(c.z), 0.0, cos(c.z)));
        vec3 t2 = normalize(cross(c, t1));
        vec3 p = vec3(dot(delta, t1) / max(uCloudStretch[i], 1.0),
                      dot(delta, t2), dot(delta, c));
        float R = max(uCloudRadius[i], 0.001);
        if (dot(p, p) > R * R * 3.1) continue;

        const int PUFFS = 8;
        vec3 offsets[PUFFS];
        offsets[0] = vec3( 0.00,  0.00,  0.00);
        offsets[1] = vec3( 0.68,  0.02,  0.03);
        offsets[2] = vec3(-0.62,  0.10, -0.04);
        offsets[3] = vec3( 0.25,  0.28,  0.06);
        offsets[4] = vec3(-0.27,  0.35, -0.02);
        offsets[5] = vec3( 0.05,  0.53,  0.08);
        offsets[6] = vec3( 0.43, -0.12, -0.07);
        offsets[7] = vec3(-0.40, -0.10,  0.05);
        float radii[PUFFS];
        radii[0] = 0.55; radii[1] = 0.39; radii[2] = 0.37; radii[3] = 0.34;
        radii[4] = 0.31; radii[5] = 0.27; radii[6] = 0.30; radii[7] = 0.29;

        float local = 0.0;
        for (int j = 0; j < PUFFS; j++) {
            vec3 q = p - offsets[j] * R;
            float ang = atan(q.y, q.x);
            float morph = 0.045 * sin(uTime * 0.10 + uCloudAzim[i] * 9.0 + float(j) * 1.7)
                        + 0.035 * cos(uTime * 0.065 + uCloudAzim[i] * 5.0 + float(j) * 2.9);
            float wobble = 0.11 * sin(ang * 3.0 + uCloudAzim[i] * 7.0 + float(j) * 2.1)
                         + 0.065 * sin(ang * 5.0 - uCloudAzim[i] * 11.0 + float(j) * 4.7)
                         + morph;
            float rj = R * radii[j] * (1.0 + wobble);
            // Wider than deep in the angular local frame: cloud shoulders stay
            // broad while the depth term gives the lobes a rounded volume.
            vec3 metric = vec3(q.x / rj, q.y / (rj * 0.92), q.z / (rj * 1.18));
            float puff = 1.0 - smoothstep(0.62, 1.12, length(metric));
            local = smoothMax(local, puff, 0.10);
        }

        vec2 envelopeP = p.xy / R;
        float ang = atan(envelopeP.y, envelopeP.x);
        float envelopeRadius = length(envelopeP)
            * (1.0 + 0.055 * sin(ang * 4.0 + uCloudAzim[i] * 13.0));
        float envelope = 1.0 - smoothstep(0.96, 1.56, envelopeRadius);

        // Noise modifies the boundary density rather than replacing it. Core
        // lobes stay solid; only the 0..1 shoulder gets cauliflower erosion.
        float n = erosionNoise(envelopeP * 3.2 + vec2(uCloudAzim[i] * 5.1, i * 7.3));
        float shoulder = 1.0 - smoothstep(0.10, 0.82, local);
        float breakup = 0.76 + 0.40 * n - 0.20 * shoulder;
        local = smoothstep(0.055, 0.72, local * breakup) * envelope;

        // Cumulus condensation line: soften the very bottom, preserve a mostly
        // level base, and let the upper lobes rise into rounded towers.
        float baseCut = smoothstep(-0.78, -0.48, envelopeP.y + (n - 0.5) * 0.18);
        local *= baseCut * (1.0 - 0.12 * smoothstep(0.48, 0.95, envelopeP.y));
        local = clamp(local, 0.0, 1.0);

        if (local > 0.002) {
            float hn = clamp(envelopeP.y * 0.5 + 0.52, 0.0, 1.0);
            float baseT = 1.0 - smoothstep(-0.56, -0.16, envelopeP.y);
            heightSum += hn * local;
            baseSum += baseT * local;
            weight += local;
            edge = max(edge, smoothstep(0.08, 0.48, local) * (1.0 - local));
        }
        density = smoothMax(density, local, 0.07);
    }

    if (weight < 0.001) return vec4(0.0);
    return vec4(clamp(density, 0.0, 1.0),
                heightSum / weight, baseSum / weight, clamp(edge, 0.0, 1.0));
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

    // Belt of Venus: the antisolar dusk band — a soft rose glow floating just
    // above the horizon opposite the sun, beneath the rising blue-grey Earth
    // shadow. Real dusks show it, and it gives the off-sun sea something true
    // to reflect instead of flat black.
    float anti = max(-sunAmount, 0.0);
    float belt = pow(anti, 2.5) * smoothstep(0.0, 0.02, h) * (1.0 - smoothstep(0.03, 0.20, h));
    sky += vec3(0.20, 0.10, 0.115) * belt * 0.55;

    // ---- procedural volumetric clouds composite over the glow ----
    vec4 surface = cloudSample(dir);
    float cl = surface.x;

    if (cl > 0.003) {
        // Seven non-uniform probes resolve the thin sunlit shoulder and the
        // much longer optical path through the core without a full-screen
        // volume texture. The same cloudSample() drives visible sky, HDR
        // environment capture, and the projected sea shadows.
        vec3 sunTan = sd - dir * dot(sd, dir);
        float stLen = length(sunTan);
        float depth = 0.0;
        float heightWeight = 0.0;
        if (stLen > 0.015) {
            sunTan /= stLen;
            for (int s = 1; s <= 7; s++) {
                float f = float(s) / 7.0;
                float travel = 0.052 * pow(f, 0.78);
                vec4 probe = cloudSample(normalize(dir + sunTan * travel));
                depth += probe.x;
                heightWeight += probe.x * probe.y;
            }
            heightWeight /= max(depth, 0.001);
        } else {
            depth = cl * 4.8;
            heightWeight = surface.y;
        }

        float tau = depth * 0.34;
        float transmit0 = exp(-tau * 0.95);
        float transmit1 = 0.55 * exp(-tau * 1.45);
        float transmit2 = 0.30 * exp(-tau * 2.15);
        float multiple = 0.15 * exp(-tau * 0.32);

        // Normalised HG forward lobe plus a small isotropic backscatter floor.
        // The combination avoids the hard black anti-sun side while keeping
        // the forward silver lining strongly directional.
        float g = 0.74;
        float cosT = clamp(dot(dir, sd), -1.0, 1.0);
        float hg = (1.0 - g * g)
                 / (4.0 * PI * pow(max(1.0 + g * g - 2.0 * g * cosT, 1e-4), 1.5));
        float phase = hg * 4.0 * PI + 0.08 * (1.0 - cosT) * 0.5;
        float direct = (0.10 + 1.55 * phase)
                     * (transmit0 + transmit1 + transmit2);

        vec3 sunCol = vec3(1.38, 0.72, 0.36);
        vec3 skyAmb = vec3(0.27, 0.36, 0.56);
        float upLight = mix(0.22, 1.0, 0.62 * surface.y + 0.38 * heightWeight);
        float baseLight = 1.0 - 0.34 * surface.z;

        vec3 col = vec3(0.82, 0.86, 0.94) * skyAmb * upLight * 0.92;
        col += sunCol * (direct * 0.90 + multiple);

        // Thin, forward-facing edges transmit much more than the bulk. Gate it
        // by the thin-edge field and optical depth so it never becomes a rim.
        float silver = pow(max(sunAmount, 0.0), 6.0) * surface.w
                     * transmit0 * (0.35 + 0.85 * phase);
        col += sunCol * silver * 0.34;

        // Dense droplets absorb more and read as powdery charcoal; flat bases
        // stay cool and dark while upper cauliflower remains blue-white.
        float powder = 1.0 - exp(-tau * 2.4);
        col *= (1.0 - 0.17 * powder) * baseLight;

        float apFade = (1.0 - smoothstep(0.018, 0.24, h)) * 0.44;
        col = mix(col, vec3(0.31, 0.17, 0.13), apFade);

        float alpha = 1.0 - exp(-cl * 2.65);
        sky = mix(sky, col, clamp(alpha, 0.0, 0.985));
    }

    // compact orange disc with a TIGHT halo — the reference sun is a defined
    // ball, not a bloom blob. Drawn last, attenuated by cloud cover.
    float cover = 1.0 - exp(-cl * 2.65);
    float disc = smoothstep(0.9977, 0.9992, sunAmount) * (1.0 - 0.88 * clamp(cover, 0.0, 1.0));
    float halo = pow(sunAmount, 900.0) * 0.45 * (1.0 - 0.6 * clamp(cover, 0.0, 1.0));
    sky = mix(sky, vec3(8.5, 4.6, 1.7), clamp(disc + halo, 0.0, 1.0));

    // Env-cubemap pass keeps HDR values (the sea shader tone maps after adding
    // glitter). The on-screen dome pass tone maps + gammas right here so the
    // visible sky matches what the water reflects.
    vec3 outCol = mix(sky, pow(sky / (sky + vec3(1.0)), vec3(1.0 / 2.2)), uTonemap);
    fragColor = vec4(outCol, 1.0);
}
