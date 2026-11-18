#version 330 core

// Water film of the splash crowns (scene 3).
//
// The sheet is shaded as a thick, nearly clear wall of water: the room's red
// and white tiles are refracted through it (same projection the walls use), the
// grazing angles reflect it, the base aerates into whitewater, and the torn rim
// carries beads. The old version mixed in a neutral grey "whitewater" term,
// which is what made the crowns read as thin smoke.

in vec3 vWorld;
in vec3 vNormal;
in float vParam;
in float vAngle;
in float vLigament;
in float vThin;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uWaterA;
uniform vec3 uWaterB;
uniform vec3 uTileA;
uniform vec3 uTileB;
uniform float uTime;
uniform float uJet;

out vec4 fragColor;

const float TAU = 6.28318530718;

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
    float f = 1.0 - u;
    float f2 = f * f;
    return F0 + (1.0 - F0) * (f2 * f2 * f);
}

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// The pool room as seen through water: a bright ceiling panel above, and the
// red / white tiled walls around. Sampling these colours (rather than a neutral
// gradient) is what makes the crown look like it is made of the pool's water.
vec3 roomRadiance(vec3 dir) {
    float up = clamp(dir.y, -1.0, 1.0);
    if (up > 0.45) {
        float panel = smoothstep(0.45, 0.88, up);
        return mix(vec3(0.96, 0.97, 0.98), vec3(1.75, 1.76, 1.72), panel);
    }
    // ~13 columns of tiles around the room, one course per ~0.8 rad.
    float az = atan(dir.x, dir.z);
    vec2 uv = vec2(az * 2.07, up * 1.25);
    vec2 cell = floor(uv);
    float chk = mod(cell.x + cell.y, 2.0);
    vec3 tile = mix(uTileA, uTileB, chk);
    vec2 f = fract(uv);
    float grout = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
    tile *= mix(1.04, 0.74, smoothstep(0.0, 0.045, grout));
    // The wall darkens toward the waterline; the ceiling lights the top.
    return tile * mix(0.55, 1.0, smoothstep(-0.45, 0.40, up));
}

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    vec3 N = normalize(vNormal);
    if (dot(N, V) < 0.0) N = -N;
    float camDist = length(uEyePos - vWorld);

    // Capillary ripples on the film, faded out once they fall below a pixel.
    vec2 q = vec2(vAngle * TAU * 9.0, vParam * 26.0);
    float foot = fwidth(q.x) + fwidth(q.y) + 1e-4;
    float gate = 1.0 - smoothstep(0.35, 1.20, foot);
    float n1 = vnoise(q + vec2(uTime * 1.7, -uTime * 2.3));
    float n2 = vnoise(q * 2.7 + vec2(-uTime * 2.9, uTime * 1.3));
    vec3 ripple = vec3((n1 - 0.5) * 0.20 + (n2 - 0.5) * 0.11, 0.0,
                       (n2 - 0.5) * 0.20 + (n1 - 0.5) * 0.11) * gate;
    vec3 Np = normalize(N + ripple);

    vec3 L = normalize(uLightDir);
    vec3 H = normalize(V + L);
    float NoV = clamp(dot(Np, V), 1e-3, 1.0);
    float NoL = max(dot(Np, L), 0.0);
    float NoH = max(dot(Np, H), 0.0);

    vec3 rd = reflect(-V, Np);
    vec3 rt = refract(-V, Np, 0.75);
    if (dot(rt, rt) < 1e-4) rt = -V;

    float fres = clamp(F_Schlick(NoV, 0.02) * 1.7, 0.02, 0.92);

    // Thick sheet: a long path through the water column, tinted by absorption.
    // Two surfaces are drawn (near and far wall of the crown), so each one is
    // allowed to stay translucent while the pair reads solid.
    float thick = mix(1.0, 0.25, vParam) * (0.45 + 0.55 * vThin);
    vec3 tint = exp(-vec3(0.17, 0.055, 0.038) * thick * 3.0);

    vec3 refr = roomRadiance(rt) * tint;
    vec3 refl = roomRadiance(rd) * 1.05;

    // Clear water: face-on you look straight through to the tiles, at grazing
    // angles the sheet turns into a mirror of them.
    vec3 col = mix(refr, refl, fres * 0.9);

    // Sun through the sheet: forward scattering lights the thin edges.
    float fwd = pow(max(dot(-V, L), 0.0), 2.0);
    col += uLightTint * fwd * (0.08 + 0.22 * vThin) * 0.55;

    // Narrow specular lobe - a water film, not a mirror ball.
    float a2 = 0.045 * 0.045;
    float glint = D_GGX(NoH, a2) * V_SmithGGX(NoV, NoL, a2) * F_Schlick(NoH, 0.02);
    col += uLightTint * min(glint * 3.5, 3.0);
    float sparkle = pow(NoH, 260.0) * gate;
    col += uLightTint * sparkle * 1.6;

    // Beads clinging to the film: small round highlights, only where the ripple
    // field is actually resolved.
    float beadField = vnoise(vec2(vAngle * TAU * 34.0, vParam * 30.0 + uTime * 2.2));
    float beads = smoothstep(0.66, 0.92, beadField) * gate;
    col += vec3(1.0) * beads * 0.30;

    // Falling water collects into vertical ribbons.
    vec2 fq = vec2(vAngle * TAU * 44.0, vParam * 2.5 - uTime * 5.0);
    float fgate = 1.0 - smoothstep(0.30, 1.10, fwidth(fq.x) + fwidth(fq.y) + 1e-4);
    float flow = mix(0.5, vnoise(fq), fgate);
    col *= 0.86 + 0.28 * flow;

    // Air entrained at the contact line: dense whitewater collar at the base.
    float collarZone = 1.0 - smoothstep(0.0, 0.30, vParam);
    float aeration = collarZone * (0.45 + 0.55 * vThin) * (0.55 + 0.45 * n1);
    aeration += collarZone * collarZone * 0.45;
    vec3 whitewater = mix(vec3(0.72, 0.82, 0.87), vec3(0.97, 0.99, 1.0),
                          clamp(0.35 + 0.65 * NoV, 0.0, 1.0));
    col = mix(col, whitewater, clamp(aeration, 0.0, 0.9) * 0.80);

    // Bright torn rim where the film breaks into droplets.
    float rimEdge = smoothstep(0.82, 1.0, vParam)
                  * smoothstep(0.25, 0.85, vLigament);
    col += vec3(0.95, 0.98, 1.0) * rimEdge * 0.55;

    // Thin dark meniscus where the wall is still glassy at the waterline;
    // the aerated foam sits above it.
    float meniscus = smoothstep(0.06, 0.0, vParam) * (1.0 - collarZone * 0.6);
    col *= 1.0 - 0.22 * meniscus;

    // Thickness of the water path: thick at the base, thin at the torn rim,
    // long at grazing angles.
    float body = mix(0.34, 0.62, vLigament);
    body *= 1.0 - 0.26 * vParam;
    body *= mix(0.75, 1.0, fres);
    body += aeration * 0.30 + rimEdge * 0.22;
    body *= 0.80 + 0.40 * flow;
    float alpha = clamp(body, 0.0, 0.96);

    // Distant crowns dissolve completely: a half-faded splash reads as a grey
    // smudge over the tiles, and at that size there is no detail left to show.
    alpha *= 1.0 - 0.88 * smoothstep(9.0, 22.0, camDist);

    if (uJet > 0.5) {
        // The re-entry jet is aerated whitewater, not a glass cone.
        float aerated = 0.35 + 0.65 * n1;
        vec3 jcol = mix(refr * 0.85, vec3(0.94, 0.97, 1.0), aerated * 0.75);
        jcol += uLightTint * min(glint * 2.0, 2.0);
        float jalpha = (0.40 + 0.40 * aerated) * (1.0 - 0.30 * vParam);
        jalpha *= 1.0 - 0.80 * smoothstep(10.0, 23.0, camDist);
        fragColor = vec4(jcol, clamp(jalpha, 0.0, 0.92));
        return;
    }
    fragColor = vec4(col, alpha);
}
