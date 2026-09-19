#version 330 core
// Visible sky: reconstruct the view ray per pixel and sample the environment
// cubemap that was just captured from the cloud dome. The sky you see and the
// sky the water reflects are therefore always identical.

in vec2 vNDC;

uniform samplerCube uEnvMap;
uniform vec3 uCamRight;
uniform vec3 uCamUp;
uniform vec3 uCamFwd;
uniform float uTanHalfFov;
uniform float uAspect;

out vec4 fragColor;

void main() {
    vec3 dir = normalize(uCamFwd + uCamRight * (vNDC.x * uTanHalfFov * uAspect) +
                         uCamUp * (vNDC.y * uTanHalfFov));
    // The cubemap only stores the upper hemisphere; mirror below-horizon rays
    // onto the horizon sky so the sliver between the sea's far edge and the
    // horizon line shows haze instead of an empty (black) cube face.
    vec3 rd = dir;
    rd.y = abs(rd.y);
    vec3 c = texture(uEnvMap, rd).rgb;
    c = c / (c + vec3(1.0));                 // same tonemap as the sea shader
    c = pow(c, vec3(1.0 / 2.2));
    fragColor = vec4(c, 1.0);
}
