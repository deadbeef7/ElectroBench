#version 120
// Fragment shader (GLSL 1.2) - PBR-ish shading with shadow-mapped sun
uniform sampler2D uBaseColor;
uniform sampler2D uNormalMap;
uniform sampler2D uMetallicMap;
uniform sampler2D uHeightMap;
uniform sampler2D uAOMap;
uniform sampler2D uRoughnessMap;
uniform sampler2D uShadowMap;

uniform vec3 uSunDirView;   // view-space direction towards the sun
uniform vec2 uShadowTexel;  // 1.0 / shadow map size

varying vec2 vTexCoord;
varying vec3 vViewPos;
varying vec3 vNormalView;
varying vec4 vShadowCoord;

// 3x3 PCF shadow lookup, returns 0 (shadowed) .. 1 (lit)
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
    vec3 lightDir = normalize(uSunDirView);
    float height = texture2D(uHeightMap, vTexCoord).r;
    float parallaxScale = 0.06;
    vec3 viewDir = normalize(-vViewPos);
    vec2 viewOffset = normalize(viewDir + vec3(0.0, 0.0, 0.4)).xy;
    vec2 texCoord = vTexCoord + (height - 0.5) * parallaxScale * viewOffset;

    vec3 normal = texture2D(uNormalMap, texCoord).rgb * 2.0 - 1.0;
    // blend the tangent-space detail with the true geometric normal
    normal = normalize(normal + normalize(vNormalView) * 0.7);
    if (length(normal) < 0.1) normal = vec3(0.0, 0.0, 1.0);

    vec3 halfVec = normalize(lightDir + viewDir);
    vec3 baseColor = texture2D(uBaseColor, texCoord).rgb;
    float metallic = texture2D(uMetallicMap, texCoord).r;
    float roughness = texture2D(uRoughnessMap, texCoord).r;
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 5.0);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotH = max(dot(normal, halfVec), 0.0);

    float shadow = shadowFactor();

    float specExponent = 64.0;
    float spec = pow(NdotH, specExponent) * (1.0 - roughness);
    spec *= smoothstep(0.0, 0.1, NdotL) * shadow;

    vec3 specColor = mix(vec3(1.0), baseColor, metallic);
    const vec3 sunColor = vec3(1.0, 0.88, 0.72);   // warm late-afternoon sun
    const vec3 skyAmbient = vec3(0.92, 0.97, 1.12); // cool sky fill

    float ao = texture2D(uAOMap, texCoord).r;
    ao = max(ao, 0.1);

    vec3 reflectionDir = reflect(-viewDir, normal);
    vec2 reflectionTexCoord = vec2(reflectionDir.x * 0.5 + 0.5, reflectionDir.y * 0.5 + 0.5);
    vec3 environmentColor = texture2D(uBaseColor, reflectionTexCoord).rgb;

    vec3 diffuse = baseColor * (1.0 - metallic);
    vec3 specular = specColor * spec * (metallic + 0.2 + fresnel * 0.5);
    vec3 ambient = baseColor * 0.18 * skyAmbient;

    vec3 diffuseLight = diffuse * NdotL * shadow * sunColor;
    diffuseLight *= 1.15;

    specular *= 1.5;
    vec3 reflection = mix(diffuse, environmentColor, metallic);
    reflection *= NdotL * shadow;
    vec3 lighting = (reflection + specular + ambient + diffuseLight) * ao;
    lighting = lighting / (lighting + vec3(1.0));
    lighting = pow(lighting, vec3(1.0 / 2.2));

    gl_FragColor = vec4(lighting, 1.0);
}
