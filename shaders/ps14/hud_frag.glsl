#version 330 core
// HUD text: single-channel bitmap font atlas, tinted pale cyan.

in vec2 vUV;

uniform sampler2D uAtlas;

out vec4 fragColor;

void main() {
    float a = texture(uAtlas, vUV).r;
    a = smoothstep(0.25, 0.75, a);
    if (a < 0.02) discard;
    fragColor = vec4(vec3(0.72, 0.93, 1.0), a * 0.9);
}
