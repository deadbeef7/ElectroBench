#version 120
// Vertex Shader (GLSL 1.2)
// Passes texture coords, view-space position/normal, world position and
// light-space shadow coordinates to the fragment stage.

uniform mat4 uInvView;      // inverse of the camera view matrix
uniform mat4 uLightMatrix;  // bias * lightProj * lightView (world -> [0,1])

varying vec2 vTexCoord;
varying vec3 vViewPos;      // view-space position
varying vec3 vNormalView;   // view-space normal (includes per-object rotation)
varying vec3 vWorldPos;     // world-space position
varying vec4 vShadowCoord;  // light-space position for shadow map lookup

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
