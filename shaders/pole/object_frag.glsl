#version 330 core
// SCENE 4 (POWER LINES) object shader: everything solid in the scene —
// utility poles, crossarms, the tangle of wires, the ground plane. One
// lighting model for all of them:
//   * the low orange sun (same direction as the sky's bloom) gives warm
//     diffuse with a soft wrap term, so silhouettes keep a trace of form
//   * sky-coloured ambient lifted toward the horizon cream
//   * a cheap specular sheen (wires glint where they face the sun)
//   * distance haze toward the horizon cream (aerial perspective)
// Per-vertex colour + a per-object flat tint carry the materials:
//   creosote-dark poles, greyer old wood, dark cable rubber, pale gravel.

in vec3 vWorld;
in vec3 vNormal;
in vec3 vColor;

out vec4 fragColor;

uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform float uTime;      // reserved (subtle cable sway shading)

const float PI = 3.14159265359;

void main() {
    vec3 N = normalize(vNormal);
    // two-sided: cables and single-sided quads read from any angle
    if (dot(N, normalize(uEyePos - vWorld)) < 0.0) N = -N;

    vec3 L = normalize(uSunDir);
    vec3 V = normalize(uEyePos - vWorld);

    // warm wrap diffuse: the low sun barely catches vertical surfaces, so
    // silhouettes keep a whisper of form. Levels: dark creosote must land
    // ~0.35-0.55 linear (dark against the bright sky), gravel ~0.75.
    float NdL = dot(N, L);
    float wrap = clamp((NdL + 0.30) / 1.30, 0.0, 1.0);
    vec3 sunTint = vec3(1.00, 0.62, 0.30);
    vec3 ambient = vec3(0.30, 0.19, 0.13) + vec3(0.10, 0.05, 0.03);   // warm bounce

    vec3 base = vColor;
    // soft sheen: tight-ish lobe scaled by material darkness (rubber cables
    // glint, matte wood barely does)
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 26.0) * (0.30 - 0.22 * clamp(dot(base, vec3(0.333)), 0.0, 1.0));

    vec3 col = base * (ambient + sunTint * wrap * 0.90) + sunTint * spec * 0.9;

    // aerial perspective: distance haze toward the horizon cream — far poles
    // and wires sink into the heat haze, the near tangle stays crisp
    float dist = length(uEyePos - vWorld);
    float haze = 1.0 - exp(-dist * 0.010);
    vec3 hazeCol = vec3(0.99, 0.78, 0.52);
    col = mix(col, hazeCol, haze * 0.55);

    // gentle highlight knee (only compresses the TOP end; dark values pass)
    col = clamp(col, 0.0, 4.0);
    col = col / (col * 0.35 + vec3(0.72));
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
