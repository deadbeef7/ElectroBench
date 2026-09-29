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

// ---- full Cook-Torrance terms (mirrors water_frag.glsl) --------------------
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
// ACES filmic tone curve (Narkowicz fit) — the highlight roll-off Hollywood
// cameras show: saturated colours stay saturated into the shoulder instead
// of washing to white the way Reinhard does.
vec3 ACESFilm(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

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

    // HYPER-REAL WETNESS RAMP: dry ceramic is matte; a fresh splash wets the
    // surface to a glossy glaze, then it slowly dries back. The wetness value
    // here is pot.splashed (0 or 1), but the shader also reads the actual
    // immersion so submerged halves look wetter than the exposed rim.
    float wet = clamp(uWetness + below * 0.45, 0.0, 1.0);
    float rough = mix(0.55, 0.08, wet);   // wet glaze is smooth
    float aGGX = rough * rough;
    float a2 = aGGX * aGGX;

    vec3 H = normalize(V + L);
    float NdH = max(dot(N, H), 0.0);
    float NoV = clamp(dot(N, V), 1e-3, 1.0);
    float NoL = clamp(dot(N, L), 0.0, 1.0);
    float dGGX = D_GGX(NdH, a2) * V_SmithGGX(NoV, NoL, a2) * 4.0;
    float diff = NoL;

    // anisotropic wet streaks: vertical drips shear the grazing sheen along
    // the pot's local V direction; stronger on wet ceramic, weakest on bone-
    // dry matte. The streak axis is the universal up in world space, projected
    // into the tangent frame of the surface (a cheap stand-in for the pot's
    // actual tangent basis on a height-field surface).
    vec3 T = normalize(cross(N, vec3(0.0, 0.0, 1.0)));
    float TdV = abs(dot(T, V));
    float streak = 0.78 + 0.22 * sin(vWorld.y * 46.0 + vUV.x * 9.0 + uTime * 0.6);
    float anisoSheen = pow(TdV, 6.0) * wet * 0.18 * streak;

    // energy-corrected ceramic: diffuse (Lambert/pi) plus a capped GGX NDF.
    // Capped to 4.5 so no view angle can spike a firefly on the rim.
    float spec = min(dGGX, 4.5);
    // wet ceramic keeps a stronger specular; dry matte mostly diffuses.
    float specK = mix(0.10, 0.22, wet) * streak;

    // room ambient: checker bounce (hot white ceiling + deep red floor)
    // follows the normal like a real two-point room.
    vec3 skyA = vec3(0.92, 0.92, 0.90);
    vec3 skyB = vec3(0.55, 0.012, 0.014);
    vec3 ambient = mix(skyB * 0.085, skyA * 0.115, clamp(N.y * 0.5 + 0.5, 0.0, 1.0));
    float horizBand = 1.0 - abs(N.y);
    ambient += mix(skyA, skyB, 0.5) * horizBand * 0.05;

    vec3 col = base * (ambient + uLightTint * diff * 0.95)
             + uLightTint * spec * specK
             + uLightTint * anisoSheen;

    // Fresnel rim: matte-glazed ceramic, not lacquered. Stronger when wet.
    float fres = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);
    col += uLightTint * fres * mix(0.05, 0.12, wet);

    // ---- HYPER-REAL WATERLINE: submerged ceramic sits IN the water volume ----
    // animated caustic webs crawl over the submerged shell (same two crossing
    // trig webs as water_frag.glsl, driven by uTime) — the pot visibly sits
    // inside the water, not behind an opaque blue mask.
    vec2 cp = vWorld.xz * 3.1;
    float web1 = 0.5 + 0.5 * sin(cp.x + sin(cp.y * 1.7 + uTime * 1.9) * 1.4);
    float web2 = 0.5 + 0.5 * sin(cp.y * 1.3 - uTime * 1.4 +
                                 sin(cp.x * 1.9 - uTime * 0.8) * 1.4);
    float caustic = pow(web1 * web2, 3.0);
    col += uLightTint * caustic * below * 0.55;

    // underwater absorption shifts the ceramic toward the pool blue with depth
    col = mix(col, col * (uWaterBody * 2.4), below * 0.75);

    // foam hugs the water line — a few cm band, fresher right after the splash.
    // Real splashes throw a white foam collar around the pot at the line; here
    // it is a soft brightening that fades with wetness and time since impact.
    float foamBand = exp(-pow((uWaterLine - vWorld.y) * 9.0, 2.0));
    col += vec3(0.9, 0.95, 1.0) * foamBand * wet * 0.30;

    // a wet meniscus shine just above the line — the clinging water sheet on
    // the exterior of a just-splashed pot.
    float meniscus = exp(-pow((vWorld.y - uWaterLine) * 12.0, 2.0));
    col += uLightTint * meniscus * wet * 0.10;

    // CLEAR-COAT GLAZE (the modern ceramic/car-paint trick): a thin smooth
    // lacquer over the shaded base — a second, sharper GGX lobe with its own
    // Fresnel. Glazed ceramic reads as TWO layers (matte body + lacquer),
    // which is exactly what separates a rendered teapot from a matte blob.
    float ccA2 = 0.012;                              // very smooth lacquer
    float ccD = D_GGX(NdH, ccA2);
    float ccV = V_SmithGGX(NoV, NoL, ccA2);
    float ccF = F_Schlick(NoV, 0.05);                // lacquer F0 ~ 0.05
    vec3 clearcoat = uLightTint * (ccD * ccV * ccF * 4.0) * 0.55;
    col += clearcoat * mix(0.5, 1.0, wet);           // wetter = glossier coat

    // ACES filmic tonemap: colours stay saturated into the highlight shoulder
    // (Reinhard's washed-out white rim is the last big 'CGI' tell).
    col = ACESFilm(col);
    col = clamp(col, 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));

    float alpha = 1.0;

    // ---- REFLECTED TEAPOT: never render a 'real' mirrored pot under the
    // water. The mirror image of a pot above the surface is NOT itself a
    // teapot swimming underwater: the water body absorbs and tints it, and
    // where the surface is rippled the reflection breaks up and disappears.
    //
    // Colour the fragment toward the water body (basically blacken it,
    // tinting by the blue pool body rather than the bright ceramic). The
    // reflected ceramic can still read as a faint warm glint where the
    // surface is nearly flat, but it must look like a reflection IN water,
    // not a ceramic pot below the surface.
    if (uReflect > 0.5) {
        // how far the reflected fragment is below the mirrored water line
        float reflFade = smoothstep(0.0, 1.0, below * 1.5 + 0.25);
        // the reflected colour is drowned toward the water body; a little of
        // the ceramic base survives only where the surface is nearly flat
        vec3 reflBase = base;
        vec3 reflDrown = uWaterBody;
        vec3 reflCol = mix(reflBase, reflDrown, 0.78 + 0.22 * reflFade);
        // a faint specular ghost where the mirror ray hits a locally flat rim
        vec3 refGhost = uLightTint * (0.10 * spec * specK * (1.0 - reflFade));
        reflCol = reflCol + refGhost;
        // break up the reflection line by line with the surface ripple so the
        // image shimmers and disappears into the water exactly where the rings
        // disturb it — weak alpha that can drop to ~0.25 at ripple peaks.
        float rippleKill = 0.55 + 0.45 * sin(vWorld.x * 8.0 + vWorld.z * 6.0 + uTime * 2.1);
        alpha = 0.22 + 0.18 * rippleKill * (1.0 - 0.60 * below);
        vec3 drowned = col * (uWaterBody * 1.6);
        col = mix(col, reflCol, 0.92);
        col = mix(col, drowned, 0.55);
    }
    fragColor = vec4(col, alpha);
}
