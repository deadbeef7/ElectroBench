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
    // The cubemap only stores the upper hemisphere; above-horizon rays sample
    // it directly. Below-horizon rays (the sliver between the sea's far edge
    // and the horizon line) are mirrored across the sea plane onto the opposite
    // azimuth above the horizon so they show sky instead of black.
    vec3 rd = dir;
    if (rd.y < 0.0) rd = normalize(vec3(dir.x, -dir.y, dir.z));
    vec3 c = texture(uEnvMap, rd).rgb;
    c = c / (c + vec3(1.0));                 // same tonemap as the sea shader
    c = pow(c, vec3(1.0 / 2.2));
    fragColor = vec4(c, 1.0);
}
