#version 120
// Ground plane fragment shader (GLSL 1.2)
// Procedural concrete display floor: subtle tile grid, sun lambert,
// 3x3 PCF shadows from the shadow map, distance fog into the sky color.
uniform sampler2D uShadowMap;
uniform vec3 uSunDirWorld;  // world-space direction towards the sun
uniform vec2 uShadowTexel;
uniform vec3 uSkyColor;
uniform float uShadowDisable; // debug: 1 disables the shadow test

varying vec3 vWorldPos;
varying vec4 vShadowCoord;

float shadowFactor() {
    vec3 p = vShadowCoord.xyz / vShadowCoord.w;
    // uLightMatrix already includes the bias (world -> [0,1] light-space).
    // Do NOT apply the half-offset again: double-biasing squeezed every
    // lookup into the top-right quadrant of the shadow map and scattered
    // the shadows in all directions.
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z >= 1.0)
        return 1.0;
    float bias = 0.0022;
    // rotated 12-tap poisson disk: soft, stable edges instead of aliased
    // stair-step fringes from the plain 3x3 tap grid
    const vec2 pois[12] = vec2[12](
        vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457),
        vec2(-0.203,  0.621), vec2( 0.963, -0.195), vec2( 0.473, -0.480),
        vec2( 0.519,  0.767), vec2( 0.185, -0.893), vec2( 0.507,  0.064),
        vec2( 0.896,  0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));
    float ang = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.28318;
    vec2 dir = vec2(cos(ang), sin(ang));
    mat2 rot = mat2(dir.x, -dir.y, dir.y, dir.x);
    float sum = 0.0;
    for (int i = 0; i < 12; i++) {
        vec2 off = rot * pois[i] * uShadowTexel * 2.0;
        sum += step(p.z - bias, texture2D(uShadowMap, p.xy + off).r);
    }
    return sum / 12.0;
}

void main() {
    // concrete tiles: large slabs with thin darker seams
    float tile = 1.5;
    vec2 g = abs(fract(vWorldPos.xz / tile) - 0.5);
    float seam = smoothstep(0.47, 0.5, max(g.x, g.y));
    float grain = fract(sin(dot(floor(vWorldPos.xz * 4.0), vec2(12.9898, 78.233))) * 43758.5453);
    vec3 floorColor = mix(vec3(0.30, 0.29, 0.28), vec3(0.24, 0.235, 0.23), seam);
    floorColor *= 0.9 + grain * 0.2;

    float NdotL = max(uSunDirWorld.y, 0.0);
    float shadow = mix(shadowFactor(), 1.0, uShadowDisable);

    vec3 sunColor = vec3(1.0, 0.93, 0.82);
    // very strong sun / minimal ambient so the gun shadows really stand out
    vec3 lighting = floorColor * (sunColor * (NdotL * 2.0 * shadow) +
                                  vec3(0.10, 0.11, 0.14));

    float r = length(vWorldPos.xz);
    float fog = smoothstep(9.0, 22.0, r);

    // match the object shader tonemap + gamma
    vec3 color = lighting;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    // fog after tonemap so the far floor blends seamlessly into the sky
    color = mix(color, uSkyColor, fog);

    gl_FragColor = vec4(color, 1.0);
}
