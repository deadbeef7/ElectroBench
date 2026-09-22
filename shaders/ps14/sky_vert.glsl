#version 330 core
// Vertex shader for the sky dome (full sphere, seen from the inside).
// The dome is rendered twice every frame: once into the environment cubemap
// (small radius, dome centred at the origin) and once directly on screen
// (large radius, dome centred at the camera) so the visible sky has full
// screen resolution instead of being magnified out of the low-res cubemap.

layout(location = 0) in vec3 aPos;

uniform mat4 uViewProj;
uniform vec3 uCenter;   // dome centre in world space
uniform float uRadius;  // dome radius (screen pass keeps it inside the far plane)

out vec3 vDir;

void main() {
    vDir = aPos; // unit direction from the dome centre
    vec3 pos = aPos * uRadius + uCenter;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
