#version 330 core


layout(location = 0) in vec3 aPos;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uRadius;

out vec3 vDir;

void main() {
    vDir = aPos;
    vec3 pos = aPos * uRadius + uCenter;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
