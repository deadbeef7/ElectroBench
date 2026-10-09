#version 330 core


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


    vec3 rd = dir;
    if (rd.y < 0.0) rd = normalize(vec3(dir.x, -dir.y, dir.z));
    vec3 c = texture(uEnvMap, rd).rgb;


    c = max(c, vec3(0.0));
    c = c / (c + vec3(1.0));
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float warmStart = 0.45;
    if (lum > warmStart) {
        float f = clamp((lum - warmStart) / 0.20, 0.0, 1.0);
        f = f * f;
        vec3 warm = vec3(1.0, 0.62, 0.36);
        warm *= lum / max(dot(warm, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
        c = mix(c, warm, f);
    }
    c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
    fragColor = vec4(c, 1.0);
}
