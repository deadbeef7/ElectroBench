#version 330 core
// SCENE 4 (POWER LINES) object vertex shader: world position, normal,
// per-vertex colour. The CPU builds poles/wires/ground with normalised
// cylinder frames and per-vertex material colours.
// Explicit attribute locations (0/1/2, matching DrawGrid's VAO pointers):
// AMD/NVIDIA do NOT assign locations in declaration order the way Mesa
// does — without a layout the object pass renders NOTHING (sky/HUD only),
// exactly the "no poles, no nothing" report from the user's AMD box.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vWorld;
out vec3 vNormal;
out vec3 vColor;

void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    // uniform scale + rotation only: normal transform = model's 3x3
    vNormal = mat3(uModel) * aNormal;
    vColor = aColor;
    gl_Position = uViewProj * w;
}
