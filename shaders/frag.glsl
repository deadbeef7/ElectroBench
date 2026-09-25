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

// rotated 12-tap poisson PCF shadow lookup, returns 0 (shadowed) .. 1 (lit)
float shadowFactor() {
    vec3 p = vShadowCoord.xyz / vShadowCoord.w;
    // uLightMatrix already includes the bias (world -> [0,1]); the half
    // offset must NOT be applied again here (see ground_frag.glsl).
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z >= 1.0)
        return 1.0;
    float bias = 0.0022;
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
    // The base-colour / metallic / roughness maps are sRGB PNGs sampled raw
    // here, but the pipeline applies pow(1/2.2) at the end — sampling raw and
    // then gamma-encoding again re-brightens dark gunmetal albedo (~0.16
    // raw) into washed-out light grey (~0.43): the guns looked far too
    // white. Linearise the colour maps once, shade in linear, encode once.
    vec3 baseColor = pow(texture2D(uBaseColor, texCoord).rgb, vec3(2.2));
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

    // Metal F0: a metal's specular reflectance is far brighter than its
    // diffuse albedo (steel F0 ~0.5 linear even when the painted body reads
    // near-black). Clamp the metallic share of specColor so metal parts keep
    // crisp bright glints while the diffuse body stays dark gunmetal.
    vec3 metalF0 = max(baseColor, vec3(0.32, 0.33, 0.35));
    vec3 specColor = mix(vec3(0.05, 0.05, 0.05), metalF0, metallic);
    const vec3 sunColor = vec3(1.0, 0.88, 0.72);   // warm late-afternoon sun
    const vec3 skyAmbient = vec3(0.92, 0.97, 1.12); // cool sky fill

    float ao = texture2D(uAOMap, texCoord).r;
    ao = max(ao, 0.1);

    vec3 reflectionDir = reflect(-viewDir, normal);
    // Polished steel mirrors the ROOM, not its own albedo texels: sampling
    // the base-colour map at screen-space reflection coords smeared bright
    // texels across the metal bodies (part of the washed-white look). A dim
    // analytic environment — cool dusk sky above, warm dark floor below —
    // plus the existing sun glint reads as real steel instead.
    vec3 environmentColor = mix(vec3(0.04, 0.035, 0.03), vec3(0.10, 0.13, 0.20),
                                clamp(reflectionDir.y * 0.5 + 0.5, 0.0, 1.0));
    // No sun lobe here: the direct Blinn specular already draws crisp,
    // shadow-gated glints — adding a reflection-ray lobe double-counts the
    // sun and washed big flat steel patches towards white.

    vec3 diffuse = baseColor * (1.0 - metallic);
    vec3 specular = specColor * spec * (metallic + 0.2 + fresnel * 0.5);
    // 0.10 (was 0.18): outdoor shadowed metal sits near-black. The fat cool
    // ambient was lifting the dark polymer + blued-steel bodies into pale
    // washed grey — the reason the guns read white in bright sun.
    vec3 ambient = baseColor * 0.10 * skyAmbient;

    vec3 diffuseLight = diffuse * NdotL * shadow * sunColor;

    specular *= 1.9;   // crisp glints sell the metal against the dark bodies
    vec3 reflection = mix(diffuse, environmentColor, metallic);
    reflection *= NdotL * shadow;
    vec3 lighting = (reflection + specular + ambient + diffuseLight) * ao;
    lighting = lighting / (lighting + vec3(1.0));
    lighting = pow(lighting, vec3(1.0 / 2.2));

    gl_FragColor = vec4(lighting, 1.0);
}
