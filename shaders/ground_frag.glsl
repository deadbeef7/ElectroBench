#version 120
// Ground plane fragment shader (GLSL 1.2)
// Procedural concrete display floor: subtle tile grid, sun lambert,
// 3x3 PCF shadows from the shadow map, distance fog into the sky color.
uniform sampler2D uShadowMap;
uniform vec3 uSunDirWorld;  // world-space direction towards the sun
uniform vec2 uShadowTexel;
uniform vec3 uSkyColor;

varying vec3 vWorldPos;
varying vec4 vShadowCoord;

float shadowFactor() {
    vec3 p = vShadowCoord.xyz / vShadowCoord.w;
    p = p * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z >= 1.0)
        return 1.0;
    float bias = 0.0022;
    float sum = 0.0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            float d = texture2D(uShadowMap, p.xy + vec2(dx, dy) * uShadowTexel).r;
            sum += step(p.z - bias, d);
        }
    }
    return sum / 9.0;
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
    float shadow = shadowFactor();

    vec3 sunColor = vec3(1.0, 0.93, 0.82);
    // strong sun / low ambient so the gun shadows clearly stand out
    vec3 lighting = floorColor * (sunColor * (NdotL * 1.7 * shadow) +
                                  vec3(0.14, 0.15, 0.19));

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
