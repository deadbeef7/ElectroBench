#version 330 core
// Elongated water streak: bright core, soft edge — a strand of water, not a
// soap bubble.

in vec2 vUV;
in float vBright;

uniform vec3 uLightTint;

out vec4 fragColor;

void main() {
    float r = length(vUV);
    if (r > 1.0) discard;
    // elliptical profile reads as a moving water strand
    float core = 1.0 - smoothstep(0.0, 1.0, r);
    float alpha = (core * core * 0.9 + 0.08) * vBright;
    vec3 col = uLightTint * (0.85 + core * 0.45);
    fragColor = vec4(col, alpha);
}
