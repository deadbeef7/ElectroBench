#version 330 core
// Pool scene teapot: Blinn-Phong lit ONLY by the hidden light (there is no
// sun in this scene), plus a soft ambient bounce from the glowing checker
// sky. map_Kd comes from teapot.mtl via the placeholder texture.

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform sampler2D uBaseColor;
uniform float uWetness;   // 0 = bone dry (in the air), 1 = soaked (after splash)

out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uEyePos - vWorld);
    vec3 L = normalize(uLightDir);

    vec3 base = texture(uBaseColor, vUV).rgb;
    base = pow(base, vec3(2.2)); // texture is sRGB; shade in linear

    float diff = clamp(dot(N, L), 0.0, 1.0);
    vec3 H = normalize(V + L);
    float shininess = mix(96.0, 220.0, uWetness);
    float spec = pow(clamp(dot(N, H), 0.0, 1.0), shininess) * mix(0.45, 0.9, uWetness);

    // ambient: cool bounce from the glowing ceiling + darker floor bounce
    vec3 ambient = mix(vec3(0.030, 0.032, 0.040), vec3(0.075, 0.080, 0.095),
                       clamp(N.y * 0.5 + 0.5, 0.0, 1.0));

    // wet glaze: push contrast a touch once the teapot has been dunked
    vec3 col = base * (ambient + uLightTint * diff * 1.15)
             + uLightTint * spec;
    col = mix(col, col * 1.06 + uLightTint * 0.02, uWetness * 0.35);

    col = col / (col + vec3(0.35));
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
