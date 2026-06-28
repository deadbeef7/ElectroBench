#version 120
// Fragment shader
uniform sampler2D uBaseColor;
uniform sampler2D uNormalMap;
uniform sampler2D uMetallicMap;
uniform sampler2D uHeightMap;
uniform sampler2D uAOMap;
uniform sampler2D uRoughnessMap;

varying vec2 vTexCoord;
varying vec3 vFakeViewDir;

void main() {
    float time = texture2D(uHeightMap, vec2(0.5)).r * 6.2831;
    vec3 lightDir = normalize(vec3(cos(time), 0.35, sin(time)));
    float height = texture2D(uHeightMap, vTexCoord).r;
    float parallaxScale = 0.06;
    vec2 viewOffset = (vFakeViewDir.xy / vFakeViewDir.z);
    vec2 texCoord = vTexCoord + (height - 0.5) * parallaxScale * viewOffset;

    vec3 normal = texture2D(uNormalMap, texCoord).rgb * 2.0 - 1.0;
    normal = normalize(normal);
    if (length(normal) < 0.1) normal = vec3(0.0, 0.0, 1.0);

    vec3 viewDir = normalize(vFakeViewDir);
    vec3 halfVec = normalize(lightDir + viewDir);
    vec3 baseColor = texture2D(uBaseColor, texCoord).rgb;
    float metallic = texture2D(uMetallicMap, texCoord).r;
    float roughness = texture2D(uRoughnessMap, texCoord).r;
    float NdotV = max(dot(normal, viewDir), 0.0);
    float fresnel = pow(1.0 - NdotV, 5.0);

    float NdotL = max(dot(normal, lightDir), 0.0);
    float NdotH = max(dot(normal, halfVec), 0.0);

    float specExponent = 64.0;
    float spec = pow(NdotH, specExponent) * (1.0 - roughness);
    spec *= smoothstep(0.0, 0.1, NdotL);

    vec3 specColor = mix(vec3(1.0), baseColor, metallic);

    float ao = texture2D(uAOMap, texCoord).r;
    ao = max(ao, 0.1);

    vec3 reflectionDir = reflect(-viewDir, normal);
    vec2 reflectionTexCoord = vec2(reflectionDir.x * 0.5 + 0.5, reflectionDir.y * 0.5 + 0.5);
    vec3 environmentColor = texture2D(uBaseColor, reflectionTexCoord).rgb;

    vec3 diffuse = baseColor * (1.0 - metallic);
    vec3 specular = specColor * spec * (metallic + 0.2 + fresnel * 0.5);
    vec3 ambient = baseColor * 0.15;
    ambient *= 1.1;

    vec3 diffuseLight = diffuse * NdotL;
    diffuseLight *= 1.1;

    specular *= 1.5;
    vec3 reflection = mix(diffuse, environmentColor, metallic);
    vec3 lighting = (reflection * NdotL + specular + ambient + diffuseLight) * ao;
    lighting = lighting / (lighting + vec3(1.0));
    lighting = pow(lighting, vec3(1.0 / 2.2));

    gl_FragColor = vec4(lighting, 1.0);
}
