#version 330 core

// Crown sheet for scene 3.
//
// A crown splash is not a wall of glass standing on the pool: surface tension
// holds the rising sheet together in a handful of arcs, each of which breaks
// apart at a different height, and the foot of every arc flares out into the
// surface instead of meeting it at a sharp vertical lip.
//
// So the silhouette is built here, not painted in the fragment shader:
//   * the rim height is a ridged noise field, and sectors where the field dies
//     collapse to the waterline, so the ring tears into separate sheets;
//   * a low-frequency envelope makes one side of the crown taller than the
//     other, the way an asymmetric impact throws an asymmetric crown;
//   * the radius grows with height, so the sheets lean outward and the base
//     flares into the pool instead of standing straight up;
//   * the bottom row sits below y = 0, which hides the contact edge underwater.

layout(location = 0) in vec2 aAngleH;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform float uRadius;
uniform float uHeight;
uniform float uTime;
uniform float uSpike;
uniform float uPhase;

out vec3 vWorld;
out vec3 vNormal;
out float vParam;
out float vAngle;
out float vLigament;
out float vThin;
out float vTip;

const float TAU = 6.28318530718;

float hash11(float n) { return fract(sin(n * 127.1 + 311.7) * 43758.5453); }

// Periodic 1-D value noise around the crown.
float angNoise(float a, float cells, float seed) {
    float x = a * cells;
    float i = floor(x);
    float f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float n0 = hash11(mod(i, cells) + seed * 37.0);
    float n1 = hash11(mod(i + 1.0, cells) + seed * 37.0);
    return mix(n0, n1, f);
}

// Surface tension pulls the wall into a handful of wide sheets (low angular
// frequency) with thin strands between them (higher frequencies). The field
// drifts slowly, so the strands move with the sheet instead of flickering.
float ligamentField(float a, float t, float phase) {
    float drift = t * 0.075 + phase * 0.11;
    // Broad sheets dominate; higher frequencies only roughen their edges.
    float l = 0.52 * angNoise(a + drift, 4.0, 1.0)
            + 0.30 * angNoise(a - drift * 1.7, 9.0, 2.0)
            + 0.18 * angNoise(a + drift * 2.3, 17.0, 3.0);
    // The sheet has to stay *connected* around most of the impact: a crown is
    // a continuous curtain whose top is torn into fingers. A sparser field left
    // a picket fence of separate fins with the pool showing between them.
    l = smoothstep(0.18, 0.52, l);
    // One or two coarse tears run right through the wall.
    float gate = smoothstep(0.04, 0.26, angNoise(a - drift * 0.8, 3.0, 7.0));
    return l * mix(0.52, 1.0, gate);
}

// Point on the sheet: hp runs from the foot (0) to the torn rim (1).
vec3 surfacePoint(float ang, float hp, float rim, float wobble) {
    float h = clamp(hp, 0.0, 1.0);
    // Outward lean: the base flares into the pool and the rim is thrown wider
    // still, so the sheet reads as a curtain of water rather than a cylinder.
    // A crown flares like a trumpet: narrow where it leaves the water, thrown
    // wide at the top. A wall that widens evenly instead reads as a bowl.
    float flare = 0.86 + 0.58 * h * h;
    float r = uRadius * flare * wobble;
    // Where the sheet has torn down to nothing, pull the remaining sliver in
    // so it lies flat on the pool instead of ending in a vertical step.
    r *= 1.0 - 0.10 * h * (1.0 - rim);
    float y = uHeight * hp * rim;
    // The foot row sits below the waterline; its contact edge is never seen.
    y -= 0.055 * uHeight * (1.0 - smoothstep(0.0, 0.12, hp));
    // The top of a crown curls outward as it thins and tears, so the tips are
    // rounded lips instead of straight-cut blades.
    float curl = smoothstep(0.72, 1.0, h);
    y -= 0.07 * uHeight * curl * rim;
    r += 0.07 * uRadius * curl;
    return vec3(cos(ang) * r, y, sin(ang) * r);
}

void main() {
    float ang = aAngleH.x * TAU;
    float hp = aAngleH.y;
    float t = uTime + uPhase;

    float lig = ligamentField(ang, t, uPhase);
    // Folds. A crown wall is never a smooth cylinder: surface tension corrugates
    // it into vertical pleats that run the height of the sheet and deepen as it
    // rises. They are what gives a real splash its bright and dark vertical
    // bands, and without them the wall reads as moulded plastic.
    float fold = (angNoise(ang + t * 0.09, 6.0, 31.0) - 0.5)
               + 0.55 * (angNoise(ang - t * 0.15, 13.0, 33.0) - 0.5);
    float wobble = 1.0 + 0.075 * (angNoise(ang + t * 0.06, 15.0, 11.0) - 0.5)
                       + 0.035 * (angNoise(ang - t * 0.11, 31.0, 17.0) - 0.5)
                       + 0.055 * fold * (0.85 + 0.15 * aAngleH.y);

    // Rim height. The wall is continuous: even where the ligament field is
    // weak, water is on its way up, so the base of the curtain stays and only
    // the top tears into fingers. A field that dropped to zero left separate
    // glass blades standing in the pool instead of a sheet of thrown water.
    float rim = 0.40 + 0.60 * pow(lig, 0.90);
    // One side of an impact is always thrown higher than the other.
    rim *= 0.70 + 0.52 * angNoise(ang + t * 0.04, 3.0, 21.0);
    // A few sharp tips climb above the rest of the sheet.
    rim += uSpike * 0.22 * pow(lig, 3.0)
         * (0.35 + 0.65 * angNoise(ang - t * 0.19, 27.0, 13.0));
    rim = clamp(rim, 0.0, 1.35);

    vec3 local = surfacePoint(ang, hp, rim, wobble);
    // An impact is never vertical: the pot arrives with its own horizontal
    // motion, so the crown is thrown over to one side. A perfectly upright
    // crown of revolution is the strongest "prop" cue there is.
    float leanDir = uPhase * 2.3;
    vec2 lean = vec2(cos(leanDir), sin(leanDir)) * 0.22 * uHeight * hp * hp;
    local.x += lean.x;
    local.z += lean.y;
    vec3 pos = uCenter + local;

    // Normal from finite differences on the actual sheet, so lighting follows
    // the torn silhouette instead of a cone of revolution.
    float e = 0.01;
    vec3 pA = surfacePoint(ang + e, hp, rim, wobble);
    // At the torn rim (hp = 1) the forward difference vanishes; step backward so
    // the tangent stays defined. A normalize(0) here produced NaNs that the
    // bloom pyramid spread across the whole frame.
    float hp2 = hp < 0.98 ? min(hp + 0.02, 1.0) : max(hp - 0.02, 0.0);
    vec3 pH = surfacePoint(ang, hp2, rim, wobble);
    vec3 n = cross(pH - local, pA - local);
    float nlen = length(n);
    n = nlen > 1e-8 ? n / nlen : vec3(cos(ang), 0.0, sin(ang));

    vWorld = pos;
    vNormal = n;
    vParam = hp;
    vAngle = aAngleH.x;
    vLigament = lig;
    vThin = 1.0 - lig;
    vTip = smoothstep(0.55, 1.0, hp) * lig;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
