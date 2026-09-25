#version 330 core
// Crown splash mesh: a cylinder sheet around the impact point. The CPU
// uploads the crown's current radius/height/life; the GPU animates the
// jagged spikes (per-angle pseudo-noise, animated by uTime) so the crown
// tears into strands exactly like a real Worthington crown.
//
// Per-vertex layout (stride 9 floats):
//   [0]    angle (0..1 around the crown)
//   [1]    height parameter (0 = water line, 1 = rim)
//   [2..3] radius scale, height scale (unit mesh; uCrown scales it)

layout(location = 0) in vec2 aAngleH;   // angle, heightParam
layout(location = 1) in vec2 aScale;    // radiusScale (1), unused

uniform mat4 uViewProj;
uniform vec3 uCenter;      // impact point
uniform vec3 uCamPos;
uniform float uRadius;     // current crown radius
uniform float uHeight;     // current crown height
uniform float uTime;
uniform float uSpike;      // spike amplitude
uniform float uPhase;      // per-crown animation phase

out vec3 vWorld;
out vec3 vNormal;
out float vParam;
out float vAngle;

float hash(float n) {
    return fract(sin(n * 127.1) * 43758.5453);
}

// smooth pseudo-noise around the ring: 8 fixed spikes + 13 wobble spikes
// (+ a per-crown phase so a fleet of crowns never pulses in lockstep)
float spikeField(float a, float t) {
    float v = 0.0;
    v += sin(a * 6.2831853 * 8.0 + t * 0.7) * 0.55;
    v += sin(a * 6.2831853 * 13.0 - t * 1.1) * 0.30;
    v += sin(a * 6.2831853 * 21.0 + t * 1.7) * 0.15;
    return v;
}

void main() {
    float ang = aAngleH.x * 6.2831853;
    float hp = aAngleH.y; // 0 base .. 1 rim

    // spikes grow toward the rim; the ring wobbles as it expands. The last
    // term adds HIGH-frequency tearing that grows with the spike amplitude:
    // the sheet disintegrates into fingers rather than wobbling smoothly.
    float spikes = spikeField(aAngleH.x, uTime + uPhase);
    spikes += uSpike * 0.35 * sin(aAngleH.x * 6.2831853 * 26.0 +
                                  uTime * 2.3 + uPhase * 1.3);
    float hMul = 1.0 + uSpike * spikes * hp;
    float rMul = 1.0 + uSpike * 0.35 * spikes * hp;

    vec3 pos = uCenter + vec3(cos(ang) * uRadius * rMul,
                              uHeight * hp * hMul,
                              sin(ang) * uRadius * rMul);

    // normal of the wobbly sheet: radial + tilt from the spike gradient
    vec3 radial = normalize(vec3(cos(ang), 0.0, sin(ang)));
    // approximate gradient numerically for a believable normal
    float e = 0.004;
    float s1 = spikeField(aAngleH.x - e, uTime + uPhase);
    float s2 = spikeField(aAngleH.x + e, uTime + uPhase);
    float grad = (s2 - s1) / (2.0 * e * 6.2831853);
    vec3 tang = normalize(vec3(-sin(ang), grad * uSpike * hp, cos(ang)));
    vec3 n = normalize(cross(radial, tang));

    vWorld = pos;
    vNormal = n;
    vParam = hp;
    vAngle = aAngleH.x;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
