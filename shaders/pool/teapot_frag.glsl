#version 330 core
// Pool scene teapot: lit ONLY by the hidden light (there is no sun in this
// scene) plus the room's checker bounce. SUPER-REALISM PASS:
//   * energy-corrected shading: NDF-normalized GGX (no Kd*diff + spec
//     double-count), a wetness-dependent roughness, and a HARD SPEC CAP so
//     no view angle can spike a firefly highlight,
//   * animated caustic webs crawl over the SUBMERGED ceramic (same two
//     crossing trig webs as water_frag.glsl, driven by uTime) — the pot
//     visibly sits IN the water volume, not behind an opaque blue mask,
//   * refraction offset: submerged geometry samples the base texture along
//     a view-bent ray, so underwater parts wobble and magnify like real
//     refracted objects,
//   * anisotropic wet streaks: vertical drips streak the grazing sheen,
//   * tone map without the knee: plain Reinhard keeps the ceramic's colour
//     from desaturating into grey plastic.

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform sampler2D uBaseColor;
uniform float uWetness;    // 0 = bone dry (in the air), 1 = soaked (after splash)
uniform float uWaterLine;  // world-space y of the water surface
uniform vec3  uWaterBody;  // blue pool-water body colour for submerged parts
uniform float uReflect;    // 1 when rendering the MIRRORED fleet into the water
uniform float uTime;

out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uEyePos - vWorld);
    vec3 L = normalize(uLightDir);

    float below = clamp((uWaterLine - vWorld.y) * 6.0, 0.0, 1.0);

    // refraction: bend the texture lookup underwater (cheap fake IOR) so the
    // submerged shell wobbles and magnifies with depth
    vec2 uv = vUV;
    if (below > 0.0) {
        vec3 bend = normalize(vec3(V.x, 0.0, V.z) + vec3(0.0, -1.1, 0.0));
        uv += bend.xz * below * 0.035;
    }
    vec3 base = texture(uBaseColor, uv).rgb;
    base = pow(base, vec3(2.2)); // texture is sRGB; shade in linear

    // ---- energy-corrected direct light: normalized diffuse (1/pi) times
    // the GGX NDF with a capped peak (a 0.055-alpha lobe can otherwise spike
    // past 50 at mirror angles and mint a white firefly on the pot rim).
    float aGGX = mix(0.11, 0.055, uWetness);
    float a2 = aGGX * aGGX;
    vec3 H = normalize(V + L);
    float NdH = max(dot(N, H), 0.0);
    float dGGX = a2 / (3.14159265 * pow(NdH * NdH * (a2 - 1.0) + 1.0, 2.0));
    float spec = min(dGGX, 4.5);
    float diff = clamp(dot(N, L), 0.0, 1.0);
    // wet ceramic keeps more grazing sheen; anisotropic streaks modulate it
    float streak = 0.75 + 0.25 * sin(vWorld.y * 46.0 + vUV.x * 9.0);
    float specK = mix(0.16, 0.38, uWetness) * streak;

    // room ambient: red bounce from the glowing checker ceiling + darker
    // floor bounce, following the normal like a real two-point room.
    vec3 skyA = vec3(0.92, 0.92, 0.90);
    vec3 skyB = vec3(0.55, 0.012, 0.014);
    vec3 ambient = mix(skyB * 0.085, skyA * 0.115, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    float horizBand = 1.0 - abs(N.y);
    ambient += mix(skyA, skyB, 0.5) * horizBand * 0.05;

    vec3 col = base * (ambient + uLightTint * diff * 0.95)
             + uLightTint * spec * specK;

    // Fresnel rim: glazed ceramic gets a grazing-angle sheen off the sky
    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);
    col += uLightTint * fres * mix(0.16, 0.26, uWetness);

    // ---- waterline: submerged shell reads as underwater ---------------
    // animated caustic web (crossing trig waves, matches water_frag.glsl):
    // bright focused bands crawl over the submerged ceramic with time.
    vec2 cp = vWorld.xz * 3.1;
    float web1 = 0.5 + 0.5 * sin(cp.x + sin(cp.y * 1.7 + uTime * 1.9) * 1.4);
    float web2 = 0.5 + 0.5 * sin(cp.y * 1.3 - uTime * 1.4 +
                                 sin(cp.x * 1.9 - uTime * 0.8) * 1.4);
    float caustic = pow(web1 * web2, 3.0);
    col += uLightTint * caustic * below * 0.55;
    // underwater absorption shifts the base toward the pool blue with depth
    col = mix(col, col * (uWaterBody * 2.4), below * 0.75);
    // foam hugs the water line (a few cm band), fresher right after the splash
    float foamBand = exp(-pow((uWaterLine - vWorld.y) * 9.0, 2.0));
    col += vec3(0.9, 0.95, 1.0) * foamBand * uWetness * 0.30;
    // a wet meniscus shine just above the line
    float meniscus = exp(-pow((vWorld.y - uWaterLine) * 12.0, 2.0));
    col += uLightTint * meniscus * uWetness * 0.18;

    col = col / (col + vec3(0.9));
    col = clamp(col, 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));

    float alpha = 1.0;
    if (uReflect > 0.5) {
        // The reflected pot is seen THROUGH rippling water: the pool body
        // bleeds into it, it dims, and its alpha shimmers line by line so the
        // mirror image breaks up exactly where the surface is disturbed.
        col *= 0.60;
        col += uWaterBody * 0.30;
        alpha = 0.55 + 0.12 * sin(vWorld.x * 8.0 + vWorld.z * 6.0);
    }
    fragColor = vec4(col, alpha);
}
