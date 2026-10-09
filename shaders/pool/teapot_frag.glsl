#version 330 core


in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform sampler2D uBaseColor;
uniform float uWetness;
uniform float uWaterLine;
uniform vec3  uWaterBody;
uniform float uTime;

out vec4 fragColor;


float D_GGX(float NoH, float a2) {
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * d * d);
}
float V_SmithGGX(float NoV, float NoL, float a2) {
    float a = sqrt(a2);
    float gv = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float gl = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-4);
}
float F_Schlick(float u, float F0) {
    float f = pow(1.0 - u, 5.0);
    return F0 + (1.0 - F0) * f;
}

vec3 ACESFilm(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uEyePos - vWorld);
    vec3 L = normalize(uLightDir);

    float below = clamp((uWaterLine - vWorld.y) * 6.0, 0.0, 1.0);


    vec2 uv = vUV;
    if (below > 0.0) {
        vec3 bend = normalize(vec3(V.x, 0.0, V.z) + vec3(0.0, -1.1, 0.0));
        uv += bend.xz * below * 0.035;
    }

    vec3 copperF0 = texture(uBaseColor, uv).rgb;
    copperF0 = pow(copperF0, vec3(2.2));


    float wet = clamp(uWetness + below * 0.45, 0.0, 1.0);
    float rough = mix(0.42, 0.26, wet);
    float aGGX = rough * rough;
    float a2 = aGGX * aGGX;

    vec3 H = normalize(V + L);
    float NdH = max(dot(N, H), 0.0);
    float NoV = clamp(dot(N, V), 1e-3, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float dGGX = D_GGX(NdH, a2) * V_SmithGGX(NoV, NoL, a2) * 4.0;
    float diff = NoL;


    vec3 T = normalize(cross(N, vec3(0.0, 0.0, 1.0)));
    float TdV = abs(dot(T, V));
    float streak = 0.78 + 0.22 * sin(vWorld.y * 46.0 + vUV.x * 9.0 + uTime * 0.6);
    float anisoSheen = pow(TdV, 6.0) * wet * 0.14 * streak;


    float spec = min(dGGX, 4.5);
    float specK = mix(0.05, 0.12, wet) * streak;
    vec3 metalSpec = copperF0 * F_Schlick(NoV, 1.0) * spec * specK;


    vec3 skyA = vec3(0.92, 0.92, 0.90);
    vec3 skyB = vec3(0.55, 0.012, 0.014);
    vec3 ambient = mix(skyB * 0.085, skyA * 0.115, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    float horizBand = 1.0 - abs(N.y);
    ambient += mix(skyA, skyB, 0.5) * horizBand * 0.05;


    vec3 col = copperF0 * (ambient + uLightTint * diff * 0.80) * 0.06
             + uLightTint * metalSpec
             + uLightTint * anisoSheen * copperF0 * 0.5;


    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);
    col += uLightTint * fres * copperF0 * mix(0.10, 0.28, wet);


    vec2 cp = vWorld.xz * 3.1;
    float web1 = 0.5 + 0.5 * sin(cp.x + sin(cp.y * 1.7 + uTime * 1.9) * 1.4);
    float web2 = 0.5 + 0.5 * sin(cp.y * 1.3 - uTime * 1.4 +
                                 sin(cp.x * 1.9 - uTime * 0.8) * 1.4);
    float caustic = pow(web1 * web2, 3.0);
    col += uLightTint * caustic * below * 0.20;


    col = mix(col, col * (uWaterBody * 2.4), below * 0.75);


    float foamBand = exp(-pow((uWaterLine - vWorld.y) * 9.0, 2.0));
    col += vec3(0.9, 0.95, 1.0) * foamBand * wet * 0.30;


    float meniscus = exp(-pow((vWorld.y - uWaterLine) * 12.0, 2.0));
    col += uLightTint * meniscus * wet * 0.10;


    col = ACESFilm(col);
    col = clamp(col, 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));

    fragColor = vec4(col, 1.0);
}
