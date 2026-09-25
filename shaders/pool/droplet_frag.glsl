#version 330 core
// Round, glassy droplet sprite: bright rim, faint core, hidden-light glint.

in vec2 vUV;
in float vBright;

uniform vec3 uLightTint;

out vec4 fragColor;

void main() {
    float r = length(vUV);
    if (r > 1.0) discard;
    // soft sphere-ish falloff: bright rim, translucent centre
    float rim = smoothstep(0.55, 1.0, r);
    float core = 1.0 - smoothstep(0.0, 0.75, r);
    float alpha = (rim * 0.85 + core * 0.30) * vBright;
    vec3 col = uLightTint * (0.55 + rim * 0.75 + core * 0.35);
    // premultiplied-ish output, straight alpha blend is enabled
    fragColor = vec4(col, alpha);
}
