#version 330 core
// SCENE 4 (POWER LINES) object vertex shader: world position, normal,
// per-vertex colour. The CPU builds poles/wires/ground with normalised
// cylinder frames and per-vertex material colours.

in vec3 aPos;
in vec3 aNormal;
in vec3 aColor;

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
