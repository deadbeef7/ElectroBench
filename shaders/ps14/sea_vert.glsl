#version 330 core
// Sea surface (vertex stage).
//
// The displacement is a spectrally derived wave set: a Phillips spectrum is
// sampled on a jittered polar grid in C++ (16 components with deep-water
// dispersion w = sqrt(g k)) and uploaded as uniforms, so the vertex and
// fragment stages differentiate the SAME field. That is the same data an FFT
// ocean would start from; evaluating the components analytically instead of
// through a transform keeps the normals exact and costs no GPU readback.
//
// Two band limits keep the drawn surface and its normals in agreement:
//
//   * distance: a component whose wavelength drops below a few pixels cannot
//     be drawn without aliasing, so it fades out first;
//   * grid: a component shorter than a few grid cells cannot be displaced at
//     all, so it survives only in the fragment normal, where it never jags.

layout(location = 0) in vec2 aXZ; // world-space XZ position of the grid node
layout(location = 1) in vec2 aUV; // large-scale water UV

uniform mat4  uViewProj;
uniform float uTime;
uniform vec3  uEyePos;
uniform float uGridCell; // world size of one quad (m)

#define WAVE_COUNT 16
uniform vec2  uWaveDir[WAVE_COUNT];
uniform float uWaveK[WAVE_COUNT];
uniform float uWaveAmp[WAVE_COUNT];
uniform float uWaveOmega[WAVE_COUNT];
uniform float uWavePhase[WAVE_COUNT];

out vec3  vWorld;
out vec2  vUV;
out float vDist;

const float kWavePixelAngle = 0.0018; // radians per pixel at the reference FOV

// Resolvability of a component of wavenumber k (rad/m) at this distance.
float bandFade(float k, float dist) {
    float lambda = 6.2831853 / k;
    float pixelFade = smoothstep(1.5, 4.0, lambda / max(dist * kWavePixelAngle, 1e-3));
    float gridFade = smoothstep(2.2, 4.2, lambda / max(uGridCell, 1e-3));
    return pixelFade * gridFade;
}

float waveHeight(vec2 p, float t, float dist) {
    float h = 0.0;
    for (int i = 0; i < WAVE_COUNT; i++) {
        float fade = bandFade(uWaveK[i], dist);
        if (fade <= 0.0) continue;
        float phase = dot(p, uWaveDir[i]) * uWaveK[i] + t * uWaveOmega[i] + uWavePhase[i];
        float c = sin(phase);
        // Slight crest sharpening: real wind waves have narrow crests and wide
        // troughs. The form is monotonic, so no flat-topped mirror plates.
        h += uWaveAmp[i] * fade * (c + 0.10 * c * abs(c));
    }
    return h;
}

void main() {
    vDist = length(aXZ - uEyePos.xz);
    vec3 pos = vec3(aXZ.x, waveHeight(aXZ, uTime, vDist), aXZ.y);
    vWorld = pos;
    vUV = aXZ / 4096.0; // world-anchored UVs so ripples do not swim with the mesh
    gl_Position = uViewProj * vec4(pos, 1.0);
}
