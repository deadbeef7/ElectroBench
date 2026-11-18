#version 330 core

// Scene 1 -- the visible light bars themselves. HDR output so the shared
// bloom pass can pick them up.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 fragColor;

uniform vec3 uColor;
uniform vec3 uEye;
uniform float uIntensity;

void main() {
    float along = 0.82 + 0.18 * sin(vUV.x * 30.0);
    float edge = 0.75 + 0.25 * abs(dot(normalize(vNormal), normalize(uEye - vWorldPos)));
    fragColor = vec4(uColor * uIntensity * along * edge, 1.0);
}
