#version 330 core


in vec3 vWorld;
in vec3 vNormal;
in float vParam;
in float vAngle;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uWaterA;
uniform vec3 uWaterB;
uniform vec3 uTileA;
uniform vec3 uTileB;
uniform float uTime;
uniform float uJet;
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


float checker(vec2 p) {
    vec2 w = fwidth(p) + 1e-4;
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5)
                  - abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}
vec3 poolEnv(vec3 dir) {
    float up = clamp(dir.y, -1.0, 1.0);
    if (up > 0.0) {


        vec2 plane = vec2(atan(dir.x, dir.z), asin(up)) * 4.0;
        float c = checker(plane);
        vec3 albedo = mix(uTileB, uTileA, c);
        float fres = 0.35 + 0.65 * pow(1.0 - up, 1.5);
        return albedo * fres;
    }


    if (up > -0.22) {

        float horiz = clamp(1.0 + up * 4.5, 0.0, 1.0);
        vec3 albedo = vec3(0.62, 0.64, 0.65);
        return albedo * (0.55 + 0.45 * horiz);
    }


    return mix(vec3(0.60, 0.72, 0.76), vec3(0.14, 0.32, 0.40),
               clamp(-up * 1.35 - 0.18, 0.0, 1.0));
}

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    vec3 N = normalize(vNormal);
    float camDist = length(uEyePos - vWorld);


    float f1 = sin(vWorld.x * 9.1 + uTime * 3.1 + vWorld.z * 7.3)
             + sin(vWorld.z * 11.7 - uTime * 2.3 + vParam * 6.0);
    float f2 = sin(vWorld.x * 23.7 - uTime * 4.7 + vWorld.z * 19.1);
    float finePhase = vAngle * 6.2831853 * 87.0 + uTime * 5.0
                    + vWorld.x * 3.0 + vWorld.z * 2.0;
    float fineGate = 1.0 - smoothstep(0.4, 1.2, fwidth(finePhase));
    float fine = sin(finePhase) * fineGate;


    float microAmp = 0.55 + 0.45 * exp(-camDist * 0.008);
    vec3 micro = vec3(f1 * 0.20 + f2 * 0.10 + fine * 0.09, 0.0,
                      f1 * 0.14 - f2 * 0.09 + fine * 0.16) * microAmp;
    vec3 Np = normalize(N + micro);


    vec3 rd = reflect(-V, Np);
    vec3 rt = refract(-V, Np, 0.75);

    if (dot(rt, rt) < 1e-4) rt = -V;

    vec3 envMirror = poolEnv(rd);
    vec3 envRefr   = poolEnv(rt);


    float thick = 1.0 - vParam;


    float edge = 1.0 - abs(dot(Np, V));

    float diff = max(dot(Np, normalize(uLightDir)), 0.0);
    vec3 H = normalize(V + normalize(uLightDir));


    float ang = vAngle * 6.2831853;
    float jitter = sin(ang * 3.0 + vWorld.x * 2.3) * 0.85
                 + sin(ang * 7.0 - vWorld.z * 1.7) * 0.55;


    float fingers = 0.5 + 0.5 * sin(ang * 22.0 + jitter * 2.2
                                   + vParam * vParam * 4.0 - uTime * 1.6);

    float grain = 0.5 + 0.5 * sin(ang * 41.0 - jitter * 3.0 - vParam * 5.0
                                  + uTime * 2.2);


    float tear = mix(0.10, 0.62, smoothstep(0.55, 1.0, vParam));
    float sheet = (1.0 - 0.62 * vParam) * (0.55 + 0.45 * grain);
    sheet *= 1.0 - tear * (1.0 - fingers);
    sheet = max(sheet, 0.0);


    sheet = smoothstep(0.18, 0.62, sheet);


    sheet = smoothstep(0.30 - 0.15 * clamp(camDist / 90.0, 0.0, 1.0),
                       0.75, sheet);


    float foam = (1.0 - fingers) * tear * smoothstep(0.45, 0.95, vParam);
    foam = clamp(foam * 1.3, 0.0, 1.0);


    vec3 L = normalize(uLightDir);
    float NoV = clamp(dot(Np, V), 1e-3, 1.0);
    float NoL = max(dot(Np, L), 0.0);
    float fres = F_Schlick(NoV, 0.02);
    fres = clamp(fres * 1.6, 0.02, 0.85);


    float aGGX = 0.18;
    float a2 = aGGX * aGGX;
    float NoH = max(dot(Np, H), 0.0);
    float dGGX = D_GGX(NoH, a2) * V_SmithGGX(NoV, NoL, a2) * F_Schlick(NoH, 0.02);
    float glint = min(dGGX * 2.2, 1.8);


    float sparkle = pow(max(dot(Np, H), 0.0), 300.0) * fineGate * microAmp;


    vec3 absorb = vec3(0.10, 0.04, 0.03);

    vec3 filmTint = exp(-absorb * thick * 2.4);


    filmTint *= 0.85 + 0.30 * (0.5 + 0.5 * sin(thick * 17.0 + f1 * 2.3
                                                + uTime * 2.9));

    vec3 transmitted = envRefr * (0.55 + 0.45 * vParam) * filmTint;


    float band = 0.5 + 0.5 * sin(thick * 28.0 + f1 * 1.5 + vParam * 9.0);
    transmitted *= 0.80 + 0.40 * band;
    vec3 mirrored   = envMirror * 1.15;


    float mlum = dot(mirrored, vec3(0.299, 0.587, 0.114));
    mirrored = mix(mirrored, vec3(mlum), 0.18);
    vec3 col = mix(transmitted, mirrored, fres * (0.45 + 0.55 * edge));


    col = mix(col, vec3(0.94, 0.97, 1.0), foam * 0.75);

    col += uLightTint * sparkle * 1.6;


    float streak = 0.5 + 0.5 * sin(ang * 6.2831853 * 34.0 + jitter * 5.0
                                  + vParam * 3.0 - uTime * 2.6);
    col *= 0.72 + 0.34 * fingers * (0.35 + 0.65 * vParam)
         + 0.14 * streak;


    col += uLightTint * pow(edge, 3.0) * 0.30;


    col += uLightTint * (0.05 + diff * 0.16);
    col += uLightTint * glint * (0.30 + 0.40 * edge);


    col *= 0.72 + 0.55 * fingers * vParam + 0.18 * grain;

    float collar = exp(-pow((vParam - 0.05) * 7.0, 2.0));
    col += vec3(0.84, 0.92, 0.98) * collar * 0.28;


    float beadBand = smoothstep(0.55, 0.95, vParam) * smoothstep(0.35, 0.75, fingers);
    float beads = pow(max(sin(ang * 6.2831853 * 9.0 + jitter * 3.0 + uTime * 1.3), 0.0), 6.0);
    col += vec3(0.85, 0.93, 1.0) * beadBand * beads * 0.85;


    float burst = smoothstep(0.80, 1.0, vParam) * smoothstep(0.45, 0.85, fingers);
    float streakPhase = ang * 6.2831853 * 17.0 + jitter * 5.0 + uTime * 6.0;
    float streaks = pow(max(sin(streakPhase), 0.0), 14.0)
                  * pow(max(sin(streakPhase * 0.53 + 1.7), 0.0), 6.0);
    col += vec3(0.96, 0.98, 1.0) * burst * streaks * 1.2;


    float dropletField = pow(max(sin(ang * 6.2831853 * 31.0 - uTime * 9.0
                                        + jitter * 2.0), 0.0), 24.0);
    col += vec3(1.0) * burst * dropletField * 0.8;

    col = min(col, vec3(0.97, 0.96, 0.95));


    float alpha = mix(0.035 + 0.15 * thick, 0.44 + 0.26 * edge, fres);
    alpha = mix(alpha, 0.88, foam * 0.7);


    alpha *= 1.0 - 0.55 * smoothstep(9.0, 17.0, camDist);
    alpha = clamp(alpha + burst * streaks * 0.5 + burst * dropletField * 0.35, 0.0, 1.0);
    alpha *= sheet;
    alpha += collar * 0.30;
    alpha += edge * (1.0 - thick) * 0.15;
    alpha += beadBand * beads * 0.25;


    if (uJet > 0.5) {

        float jetFade = (1.0 - smoothstep(9.0, 17.0, camDist)) * 0.55
                      + 0.45;
        vec3 jcol = mix(envMirror * 1.25, envRefr * 0.8, fres * 0.45);
        jcol += uLightTint * glint * 0.9 + uLightTint * sparkle;
        float aerate = 0.35 + 0.65 * grain;
        jcol = mix(jcol, vec3(0.92, 0.96, 1.0), (1.0 - aerate) * 0.55);


        float flutter = 0.78 + 0.22 * sin(vParam * 34.0 - uTime * 11.0
                                          + jitter * 4.0);
        jcol *= flutter;
        float jalpha = (0.34 + 0.26 * edge) * aerate * jetFade * flutter;

        fragColor = vec4(jcol, clamp(jalpha, 0.0, 1.0));
        return;
    }
    fragColor = vec4(col, alpha);
}
