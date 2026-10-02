#version 330 core
// SCENE 4 (POWER LINES) sky: the vertex shader only forwards the world
// direction; the fragment shader does the whole sky analytically.
// Explicit attribute locations: AMD/NVIDIA do NOT assign locations in
// declaration order the way Mesa does — without a layout the VAO pointers
// can land on the wrong attributes and the whole object pass disappears.

layout(location = 0) in vec3 aPos;  // dome vertices are unit directions

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vDir;

void main() {
    // dome radius is arbitrary (large), direction = normalised position
    vec4 w = uModel * vec4(aPos, 1.0);
    vDir = w.xyz;
    gl_Position = uViewProj * w;
}
