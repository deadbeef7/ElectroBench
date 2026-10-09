#version 330 core


layout(location = 0) in vec3 aPos;

uniform mat4 uViewProj;
uniform mat4 uModel;

out vec3 vDir;

void main() {

    vec4 w = uModel * vec4(aPos, 1.0);
    vDir = w.xyz;
    gl_Position = uViewProj * w;
}
