#version 330 core
// Pool scene HUD: same shared 8x8 atlas as the other scenes, tinted
// aqua-cyan to match the pool-room palette.

in vec2 vUV;

uniform sampler2D uAtlas;

out vec4 fragColor;

void main() {
    float a = texture(uAtlas, vUV).r;
    a = smoothstep(0.25, 0.75, a);
    if (a < 0.02) discard;
    fragColor = vec4(vec3(0.66, 0.92, 0.98), a * 0.9);
}
