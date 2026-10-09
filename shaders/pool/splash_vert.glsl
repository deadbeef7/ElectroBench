#version 330 core


layout(location = 0) in vec2 aAngleH;
layout(location = 1) in vec2 aScale;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform vec3 uCamPos;
uniform float uRadius;
uniform float uHeight;
uniform float uTime;
uniform float uSpike;
uniform float uPhase;

out vec3 vWorld;
out vec3 vNormal;
out float vParam;
out float vAngle;

float hash(float n) {
    return fract(sin(n * 127.1) * 43758.5453);
}


float spikeField(float a, float t) {


    float v = 0.0;
    v += sin(a * 6.2831853 * 8.0 + t * 0.7) * 0.45;
    v += sin(a * 6.2831853 * 13.0 - t * 1.1) * 0.35;
    v += sin(a * 6.2831853 * 21.0 + t * 1.7) * 0.30;
    v += sin(a * 6.2831853 * 34.0 - t * 2.3) * 0.12;
    v += sin(a * 6.2831853 * 5.0 + t * 0.35) * 0.18;


    v += 0.35 * max(v, 0.0) * max(v, 0.0);
    return v;
}


float radiusWobble(float a, float t, float phase) {
    return 1.0 + 0.18 * sin(a * 6.2831853 * 2.0 + phase * 3.1 + t * 0.9)
              + 0.10 * sin(a * 6.2831853 * 3.0 - phase * 1.7 - t * 1.4);
}

void main() {
    float ang = aAngleH.x * 6.2831853;
    float hp = aAngleH.y;


    float tear = smoothstep(0.10, 0.92, hp);
    tear = tear * tear;


    float spikes = spikeField(aAngleH.x, uTime + uPhase);
    spikes += uSpike * 0.35 * sin(aAngleH.x * 6.2831853 * 26.0 +
                                  uTime * 2.3 + uPhase * 1.3);
    float hMul = 1.0 + uSpike * spikes * hp * tear;


    float rMul = (1.0 + uSpike * 0.30 * spikes * hp * tear)
               * (1.0 - uSpike * 0.18 * hp)
               * radiusWobble(aAngleH.x, uTime + uPhase, uPhase)
               * (1.0 - 0.30 * hp + 0.38 * hp * hp);

    vec3 pos = uCenter + vec3(cos(ang) * uRadius * rMul,
                              uHeight * hp * hMul,
                              sin(ang) * uRadius * rMul);


    vec3 radial = normalize(vec3(cos(ang), 0.0, sin(ang)));

    float e = 0.004;
    float s1 = spikeField(aAngleH.x - e, uTime + uPhase);
    float s2 = spikeField(aAngleH.x + e, uTime + uPhase);
    float grad = (s2 - s1) / (2.0 * e * 6.2831853);
    vec3 tang = normalize(vec3(-sin(ang), grad * uSpike * hp * tear, cos(ang)));
    vec3 n = normalize(cross(radial, tang));

    vWorld = pos;
    vNormal = n;
    vParam = hp;
    vAngle = aAngleH.x;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
