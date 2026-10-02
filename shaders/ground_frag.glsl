#version 120
// Ground plane fragment shader (GLSL 1.2)
// BUILD-P11: polished display concrete.
//
// What was wrong: the floor was one constant Lambert term over a 2-octave
// hash, so 60% of the frame was a single flat value. A floor is the largest
// surface in almost every shot a benchmark takes, and a flat one reads as a
// backdrop rather than as ground — it gives the eye no scale, no distance and
// no reason to believe the objects standing on it are really resting on it.
//
// What is here now:
//   * POWER-TROWELLED CONCRETE: broad burnish sweeps from the trowel, fine
//     exposed aggregate, and saw-cut control joints on a 3 m grid with a
//     chamfered arris that catches the sun.
//   * A real DYNAMIC RANGE on the floor: the burnished areas are smooth
//     enough to throw a genuine sun specular lobe and a grazing sheen toward
//     the viewer, so the floor has somewhere for the light to come back from
//     instead of only scattering it.
//   * AERIAL PERSPECTIVE out to the horizon: the far floor converges on the
//     sky dome's own horizon colour, with the haze brightening toward the sun
//     (forward scattering), so ground and sky MEET instead of the ground
//     stopping in a band of flat colour.
uniform sampler2D uShadowMap;
uniform vec3 uSunDirWorld;  // world-space direction towards the sun
uniform vec2 uShadowTexel;
uniform vec3 uSkyColor;
uniform vec3 uEyePos;   // camera, for the view vector and distance
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

// cheap value noise (no sin): the old hash was a sin(dot(...)) which costs
// ~20 cycles on the pre-SSE machines this scene exists for, once per pixel
float h21(vec2 p) {
    vec3 q = fract(vec3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(h21(i), h21(i + vec2(1.0, 0.0)), f.x),
               mix(h21(i + vec2(0.0, 1.0)), h21(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main() {
    vec2 wp = vWorldPos.xz;
    vec3 toEye = uEyePos - vWorldPos;
    float dist = length(toEye);

    // ---- the material -------------------------------------------------------
    // Saw-cut control joints on a 3 m grid. A joint is not a line, it is a
    // groove: dark at the bottom, with a bright chamfer on the sun side.
    vec2 g3 = abs(fract(wp / 3.0) - 0.5);
    float jline = max(g3.x, g3.y);
    float joint = smoothstep(0.494, 0.500, jline);
    float chamfer = smoothstep(0.470, 0.494, jline) * (1.0 - joint);

    // Broad trowel burnish: the arcs a power trowel leaves, in long slow
    // sweeps. This is the term that gives the floor a sense of scale — a
    // uniform noise has no direction and no feature size, so it reads as film
    // grain rather than as a surface.
    float sweep = vnoise(vec2(wp.x * 0.16 + wp.y * 0.05, wp.y * 0.11));
    float sweep2 = vnoise(vec2(wp.x * 0.62, wp.y * 0.51));
    vec3 base = vec3(0.255, 0.250, 0.243);
    base *= 0.84 + 0.30 * sweep;
    base *= 0.92 + 0.16 * sweep2;
    // exposed aggregate, near field only: it is sub-pixel past ~35 m and
    // aliases into a shimmering band if it is left running out there
    float near = 1.0 - smoothstep(18.0, 46.0, dist);
    if (near > 0.01) {
        float stones = smoothstep(0.66, 0.90, vnoise(wp * 13.0));
        base = mix(base, base * 1.55 + vec3(0.020), stones * near * 0.6);
    }
    // joint and chamfer on top of all of it
    base = mix(base, vec3(0.075, 0.072, 0.070), joint * 0.85);
    base = mix(base, base * 1.30 + vec3(0.012), chamfer * 0.7);

    // ---- lighting -----------------------------------------------------------
    float shadow = mix(shadowFactor(), 1.0, uShadowDisable);
    vec3 V = toEye / max(dist, 0.001);
    vec3 L = normalize(uSunDirWorld);
    vec3 N = vec3(0.0, 1.0, 0.0);
    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 0.001);
    vec3 H = normalize(L + V);
    float NoH = max(dot(N, H), 0.0);

    vec3 sunColor = vec3(1.0, 0.93, 0.82);
    vec3 ambSky = vec3(0.16, 0.19, 0.26);
    vec3 ambGnd = vec3(0.10, 0.09, 0.08);
    vec3 ambient = mix(ambGnd, ambSky, 0.75);
    // hemispheric ambient: the floor sees the whole sky dome
    vec3 lighting = base * (ambient + sunColor * NoL * 1.55 * shadow);

    // ---- the specular that makes it a floor and not a backdrop -------------
    // Burnished concrete is smooth enough to return a real highlight. A wide
    // low lobe plus a tight one: the wide lobe is the sheen a trowelled slab
    // throws across a room, the tight one is the glint off the aggregate.
    float burnish = smoothstep(0.35, 0.85, sweep);
    float rough = mix(0.55, 0.24, burnish);
    float spWide = pow(NoH, mix(6.0, 90.0, 1.0 - rough)) * 0.55;
    float spTight = pow(NoH, 340.0) * near * 0.5;
    // grazing sheen: at a low camera the floor picks up the sky along the
    // view direction, which is what stops it going dead flat in the distance
    float fres = pow(1.0 - NoV, 4.0);
    vec3 sheen = mix(vec3(0.20, 0.22, 0.26), vec3(0.55, 0.52, 0.46),
                     0.5 + 0.5 * dot(normalize(vec3(L.x, 0.0, L.z)),
                                     normalize(vec3(V.x, 0.0, V.z))));
    vec3 color = lighting
               + sunColor * (spWide + spTight) * NoL * shadow
               + sheen * fres * 0.30;

    // match the object shader tonemap + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    // ---- aerial perspective out to the horizon -----------------------------
    // Exponential in distance, not a 13 m smoothstep: the old one reached full
    // fog by 22 m, which turned the far two-thirds of every frame into one flat
    // wash of clear colour and killed every depth cue the shot had. The haze
    // also brightens toward the sun, because backlit air forward-scatters.
    float fog = 1.0 - exp(-dist * 0.0092);
    vec3 haze = mix(uSkyColor, uSkyColor * vec3(1.06, 1.02, 0.95),
                    max(dot(-V, L), 0.0));
    color = mix(color, haze, clamp(fog, 0.0, 1.0));

    gl_FragColor = vec4(color, 1.0);
}