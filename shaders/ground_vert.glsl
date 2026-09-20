#version 120
// Ground plane vertex shader (GLSL 1.2)
uniform mat4 uInvView;
uniform mat4 uLightMatrix;

varying vec3 vWorldPos;
varying vec4 vShadowCoord;
varying vec3 vViewPos;

void main() {
    vec4 viewPos = gl_ModelViewMatrix * gl_Vertex;
    gl_Position = gl_ProjectionMatrix * viewPos;
    vec4 worldPos = uInvView * viewPos;
    vWorldPos = worldPos.xyz;
    vShadowCoord = uLightMatrix * worldPos;
    vViewPos = viewPos.xyz;
}
