#version 330 core

// Scene 1 -- the haze cones under the light bars. Additive, depth-tested and
// depth-write-off: cheap stand-in for a scattering integral, with the density
// peaking where the cone is thickest (looking through its middle) and fading
// with distance from the bar.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;      // x = around the cone, y = 0 at the bar, 1 at the floor

out vec4 fragColor;

uniform vec3 uEye;
uniform vec3 uColor;
uniform float uIntensity;
uniform float uTime;

void main() {
    vec3 V = normalize(uEye - vWorldPos);
    float facing = abs(dot(normalize(vNormal), V));

    // brightest looking through the middle of the cone, zero at its silhouette,
    // so the geometry has no visible edge
    float density = pow(facing, 2.4);
    // fade both ends: no bright disc where the cone meets the slab, no hard
    // edge at the bar itself
    float nearBar = smoothstep(0.0, 0.16, vUV.y);
    float nearFloor = 1.0 - smoothstep(0.72, 1.0, vUV.y);
    float toFloor = 1.0 - 0.45 * vUV.y;
    float flicker = 0.94 + 0.06 * sin(uTime * 7.0 + vUV.y * 9.0);

    float a = density * nearBar * nearFloor * toFloor * flicker * uIntensity;
    fragColor = vec4(uColor * a, a);
}
