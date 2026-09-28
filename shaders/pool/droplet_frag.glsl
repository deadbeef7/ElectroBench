#version 330 core
// Elongated water streak: bright core, soft edge — a strand of POOL WATER,
// not a soap bubble and not a checker tile.
//
// HYPER-REAL PASS: ejecta picks up the water's own colours — deep blue at
// the thin edges, bright surface blue at the dense core — with only a thin
// warm glint from the hidden light. Dense overlapping strands blend toward
// bright blue-white (real spray mass), never saturate to solid white
// (hard clamp below clip).

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
    // translucent strand: real spray stays see-through
    float alpha = (core * core * 0.42 + 0.05) * vBright;
    // water-led colour: deep blue edges, bright surface-blue core, thin
    // warm glint. A slight lateral sweep keeps strands from looking stamped.
    float sweep = 0.5 + 0.5 * sin(vUV.x * 9.0 + vBright * 5.0);
    vec3 body = mix(uWaterB, uWaterA, sweep);
    vec3 col = mix(body, uWaterA * 1.35, core * 0.6)
             + uLightTint * (0.10 + core * 0.12);
    // hard clamp below clip: no amount of sprite overlap can mint a
    // saturated white mass (the "plastic dome" artifact)
    col = min(col, vec3(0.97, 0.96, 0.95));
    fragColor = vec4(col, alpha);
}
