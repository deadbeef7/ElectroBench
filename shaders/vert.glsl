#version 120


uniform mat4 uInvView;
uniform mat4 uLightMatrix;

varying vec2 vTexCoord;
varying vec3 vViewPos;
varying vec3 vNormalView;
varying vec3 vWorldPos;
varying vec4 vShadowCoord;

void main() {
    vec4 viewPos = gl_ModelViewMatrix * gl_Vertex;
    gl_Position = gl_ProjectionMatrix * viewPos;
    vTexCoord = gl_MultiTexCoord0.xy;
    vViewPos = viewPos.xyz;
    vNormalView = gl_NormalMatrix * gl_Normal;
    vec4 worldPos = uInvView * viewPos;
    vWorldPos = worldPos.xyz;
    vShadowCoord = uLightMatrix * worldPos;
}
