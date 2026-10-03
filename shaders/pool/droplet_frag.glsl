#version 330 core
// Elongated water streak: bright core, soft edge — a strand of POOL WATER,
// not a soap bubble and not a checker tile.
//
// BUILD-D4: near-NEUTRAL glassy water. The old body mix (uWaterB ->
// uWaterA * 1.35) painted every strand saturated cyan at the core —
// thousands of those strands blending over the far fleet WAS the blue
// cloud bank on the horizon. Real backlit spray against a bright room is
// near-white with only a whisper of cyan in the thin edges.

in vec2 vUV;
in float vBright;

uniform vec3 uLightTint;   // warm champagne light, a thin glint on strands
uniform vec3 uWaterA;      // bright pool-water surface blue
uniform vec3 uWaterB;      // deep pool-water body blue

out vec4 fragColor;

void main() {
    float r = length(vUV);
    if (r > 1.0) discard;
    // elliptical profile reads as a moving water strand
    float core = 1.0 - smoothstep(0.0, 1.0, r);
    // translucent strand: real spray stays see-through; lower base so
    // distant stacks cannot fuse into a veil
    float alpha = (core * core * 0.40 + 0.03) * vBright;
    // near-neutral glass: thin cyan edge (deep body), bright neutral core
    vec3 body = mix(vec3(0.62, 0.68, 0.70), uWaterB, core * 0.35);
    vec3 col = mix(body, vec3(0.90, 0.94, 0.96), core * 0.55)
             + uLightTint * (0.10 + core * 0.10);
    // BUILD-P15: A WATER DROP IS A LENS, NOT A COTTON BALL. The old profile
    // was one soft gaussian, so every strand read as the same fuzzy smudge at
    // every size — which is most of why the spray curtain looked painted
    // rather than thrown. A real airborne drop has (a) a DARK limb where the
    // surface curves away from the light and total-internal-reflection sends
    // the ray back down, and (b) a hard off-centre glint, the same caustic
    // hotspot every photograph of thrown water shows. Those two features are
    // what make a few hundred small sprites read as water.
    float limb = smoothstep(0.55, 0.98, r) * (1.0 - smoothstep(0.98, 1.0, r));
    col *= 1.0 - 0.42 * limb;
    // the hotspot rides the upper-left of the strand, as the sun is high
    // and behind the camera in this room
    float hx = vUV.x * 0.55 + 0.42, hy = vUV.y * 0.55 - 0.40;
    float glint = exp(-dot(vec2(hx, hy), vec2(hx, hy)) * 26.0);
    col += uLightTint * glint * 0.85 * vBright;
    alpha += glint * 0.35 * vBright;
    // hard clamp below clip: no amount of sprite overlap can mint a
    // saturated white mass (the "plastic dome" artifact)
    col = min(col, vec3(0.97, 0.96, 0.95));
    fragColor = vec4(col, clamp(alpha, 0.0, 1.0));
}
