#version 330 core
// Scene 4 HUD: the shared 8x8 atlas tinted CRT-amber to match the
// power-line sky palette.

in vec2 vUV;

uniform sampler2D uAtlas;

out vec4 fragColor;

void main() {
    float a = texture(uAtlas, vUV).r;
    a = smoothstep(0.25, 0.75, a);
    if (a < 0.02) discard;
    fragColor = vec4(vec3(1.0, 0.86, 0.55), a * 0.9);
}
