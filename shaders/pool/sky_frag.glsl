#version 330 core


in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uEyePos;
uniform vec3 uCenter;
uniform float uRadius;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform float uTime;

out vec4 fragColor;

const float PI = 3.14159265358979;

float checker(vec2 p) {


    vec2 w = fwidth(p) + 1e-4;
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5)
                  - abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}

void main() {


    vec3 dir = normalize(vNormal);
    float up = clamp(dir.y, -1.0, 1.0);


    vec2 plane = vec2(atan(dir.x, dir.z), asin(clamp(up, -1.0, 1.0))) * 4.0;
    float c = checker(plane + vec2(uTime * 0.006, 0.0));


    vec3 tileA = vec3(2.30, 2.30, 2.26);
    vec3 tileB = vec3(1.50, 0.008, 0.010);
    vec3 albedo = mix(tileB, tileA, c);


    float foot = max(fwidth(plane.x), fwidth(plane.y));
    float grout = (1.0 - smoothstep(0.06, 0.34, foot))
                * smoothstep(0.10, 0.0, abs(c - 0.5));
    albedo *= mix(1.0, 0.62, grout);


    vec3 Ld = normalize(uLightDir);
    float toLight = clamp(dot(dir, Ld), 0.0, 1.0);
    float wash = pow(toLight, 1.6);


    float horiz = 1.0 - smoothstep(0.0, 0.42, abs(up));

    vec3 col = albedo * uLightTint * (0.85 + 0.55 * wash);
    col += uLightTint * 0.05 * horiz;


    vec2 wuv = plane * 20.0;
    float q1 = 0.5 + 0.5 * sin(wuv.x + sin(wuv.y * 0.62 + uTime * 1.15) * 1.9);
    float q2 = 0.5 + 0.5 * sin(wuv.y * 0.74 - uTime * 0.95
                              + sin(wuv.x * 0.68 - uTime * 0.60) * 1.7);
    float caus = pow(smoothstep(0.28, 0.90, q1 * q2), 1.4);


    float cfall = exp(-max(up, 0.0) * 4.2) * horiz;

    cfall *= 0.35 + 0.65 * wash;
    col += uLightTint * albedo * caus * cfall * 0.90;


    vec3 tUp = abs(Ld.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 tx = normalize(cross(tUp, Ld));
    vec3 ty = cross(Ld, tx);
    float cosA = max(dot(dir, Ld), 0.08);
    vec2 q = vec2(dot(dir, tx), dot(dir, ty)) / cosA;
    float half_ = 0.30;
    float box = max(abs(q.x), abs(q.y));


    float edgeT = smoothstep(half_, half_ * 0.55, box);


    float panel = edgeT * (0.62 + 0.38 * smoothstep(half_ * 0.05, half_ * 0.80, edgeT));


    float louvre = 0.5 + 0.5 * cos(q.x * 210.0);
    panel *= 0.86 + 0.14 * louvre;


    panel *= smoothstep(half_ * 1.00, half_ * 0.86, box);

    float mull = smoothstep(0.030, 0.012, abs(q.x))
               + smoothstep(0.030, 0.012, abs(q.y));
    panel *= clamp(1.0 - mull * 0.85, 0.0, 1.0);


    col += uLightTint * panel * 4.2;

    col += uLightTint * smoothstep(half_ * 2.6, half_, box) * 0.30;


    col *= 0.74;
    col = clamp((col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14), 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
