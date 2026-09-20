#version 120
// Shadow pass vertex shader (GLSL 1.2) - depth only
void main() {
    gl_Position = ftransform();
}
