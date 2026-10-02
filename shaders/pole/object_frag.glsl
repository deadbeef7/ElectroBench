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
    // BUILD-P2 ground patchiness: a flat single-colour quad is the cheapest
    // CGI tell in the scene. Two smooth world-space octaves modulate the
    // gravel albedo (dust paths, damp patches); only downward normals.
    if (N.y > 0.9) {
        float p1 = sin(vWorld.x * 0.35 + sin(vWorld.z * 0.21) * 1.7)
                 * sin(vWorld.z * 0.27 + sin(vWorld.x * 0.17) * 2.1);
        float p2 = sin(vWorld.x * 1.9 + vWorld.z * 1.3)
                 * sin(vWorld.z * 2.3 - vWorld.x * 0.7);
        base *= 0.78 + 0.22 * p1 + 0.10 * p2;
    }
    // soft sheen: tight-ish lobe scaled by material darkness (rubber cables
    // glint, matte wood barely does)
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 26.0) * (0.30 - 0.22 * clamp(dot(base, vec3(0.333)), 0.0, 1.0));
    // BUILD-P2 grazing rim glint: with the sun ahead, every wire edge catches
    // a hair-thin highlight — the single strongest "real cables" tell.
    float edge = 1.0 - abs(dot(N, V));
    spec += pow(edge, 8.0) * 0.50;

    vec3 col = base * (ambient + sunTint * wrap * 0.90 + vec3(0.16, 0.08, 0.04))
             + sunTint * spec * 0.9;

    // aerial perspective: distance haze toward the horizon cream — far poles
    // and wires sink into the heat haze, the near tangle stays crisp.
    // BUILD-P2 DETAIL FALLOFF: the old single exp was still eating everything
    // past ~80 m ("poles not being rendered"). Two-scale haze: slow global
    // ramp + a modest near-fade so the vanishing corridor reads through.
    float dist = length(uEyePos - vWorld);
    float haze = 1.0 - exp(-dist * 0.0042);
    haze += 0.18 * (1.0 - exp(-dist * 0.028));
    vec3 hazeCol = vec3(0.99, 0.78, 0.52);
    col = mix(col, hazeCol, clamp(haze * 0.55, 0.0, 0.85));

    // BUILD-P2: analytic touch — poles and wires are THROUGH-SEEN: their
    // silhouettes carry a faint warm veil of the glowing haze behind them
    // (thin dark shapes over a bright sky never read as pitch black).
    float silh = clamp(1.0 - dot(base, vec3(0.333)) * 2.6, 0.0, 1.0);
    col = mix(col, vec3(0.52, 0.30, 0.14), silh * 0.18);

    // gentle highlight knee (only compresses the TOP end; dark values pass)
    col = clamp(col, 0.0, 4.0);
    col = col / (col * 0.35 + vec3(0.72));
    col = pow(max(col, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
