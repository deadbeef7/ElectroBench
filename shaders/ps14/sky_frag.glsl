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

#define MAX_CLOUDS 9
uniform int   uCloudCount;
uniform float uCloudPhase[MAX_CLOUDS];  // slow cloud-local aging frame (updated on
                                        // the CPU from the DRIFTED azimuth), so the
                                        // morph noise advects WITH the cloud instead of
                                        // fighting it — the old absolute-time morph made
                                        // lobes pop and jitter as the bank crawled
uniform float uCloudAzim[MAX_CLOUDS];   // centre azimuth, radians
uniform float uCloudElev[MAX_CLOUDS];   // centre elevation, radians
uniform float uCloudRadius[MAX_CLOUDS]; // angular half-height, radians
uniform float uCloudStretch[MAX_CLOUDS];// azimuthal elongation (>1 = wider than tall)

out vec4 fragColor;

const float PI = 3.14159265359;

// BUILD-P21: ONE DISPLAY TRANSFORM, SHARED WITH THE SEA.
// MUST stay byte-identical in shaders/ps14/sea_frag.glsl: the sea reflects this
// dome out of the HDR cubemap and runs the result through this function ITSELF,
// so any difference between the two curves shows up as a reflection that does
// not match the thing it reflects. Before P21 the dome used Reinhard plus a
// warm roll-off and the sea Reinhard without one, so the sun path came back
// visibly different from the sky above it.
vec3 encodeScene(vec3 hdr, float dith) {
    // ACES (Narkowicz) shoulder: rolls the sun disc and glitter path off
    // instead of letting them clip into flat paper.
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    // Exposure. ACES is not a like-for-like swap for Reinhard (at 0.2 it
    // returns 0.30 vs Reinhard's 0.17), so the curve change is paid for here,
    // once, rather than by walking every radiance in the scene.
    hdr *= 0.70;
    vec3 x = clamp(max(hdr, vec3(0.0)), 0.0, 8.0);
    x = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);

    // Dusk highlight roll-off. A real sun does not go white, it goes through
    // orange on its way to being clipped; per-channel tonemapping alone sends
    // the brightest 2% of a saturated orange frame straight to paper.
    float lum0 = dot(x, vec3(0.2126, 0.7152, 0.0722));
    const float warmStart = 0.45;
    if (lum0 > warmStart) {
        float f = clamp((lum0 - warmStart) / 0.20, 0.0, 1.0);
        f = f * f;
        vec3 warm = vec3(1.0, 0.62, 0.36);
        warm *= lum0 / max(dot(warm, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
        x = mix(x, warm, f);
    }

    vec3 g = pow(max(x, vec3(0.0)), vec3(1.0 / 2.2));

    // ---- THE GRADE, applied in DISPLAY space on purpose. Gamma encoding
    // roughly HALVES apparent saturation (a 0.52 linear channel ratio arrives
    // near 0.28 on screen), so a lift applied before the gamma is mostly
    // cancelled by it. Pre-gamma, this frame measured a mean saturation of
    // 0.130 — a brown-grey mush rather than a sunset.
    float lum = dot(g, vec3(0.2126, 0.7152, 0.0722));
    g = mix(g, g * vec3(0.93, 0.98, 1.13),
            (1.0 - smoothstep(0.02, 0.34, lum)) * 0.50);   // cool shadows
    g = mix(g, g * vec3(1.07, 1.01, 0.92),
            smoothstep(0.52, 1.00, lum) * 0.45);          // warm highlights
    g = clamp(mix(vec3(lum), g, 1.34), 0.0, 1.0);

    // Two-tap triangular dither. The dusk gradient is the widest smooth ramp
    // in the project - a whole ocean fading into a whole sky - and 8-bit
    // output draws visible contour lines across exactly that ramp.
    g += dith * (1.5 / 255.0);
    return clamp(g, 0.0, 1.0);
}

// Wisp flattening: clouds 7+ (the thin streaks above the sun) are vertically
// squashed so they read as cirrus veils instead of miniature cumulus puffs.
float wispSquash(int i) { return i >= 7 ? 0.55 : 1.0; }

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
            // Cloud-local aging phase: the lobe outlines evolve in a frame that
            // moves with the bank (uCloudPhase derives from the drifted azimuth),
            // so shape noise never counter-scrolls against the cloud itself.
            float ph = uCloudPhase[i];
            float morph = 0.045 * sin(ph + float(j) * 1.7)
                        + 0.035 * cos(ph * 0.65 + float(j) * 2.9);
            float wobble = 0.11 * sin(ang * 3.0 + uCloudAzim[i] * 7.0 + float(j) * 2.1)
                         + 0.065 * sin(ang * 5.0 - uCloudAzim[i] * 11.0 + float(j) * 4.7)
                         + morph;
            float lobeNoise = erosionNoise(vec2(
                cos(ang) * 1.8 + uCloudAzim[i] * 3.7,
                sin(ang) * 1.8 + float(j) * 2.1 + uCloudElev[i] * 9.0));
            float rj = R * radii[j] * (1.0 + wobble) * (0.91 + 0.16 * lobeNoise);
            // Wider than deep in the angular local frame: cloud shoulders stay
            // broad while the depth term gives the lobes a rounded volume.
            // Wisps squash the depth axis so they stay paper-thin.
            vec3 metric = vec3(q.x / rj, q.y / (rj * 0.92), q.z / (rj * 1.18)) * vec3(1.0, 1.0, wispSquash(i));
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
        // The knee opens at 0.06 (was 0.035): below that the smoothstep slope
        // shattered semi-dense regions into isolated bright blobs — the
        // "glitched" cauliflower patchwork. A shallower slope keeps the
        // silhouette organic without the mid-density fragmentation.
        float n = erosionNoise(envelopeP * 3.2 + vec2(uCloudAzim[i] * 5.1, i * 7.3));
        float fine = erosionNoise(envelopeP * 7.8 + vec2(uCloudAzim[i] * 11.0, i * 13.0));
        float shoulder = 1.0 - smoothstep(0.10, 0.82, local);
        float breakup = 0.70 + 0.30 * n + 0.12 * fine - 0.20 * shoulder;
        local = smoothstep(0.06, 0.74, local * breakup) * envelope;

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
}    // Dusk sky colour at a direction, shared by the gradient and the cloud
// lighting: clouds are lit by the same sky they hang in, so the blue fill on
// their shaded sides is the ACTUAL zenith/mid colour from that direction
// instead of a constant.
vec3 skyGradient(vec3 dir, vec3 sd) {
    float hh = clamp(dir.y, 0.0, 1.0);
    vec3 s = mix(uHorizonColor, uMidColor, smoothstep(0.0, 0.14, hh));
    s = mix(s, uZenithColor, smoothstep(0.10, 0.38, hh));
    // HYPER-REAL SKY FILL: real dusk sky is NOT an even warm gradient — the
    // side facing the sun gets the warm scatter, the top is cool blue, and the
    // anti-sun sky falls toward the dark Earth shadow. A directional falloff
    // here already does that; add a gentle spatial softening so the gradient
    // itself reads as atmospheric depth, not a painted dome.
    float sa = max(dot(dir, sd), 0.0);
    float shadowedWarmth = 0.22 + 0.78 * pow(sa, 4.0);
    // push the brightest warm light toward the horizon band, not the zenith
    float horizonBoost = smoothstep(0.0, 0.20, hh) * (1.0 - smoothstep(0.20, 0.60, hh));
    return s * (shadowedWarmth + 0.08 * horizonBoost);
}

void main() {
    vec3 dir = normalize(vDir);
    float h = clamp(dir.y, 0.0, 1.0);
    vec3 sd = normalize(uSunDir);

    // Sunset gradient: deep blue-black zenith through mauve, into the warm
    // horizon band. Multiplied by a sun-direction falloff so the anti-sun sky
    // stays dark like the reference shot.
    vec3 sky = skyGradient(dir, sd);

    float sunAmount = max(dot(dir, sd), 0.0);

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

    // ---- LOW STRATUS BARS. BUILD-P21. Measured on the baseline frame: every
    // hand-placed bank sits between 9 and 27 degrees elevation with a 1.5-2.9
    // degree radius, so nothing existed below about 6 degrees — and that empty
    // strip measured 95% of its pixels with a local standard deviation under
    // 0.006, i.e. below 1.5/255. It was the single largest dead-flat region in
    // the picture, and it sat directly above the brightest, busiest part of the
    // frame. Real dusk skies are almost never clear that close to the horizon:
    // the last few degrees carry thin, broken, hard-foreshortened cloud bars
    // lit from UNDERNEIGH because the sun is under them. They are the
    // difference between "gradient" and "sky", and they cost one band.
    {
        float band = smoothstep(0.004, 0.022, h) * (1.0 - smoothstep(0.050, 0.150, h));
        if (band > 0.002) {
            // Foreshortening: real cloud streets compress toward the horizon,
            // so the sample's vertical frequency RISES as h falls.
            float az = atan(dir.z, dir.x);
            float squash = 22.0 + 86.0 * smoothstep(0.0, 0.15, h);
            vec2 sp = vec2(az * 2.6 + uTime * 0.006, h * squash);
            float n1 = erosionNoise(sp);
            float n2 = erosionNoise(sp * vec2(2.3, 1.7) + 5.1);
            float bars = smoothstep(0.505, 0.735, n1 * 0.72 + n2 * 0.28);
            bars *= band;
            // lit from below, so the undersides are the brightest thing in the
            // band toward the sun and the tops are never visible at all
            float under = pow(max(sunAmount, 0.0), 3.0);
            vec3 barCol = mix(vec3(0.070, 0.044, 0.068), vec3(1.15, 0.44, 0.16),
                              0.20 + 0.80 * under);
            sky = mix(sky, barCol, clamp(bars * 0.82, 0.0, 0.82));
        }
    }

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

        // FULL Henyey-Greenstein forward lobe (two lobes: tight silver
        // lining + broad glow) plus a small isotropic floor — the approximated
        // single-lobe form over-brightened the whole cloud mass.
        float cosT = clamp(dot(dir, sd), -1.0, 1.0);
        float hg1 = (1.0 - 0.82 * 0.82)
                  / (4.0 * PI * pow(max(1.0 + 0.82 * 0.82 - 2.0 * 0.82 * cosT, 1e-4), 1.5));
        float hg2 = (1.0 - 0.45 * 0.45)
                  / (4.0 * PI * pow(max(1.0 + 0.45 * 0.45 - 2.0 * 0.45 * cosT, 1e-4), 1.5));
        float phase = hg1 * 4.0 * PI * 0.72 + hg2 * 4.0 * PI * 0.28
                    + 0.08 * (1.0 - cosT) * 0.5;
        float direct = (0.10 + 1.55 * phase)
                     * (transmit0 + transmit1 + transmit2);

        // Sun colour THROUGH the cloud: Beer-Lambert per channel — dense
        // cores see deep red (blue/green fully scattered out), thin edges see
        // the full orange. A constant sunCol made every lit cloud the same
        // orange regardless of how much cloud it shone through.
        vec3 sunCol = vec3(1.30, 0.72, 0.38)
                    * vec3(exp(-tau * 0.42), exp(-tau * 0.62), exp(-tau * 0.94));
        // Clouds hang in the sky: the blue fill IS the dusk gradient at the
        // cloud's own direction (zenith-blue high up, mauve low), not a flat
        // constant. Slightly boosted so the fill survives the glow behind.
        vec3 skyAmb = skyGradient(dir, sd) * 1.25 + vec3(0.03, 0.04, 0.07);
        float upLight = mix(0.20, 1.0, 0.62 * surface.y + 0.38 * heightWeight);
        float baseLight = 1.0 - 0.34 * surface.z;

        vec3 col = vec3(0.68, 0.71, 0.79) * skyAmb * upLight * 0.86;
        col += sunCol * (direct * 0.74 + multiple * 0.62);

        // Thin, forward-facing edges transmit much more than the bulk. Gate it
        // by the thin-edge field and optical depth so it never becomes a rim.
        float silver = pow(max(sunAmount, 0.0), 6.0) * surface.w
                     * transmit0 * (0.35 + 0.85 * phase);
        col += sunCol * silver * 0.22;

        // Dense droplets absorb more and read as powdery charcoal; flat bases
        // stay cool and dark while upper cauliflower remains blue-white.
        float powder = 1.0 - exp(-tau * 2.4);
        col *= (1.0 - 0.28 * powder) * baseLight;

        float apFade = (1.0 - smoothstep(0.018, 0.24, h)) * 0.44;
        // Aerial perspective tints toward the DUSK BAND at the cloud's own
        // elevation, not a constant: far clouds fade warm near the horizon and
        // mauve higher up, like real atmospheric scattering.
        vec3 apTarget = mix(vec3(0.31, 0.17, 0.13), vec3(0.24, 0.22, 0.30),
                            smoothstep(0.0, 0.45, h));
        col = mix(col, apTarget, apFade);
        col = mix(col, vec3(0.29, 0.31, 0.37), 0.14);

        float alpha = 1.0 - exp(-cl * 2.15);
        sky = mix(sky, col, clamp(alpha * 0.86, 0.0, 0.90));
    }

    // compact orange disc with a TIGHT halo — the reference sun is a defined
    // ball, not a bloom blob. Drawn last, attenuated by cloud cover.
    float cover = 1.0 - exp(-cl * 2.65);
    float disc = smoothstep(0.9977, 0.9992, sunAmount) * (1.0 - 0.88 * clamp(cover, 0.0, 1.0));
    // halo stays tight and warm: real sun glare is a small bright disc with a
    // soft warm halo, not a large bloom; collapse the halo with cloud cover so
    // thin clouds don't erase the disc but heavy ones do.
    float halo = pow(sunAmount, 900.0) * 0.45 * (1.0 - 0.6 * clamp(cover, 0.0, 1.0));
    // push the disc a touch warmer where it is brightest so the HDR core still
    // reads orange after tone map (the previous hot white core washed to pale).
    vec3 sunColHot = vec3(9.2, 5.1, 1.9);
    sky = mix(sky, sunColHot, clamp(disc + halo, 0.0, 1.0));

    // The env-cubemap pass keeps HDR values (the sea shader runs the result
    // through the SAME encodeScene() after adding glitter). The on-screen dome
    // pass encodes here, so the visible sky and its reflection in the water
    // cannot drift apart: dusk sun and bloom roll through orange instead of
    // desaturating to paper, the glow band stays warm to the last pixel, and
    // both passes get the same grade and the same dither.
    vec3 outCol = sky;
    if (uTonemap > 0.5) {
        float dith = (hash21(gl_FragCoord.xy + fract(uTime) * 13.0)
                     + hash21(gl_FragCoord.xy * 1.7 + 41.0) - 1.0) * 0.5;
        outCol = encodeScene(sky, dith);
    }
    fragColor = vec4(outCol, 1.0);
}
