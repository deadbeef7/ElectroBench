#version 330 core
// Crown splash: a thin sheet of water erupting around the impact point.
// Physically it is a curved film of water: it both REFLECTS and TRANSMITS
// the environment, so its colour is a Fresnel-weighted blend of the two
// checker tiles sweeping around the ring — it never reads grey.
//
// ANTI-PLASTIC PASS: the old sheet was a smooth film crossed by one even
// sinusoid stripe ring — uniform thickness, uniform brightness, regular
// spacing — which read as moulded plastic. Real crowns disintegrate in
// stages: a dense base, a tearing midsection whose fingers lengthen and
// pinch off at irregular azimuths, and a rim that is already individual
// droplets. The alpha now breaks up with phase-jittered fingers (warped
// by smooth azimuth noise so the spacing is never regular), a thickness
// ripple along the sheet, and a tear term that deepens toward the rim;
// brightness follows the local thickness so the mass reads granular.

in vec3 vWorld;
in vec3 vNormal;
in float vParam; // height parameter 0 (water line) .. 1 (rim)
in float vAngle; // 0..1 around the crown

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uSkyA;   // white checker tile (matches the sky dome)
uniform vec3 uSkyB;   // deep red checker tile

out vec4 fragColor;

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    vec3 N = normalize(vNormal);

    // transmitted environment: the checker tiles behind the sheet. The sweep
    // follows the ACTUAL reflected ray's azimuth (not a fixed ring pattern),
    // so the tile reflections slide correctly as the camera orbits and as
    // the wobbly sheet tilts — neighbouring fingers pick up different tiles
    // because their normals genuinely differ.
    vec3 rd = reflect(-V, N);
    float sweepPhase = atan(rd.z, rd.x);
    float sweep = 0.5 + 0.5 * sin(sweepPhase * 3.0 +
                                  vWorld.x * 0.7 + vWorld.z * 0.9);
    vec3 env = mix(uSkyB, uSkyA, sweep);

    float edge = 1.0 - abs(dot(N, V));         // grazing = mirror-like
    float diff = max(dot(N, normalize(uLightDir)), 0.0);
    vec3 H = normalize(V + normalize(uLightDir));
    float spec = pow(max(dot(N, H), 0.0), 64.0);

    // ---- irregular tearing ----
    // smooth phase warps (NOT per-pixel hash: the crown is only a few
    // hundred pixels around, unfiltered noise would alias to glitter) with
    // two incommensurate frequencies, seeded by world position so every
    // crown tears differently.
    float ang = vAngle * 6.2831853;
    float jitter = sin(ang * 3.0 + vWorld.x * 2.3) * 0.85
                 + sin(ang * 7.0 - vWorld.z * 1.7) * 0.55;
    // finger field: base comb + warped phase; climbs to a second wobble
    // frequency as the sheet climbs, so the rim spacing differs from the base
    float fingers = 0.5 + 0.5 * sin(ang * 22.0 + jitter * 2.2
                                   + vParam * vParam * 4.0);
    // thickness ripple along the sheet so the film never reads uniform
    float grain = 0.5 + 0.5 * sin(ang * 41.0 - jitter * 3.0 - vParam * 5.0);
    // the sheet is dense at the base and disintegrates toward the rim:
    // the tear contrast DEEPENS with height (fingers pinch off there)
    float tear = mix(0.22, 0.62, smoothstep(0.15, 0.9, vParam));
    float sheet = (1.0 - 0.62 * vParam) * (0.55 + 0.45 * grain);
    sheet *= 1.0 - tear * (1.0 - fingers);
    sheet = max(sheet, 0.0);

    // transmitted env + reflection sheen; the thin rim transmits MORE
    // environment (a thinner film hides less behind it) and the rim catches
    // the hidden light in sharp glints
    // reflection-dominant film: at grazing angles a water sheet shows the
    // ROOM it reflects (the red/white checker sweep), never flat white —
    // the old lightTint-heavy stack painted every edge-on crown as a solid
    // white plastic dome. Face-on, the thin film stays translucent.
    vec3 col = env * (0.50 + 0.30 * vParam + 0.85 * edge)
             + uLightTint * (0.06 + diff * 0.22 + spec * 0.55);
    // local thickness shading: dense finger cores carry more water (brighter),
    // torn gaps are thinner film (dimmer, more env through them)
    col *= 0.72 + 0.55 * fingers * vParam + 0.18 * grain;
    // clamp below clip: the crown sheet blends over bright water where many
    // crowns overlap; unclamped it saturated the framebuffer into a solid
    // white dome (the plastic-mould look the user screenshotted)
    col = min(col, vec3(0.97, 0.96, 0.95));

    // thin film alpha: transmission-dominant even at grazing (the reflection
    // TERM above carries the grazing look, not opacity)
    float alpha = (0.18 + edge * 0.38) * sheet;
    fragColor = vec4(col, alpha);
}
