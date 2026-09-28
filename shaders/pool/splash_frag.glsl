#version 330 core
// Crown splash: a thin sheet of water erupting around the impact point.
//
// HYPER-REAL PASS: the film is POOL WATER, not a checker mirror — its
// colour is the same blues as the water body (uWaterA bright surface,
// uWaterB deep body), transmission-dominant face-on, and at grazing angles
// it shows the bright surface sheen of the pool, never flat white and
// never red/white tiles. A dense foam COLLAR hugs the water line (real
// crowns churn white-blue at the base), fingers tear irregularly and CRAWL
// up the sheet (the tear pattern drifts with time, like the sheet is
// actively disintegrating), and brightness follows local thickness.

in vec3 vWorld;
in vec3 vNormal;
in float vParam; // height parameter 0 (water line) .. 1 (rim)
in float vAngle; // 0..1 around the crown

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uWaterA;  // bright pool-water surface blue
uniform vec3 uWaterB;  // deep pool-water body blue
uniform float uTime;

out vec4 fragColor;

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    vec3 N = normalize(vNormal);

    // the film sees the POOL: bright surface blue where the sheet curves
    // toward the camera's reflected view, deep body blue where it shows
    // the water mass behind it. The sweep follows the ACTUAL reflected
    // ray's azimuth, so neighbouring fingers pick up different blues as
    // the wobbly sheet tilts — the film reads 3D, not painted.
    vec3 rd = reflect(-V, N);
    float sweepPhase = atan(rd.z, rd.x);
    float sweep = 0.5 + 0.5 * sin(sweepPhase * 3.0 +
                                  vWorld.x * 0.7 + vWorld.z * 0.9);
    vec3 env = mix(uWaterB, uWaterA, sweep);

    float edge = 1.0 - abs(dot(N, V));         // grazing = surface sheen
    float diff = max(dot(N, normalize(uLightDir)), 0.0);
    vec3 H = normalize(V + normalize(uLightDir));
    float spec = pow(max(dot(N, H), 0.0), 64.0);

    // ---- irregular, CRAWLING tearing ----
    // smooth phase warps (NOT per-pixel hash: the crown is only a few
    // hundred pixels around, unfiltered noise would alias to glitter) with
    // two incommensurate frequencies, seeded by world position so every
    // crown tears differently.
    float ang = vAngle * 6.2831853;
    float jitter = sin(ang * 3.0 + vWorld.x * 2.3) * 0.85
                 + sin(ang * 7.0 - vWorld.z * 1.7) * 0.55;
    // finger field: base comb + warped phase; climbs to a second wobble
    // frequency as the sheet climbs. The slow uTime drift makes the tear
    // pattern MIGRATE up the film — the sheet visibly churns.
    float fingers = 0.5 + 0.5 * sin(ang * 22.0 + jitter * 2.2
                                   + vParam * vParam * 4.0 - uTime * 1.6);
    // thickness ripple along the sheet, also slowly crawling
    float grain = 0.5 + 0.5 * sin(ang * 41.0 - jitter * 3.0 - vParam * 5.0
                                  + uTime * 2.2);
    // the sheet is dense at the base and disintegrates toward the rim:
    // the tear contrast DEEPENS with height (fingers pinch off there)
    float tear = mix(0.22, 0.62, smoothstep(0.15, 0.9, vParam));
    float sheet = (1.0 - 0.62 * vParam) * (0.55 + 0.45 * grain);
    sheet *= 1.0 - tear * (1.0 - fingers);
    sheet = max(sheet, 0.0);

    // reflection-dominant film: grazing views show the bright surface sheen,
    // face-on views stay translucent blue. Thin light glints only.
    vec3 col = env * (0.50 + 0.30 * vParam + 0.85 * edge)
             + uLightTint * (0.06 + diff * 0.22 + spec * 0.55);
    // local thickness shading: dense finger cores carry more water (brighter),
    // torn gaps are thinner film (dimmer, more water body through them)
    col *= 0.72 + 0.55 * fingers * vParam + 0.18 * grain;
    // dense white-blue foam COLLAR at the water line (the crown base churns)
    float collar = exp(-pow((vParam - 0.05) * 7.0, 2.0));
    col += vec3(0.72, 0.86, 0.98) * collar * 0.34;
    // clamp below clip: no amount of overlap can saturate a solid white mass
    col = min(col, vec3(0.97, 0.96, 0.95));

    // thin film alpha: transmission-dominant; the foam collar is denser
    float alpha = (0.18 + edge * 0.38) * sheet + collar * 0.30;
    fragColor = vec4(col, alpha);
}
