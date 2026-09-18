#version 330 core
// Vertex shader for the sky dome (half sphere rendered around the camera).

layout(location = 0) in vec3 aPos;

uniform mat4 uViewProj;

out vec3 vDir;

void main() {
    vDir = aPos; // unit direction from the dome centre
    vec3 pos = aPos * 5.0; // small radius: the dome is only rendered into the env cubemap from its centre
    gl_Position = uViewProj * vec4(pos, 1.0);
}
