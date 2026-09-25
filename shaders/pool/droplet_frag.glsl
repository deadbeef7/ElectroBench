#version 330 core
// Elongated water streak: bright core, soft edge — a strand of water, not a
// soap bubble.

in vec2 vUV;
in float vBright;

uniform vec3 uLightTint;   // warm champagne light, also backlights the strands
uniform vec3 uSkyA;   // turquoise checker tile  (matches the sky dome)
uniform vec3 uSkyB;   // hot coral checker tile

out vec4 fragColor;

void main() {
    float r = length(vUV);
    if (r > 1.0) discard;
    // elliptical profile reads as a moving water strand
    float core = 1.0 - smoothstep(0.0, 1.0, r);
    float alpha = (core * core * 0.9 + 0.08) * vBright;
    // Real ejecta is transparent: it PICKS UP the colours behind it (the
    // checker sky), instead of glowing grey. vUV.x runs across the strand,
    // so a slow lateral sweep of the two tile hues sells the reflection.
    float sweep = 0.5 + 0.5 * sin(vUV.x * 9.0 + vBright * 5.0);
    vec3 skyTint = mix(uSkyB, uSkyA, sweep);
    vec3 col = uLightTint * (0.55 + core * 0.35) + skyTint * (0.35 + 0.45 * core);
    fragColor = vec4(col, alpha);
}
