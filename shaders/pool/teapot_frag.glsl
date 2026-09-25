#version 330 core
// Pool scene teapot: Blinn-Phong lit ONLY by the hidden light (there is no
// sun in this scene), plus a soft ambient bounce from the glowing checker
// sky. map_Kd comes from teapot.mtl via the placeholder texture.
//
// Realism pass:
//   * Schlick Fresnel — grazing-angle sheen like real glazed ceramic,
//   * a reflective waterline band: while the pot bobs, the submerged rim
//     catches the blue pool body + a foam ring right at the water line,
//   * subtle height-based darkening below the waterline (wet ceramic).

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform sampler2D uBaseColor;
uniform float uWetness;    // 0 = bone dry (in the air), 1 = soaked (after splash)
uniform float uWaterLine;  // world-space y of the water surface
uniform vec3  uWaterBody;  // blue pool-water body colour for submerged parts
uniform float uReflect;    // 1 when rendering the MIRRORED fleet into the water

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

    // ambient: red bounce from the glowing checker ceiling + darker floor
    vec3 ambient = mix(vec3(0.030, 0.010, 0.011), vec3(0.090, 0.078, 0.075),
                       clamp(N.y * 0.5 + 0.5, 0.0, 1.0));

    vec3 col = base * (ambient + uLightTint * diff * 1.15)
             + uLightTint * spec;

    // Fresnel rim: glazed ceramic gets a grazing-angle sheen off the sky
    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);
    col += uLightTint * fres * 0.22;

    // wet glaze: push contrast a touch once the teapot has been dunked
    col = mix(col, col * 1.06 + uLightTint * 0.02, uWetness * 0.35);

    // ---- waterline: submerged shell reads as underwater, foam at the rim --
    float below = clamp((uWaterLine - vWorld.y) * 6.0, 0.0, 1.0);
    // foam hugs the water line (a few cm band), fresher right after the splash
    float foamBand = exp(-pow((uWaterLine - vWorld.y) * 9.0, 2.0));
    col = mix(col, col * 0.55 + uWaterBody * 0.9, below * 0.85);       // submerged tint
    col += vec3(0.9, 0.95, 1.0) * foamBand * uWetness * 0.30;          // waterline foam
    // a wet meniscus shine just above the line
    float meniscus = exp(-pow((vWorld.y - uWaterLine) * 12.0, 2.0));
    col += uLightTint * meniscus * uWetness * 0.18;

    col = col / (col + vec3(0.35));
    col = pow(col, vec3(1.0 / 2.2));

    float alpha = 1.0;
    if (uReflect > 0.5) {
        // The reflected pot is seen THROUGH rippling water: the pool body
        // bleeds into it, it dims, and its alpha shimmers line by line so the
        // mirror image breaks up exactly where the surface is disturbed.
        col *= 0.60;
        col += uWaterBody * 0.30;
        alpha = 0.55 + 0.12 * sin(vWorld.x * 8.0 + vWorld.z * 6.0);
    }
    fragColor = vec4(col, alpha);
}
