#version 330 core
// Crown splash: a thin sheet of water — bright where you see it edge-on, and
// the rim glows in the hidden light. Turbulence noise in the vertex shader
// tears the sheet into the classic jagged crown.

in vec3 vWorld;
in vec3 vNormal;
in float vParam; // v = angle 0..1, w = height 0..1 (base->rim)

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;

out vec4 fragColor;

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    vec3 N = normalize(vNormal);

    // thin water sheet: fresnel edge-on brightness, bright specular rim
    float edge = 1.0 - abs(dot(N, V));
    float diff = max(dot(N, normalize(uLightDir)), 0.0);
    vec3 H = normalize(V + normalize(uLightDir));
    float spec = pow(max(dot(N, H), 0.0), 64.0);

    // fade the sheet as the crown collapses (w also fades it top-down)
    float life = clamp(vParam, 0.0, 1.0);

    vec3 col = uLightTint * (0.30 + edge * 0.85 + diff * 0.35 + spec * 1.6);
    // faint turquoise body so the sheet never reads grey
    col += vec3(0.02, 0.14, 0.20) * (1.0 - edge);
    float alpha = (0.35 + edge * 0.6) * life;

    fragColor = vec4(col, alpha);
}
