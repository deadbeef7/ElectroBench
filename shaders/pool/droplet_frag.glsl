#version 330 core


in vec2 vUV;
in float vBright;
in float vStretch;

uniform vec3 uLightTint;
uniform vec3 uWaterA;
uniform vec3 uWaterB;

out vec4 fragColor;

void main() {
    float r = length(vUV);
    if (r > 1.0) discard;

    float core = 1.0 - smoothstep(0.0, 1.0, r);


    float smear = 1.0 / vStretch;
    float alpha = (core * core * 0.40 + 0.03) * vBright * smear;

    vec3 body = mix(vec3(0.62, 0.68, 0.70), uWaterB, core * 0.35);
    vec3 col = mix(body, vec3(0.90, 0.94, 0.96), core * 0.55)
             + uLightTint * (0.10 + core * 0.10);
    col *= mix(0.62, 1.0, sqrt(smear));


    float limb = smoothstep(0.55, 0.98, r) * (1.0 - smoothstep(0.98, 1.0, r));
    col *= 1.0 - 0.42 * limb;


    float hx = vUV.x * 0.55 + 0.42, hy = vUV.y * 0.55 - 0.40;
    float glint = exp(-dot(vec2(hx, hy), vec2(hx, hy)) * 26.0);


    float gl2 = exp(-dot(vec2(hx, hy), vec2(hx, hy)) * (26.0 / vStretch));
    glint = mix(glint, gl2, 0.6);
    col += uLightTint * glint * 0.85 * vBright * mix(0.55, 1.0, sqrt(smear));
    alpha += glint * 0.35 * vBright * smear;


    col = min(col, vec3(0.97, 0.96, 0.95));
    fragColor = vec4(col, clamp(alpha, 0.0, 1.0));
}
