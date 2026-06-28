#version 120
// Vertex Shader
varying vec2 vTexCoord;
varying vec3 vFakeViewDir;

void main() {
    gl_Position = ftransform();
    vTexCoord = gl_MultiTexCoord0.xy;
    vFakeViewDir = normalize(vec3(0.0, 0.0, 1.0)); // View from front
}
