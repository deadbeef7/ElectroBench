#version 330 core
// Elongated water streak: bright core, soft edge — a strand of water, not a
// soap bubble.

in vec2 vUV;
in float vBright;

uniform vec3 uLightTint;   // warm champagne light, also backlights the strands
uniform vec3 uSkyA;   // white checker tile  (matches the sky dome)
uniform vec3 uSkyB;   // deep red checker tile

out vec4 fragColor;

void main() {
    float r = length(vUV);
    if (r > 1.0) discard;
    // elliptical profile reads as a moving water strand
    float core = 1.0 - smoothstep(0.0, 1.0, r);
    // translucent strand: the old core^2*0.9 alpha stacked dozens of
    // overlapping sprites into framebuffer-saturating solid white domes;
    // real spray stays see-through
    float alpha = (core * core * 0.42 + 0.05) * vBright;
    // Real ejecta is transparent: it PICKS UP the colours behind it (the
    // checker sky), instead of glowing grey. vUV.x runs across the strand,
    // so a slow lateral sweep of the two tile hues sells the reflection.
    float sweep = 0.5 + 0.5 * sin(vUV.x * 9.0 + vBright * 5.0);
    vec3 skyTint = mix(uSkyB, uSkyA, sweep);
    // REFLECTION-LED colour: the sky-tile sweep dominates (the strand is a
    // curved mirror), the warm light is a thin glint — the old light-led
    // mix made dense spray fuse into flat white plastic.
    vec3 col = skyTint * (0.50 + 0.45 * core) + uLightTint * (0.18 + core * 0.14);
    // hard clamp below clip: no amount of sprite overlap can mint a
    // saturated white mass (the "plastic dome" artifact)
    col = min(col, vec3(0.97, 0.96, 0.95));
    fragColor = vec4(col, alpha);
}
