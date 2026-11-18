#version 330 core

// Water film of the splash crowns (scene 3).
//
// The sheet is shaded as clear, moving water: it bends the *actual* pool behind
// it (scene 3 resolves the frame so far into a texture and hands it over), it
// reflects the bright room at grazing angles, and only its foot aerates into
// whitewater. Refracting an analytic copy of the room looked wrong no matter how
// it was tuned - the sheet showed big flat slabs of tile that never lined up
// with the pool it was standing in, which is what made the crowns look glued on.

in vec3 vWorld;
in vec3 vNormal;
in float vParam;
in float vAngle;
in float vLigament;
in float vThin;
in float vTip;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uWaterA;
uniform vec3 uWaterB;
uniform vec3 uTileA;
uniform vec3 uTileB;
uniform float uTime;
uniform float uJet;

uniform sampler2D uScene;   // frame drawn so far, resolved to a plain texture
uniform vec2 uResolution;
uniform float uHasScene;

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

// The pool room as seen in a reflection: the bright ceiling panel above, the
// red / white tiled walls around. Only used for the reflected ray - what is
// *behind* the sheet comes from the scene texture.
vec3 roomRadiance(vec3 dir) {
    float up = clamp(dir.y, -1.0, 1.0);
    if (up > 0.45) {
        float panel = smoothstep(0.45, 0.88, up);
        return mix(vec3(0.96, 0.97, 0.98), vec3(1.75, 1.76, 1.72), panel);
    }
    float az = atan(dir.x, dir.z);
    vec2 uv = vec2(az * 2.07, up * 1.25);
    vec2 cell = floor(uv);
    float chk = mod(cell.x + cell.y, 2.0);
    vec3 tile = mix(uTileA, uTileB, chk);
    vec2 f = fract(uv);
    float grout = min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y));
    tile *= mix(1.04, 0.74, smoothstep(0.0, 0.045, grout));
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
    float n1 = vnoise(q + vec2(uTime * 1.9, -uTime * 2.4));
    float n2 = vnoise(q * 2.6 + vec2(-uTime * 3.1, uTime * 1.4));
    vec3 ripple = vec3((n1 - 0.5) * 0.18 + (n2 - 0.5) * 0.09, 0.0,
                       (n2 - 0.5) * 0.18 + (n1 - 0.5) * 0.09) * gate;
    vec3 Np = normalize(N + ripple);

    vec3 L = normalize(uLightDir);
    vec3 H = normalize(V + L);
    float NoV = clamp(dot(Np, V), 1e-3, 1.0);
    float NoL = max(dot(Np, L), 0.0);
    float NoH = max(dot(Np, H), 0.0);

    float fres = clamp(F_Schlick(NoV, 0.02) * 1.55, 0.03, 0.92);

    // ---- what is behind the sheet -------------------------------------------
    // A thin sheet barely bends the ray, so the offset is small - but it has to
    // be big enough to slide the water's own reflections and the splash's own
    // far wall, which is what the eye reads as "that is water in front of
    // something", not a decal. The vertical bias comes from looking along a
    // nearly vertical curtain.
    vec2 suv = gl_FragCoord.xy / uResolution;
    float bend = 0.115 / (1.0 + camDist * 0.10);
    vec2 refrOff = vec2((n1 - 0.5) + 0.30 * sin(vAngle * TAU * 5.0 + uTime * 2.1),
                        (n2 - 0.5) * 1.05) * bend;
    refrOff += Np.xz * bend * 0.9;

    vec3 behind;
    if (uHasScene > 0.5) {
        // A vertical sheet of water is a cylindrical lens: it squeezes the pool
        // horizontally behind it and barely at all vertically. That compression
        // is the strongest "this is water" cue available here - a pure offset
        // just slides the checkers, which look nearly the same either way.
        // A slight horizontal squeeze plus the offset: enough to visibly bend
        // the pool, not enough to turn the crown into a fishbowl of stripes.
        vec2 lens = suv - 0.5;
        float zoomX = 0.88 + 0.06 * n1 + 0.04 * sin(vAngle * TAU * 4.0 - uTime * 1.7);
        vec2 uvr = clamp(0.5 + vec2(lens.x * zoomX, lens.y * 0.985) + refrOff,
                         vec2(0.002), vec2(0.998));
        behind = texture(uScene, uvr).rgb;
    } else {
        vec3 rt = refract(-V, Np, 0.75);
        behind = roomRadiance(rt);
    }

    // The sheet's water path: thin at the torn rim, a real column at the foot.
    // Against a white-tiled room this absorption is most of what makes the
    // crown visible at all - a white sheet on a white pool has no contrast,
    // while real water reads a shade darker and bluer with bright rims.
    float thick = mix(1.55, 0.30, vParam) * (0.50 + 0.55 * vLigament);
    vec3 tint = exp(-vec3(0.46, 0.150, 0.085) * thick);

    vec3 refr = behind * tint;
    vec3 refl = roomRadiance(reflect(-V, Np)) * 1.05;

    // Air entrained by the impact. A crown splash is not a sheet of glass: the
    // water thrown up by a pot is churned full of bubbles, so it reads as a
    // white sheet with clear holes rather than a transparent film. This is the
    // whole difference between "fog" and "water" here.
    // Stretched along the sheet: churned water runs vertically, and a noise
    // lattice sampled evenly in both directions shows up as a woven texture.
    float churn = vnoise(vec2(vAngle * TAU * 21.0,
                              vParam * 15.0 - uTime * 2.6));
    churn = churn * 0.62 + 0.38 * vnoise(vec2(vAngle * TAU * 47.0 - uTime * 1.3,
                                              vParam * 34.0 - uTime * 4.0));
    // Entrained air covers most of the sheet: water thrown up by a falling pot
    // is churned nearly white until it thins out at the torn tips.
    float aer = 0.34 + 0.46 * smoothstep(0.12, 0.82, churn);
    aer *= mix(0.84, 1.0, vTip);
    aer = clamp(aer * (0.82 + 0.18 * vLigament), 0.0, 0.96);


    // Clear water: face-on you look straight through to the pool, at grazing
    // angles the sheet turns into a mirror of the bright room.
    // Whitewater has to sit *above* the pool's own brightness: the room's tiles
    // are already near white, so foam at 0.9 is invisible against them. Real
    // backlit spray is far brighter than what it covers, and the shared HDR
    // pipeline then blooms it the way a camera would.
    vec3 foamCol = vec3(2.20, 2.28, 2.38) * (0.72 + 0.55 * vTip);
    vec3 col = mix(refr, refl, fres * 0.90);
    col = mix(col, foamCol, aer);

    // Light and shade across the pleats. A real sheet is lit unevenly: the
    // folds turned toward the ceiling panel flare almost white, the ones turned
    // away go blue-grey. Without this the crown is one flat value that reads as
    // plastic no matter how correct the refraction is.
    float ndl = clamp(dot(Np, L), -1.0, 1.0);
    col *= 0.52 + 0.90 * (0.5 + 0.5 * ndl);
    col += uLightTint * pow(max(ndl, 0.0), 3.0) * (0.10 + 0.28 * vTip);

    // Narrow specular lobe - a water film, not a mirror ball.
    float a2 = 0.040 * 0.040;
    float glint = D_GGX(NoH, a2) * V_SmithGGX(NoV, NoL, a2) * F_Schlick(NoH, 0.02);
    col += uLightTint * min(glint * 3.2, 2.4);
    float sparkle = pow(NoH, 240.0) * gate;
    col += uLightTint * sparkle * 1.4;

    // Water runs down the sheet in vertical ribbons.
    vec2 fq = vec2(vAngle * TAU * 44.0, vParam * 2.5 - uTime * 5.0);
    float fgate = 1.0 - smoothstep(0.30, 1.10, fwidth(fq.x) + fwidth(fq.y) + 1e-4);
    float flow = mix(0.5, vnoise(fq), fgate);
    col *= 0.90 + 0.20 * flow;

    // Air entrained where the sheet meets the pool: a broken whitewater ring in
    // the lowest few centimetres, not a haze over the whole crown.
    float base = 1.0 - smoothstep(0.0, 0.22, vParam);
    float broken = smoothstep(0.30, 0.72, vnoise(vec2(vAngle * TAU * 26.0,
                                                     vParam * 34.0 - uTime * 2.0)));
    float aeration = base * base * (0.35 + 0.65 * broken) + base * 0.18;
    vec3 whitewater = mix(vec3(1.35, 1.45, 1.55), vec3(2.35, 2.42, 2.50),
                          clamp(0.30 + 0.70 * NoV, 0.0, 1.0));
    col = mix(col, whitewater, clamp(aeration, 0.0, 0.85) * 0.85);

    // A thin dark line where the sheet leaves the pool: without it the crown
    // fades into the foam instead of standing in the water.
    float contact = exp(-pow((vParam - 0.22) / 0.10, 2.0));
    col *= 1.0 - 0.30 * contact * (1.0 - fres);

    // The torn edge catches a thin bright line where the sheet is about to
    // break into droplets.
    float rimEdge = smoothstep(0.72, 1.0, vParam) * vLigament;
    col += vec3(0.95, 0.98, 1.0) * rimEdge * 0.55;

    // Water path thickness: a curtain of water is mostly a thin, see-through
    // film. Opacity is what makes a sheet look like plastic.
    // Aerated water is opaque: spray hides what is behind it. A translucent
    // white sheet just dissolves into a white pool.
    float body = 0.42 + 0.26 * fres + aer * 0.38;
    body *= 0.70 + 0.45 * vLigament;
    body *= 1.0 - 0.20 * vParam;
    body += aeration * 0.30 + rimEdge * 0.16;
    body *= 0.86 + 0.24 * flow;
    // Break the very top of the sheet up: a hard geometric rim is what makes a
    // crown look cut out of plastic.
    // A crown sheet is thick and solid where it leaves the water and thins to
    // nothing at the tips, where it tears into droplets. Without this falloff
    // the upper edge stays a hard sheet and reads as carved crystal.
    body *= mix(1.0, 0.50, smoothstep(0.45, 1.0, vParam));
    body *= mix(1.0, 0.12, smoothstep(0.82, 1.0, vParam));
    // Dense foam at the foot. This is what attaches the crown to the pool: a
    // clean glassy rim at the waterline is the single strongest reason a crown
    // looks placed on the surface rather than torn out of it.
    float footFoam = 1.0 - smoothstep(0.0, 0.20, vParam);
    body = mix(body, 0.98, footFoam * 0.92);
    col = mix(col, vec3(2.30, 2.38, 2.46), footFoam * 0.88);
    // Where the ligament field has died, the wall has torn away completely.
    body *= 0.62 + 0.38 * smoothstep(0.02, 0.20, vLigament);
    // Above the base the sheet has already broken into separate fingers: the
    // gaps between them are real holes, not just thinner film. Keeping the wall
    // solid to the top is what made the crown read as a bowl standing in the
    // pool instead of water thrown up out of it.
    float strips = vnoise(vec2(vAngle * TAU * 34.0 - uTime * 0.7, vParam * 3.0));
    float fingers = smoothstep(0.22, 0.62, strips * 0.55 + vLigament * 0.45);
    body *= mix(1.0, 0.10 + 0.90 * fingers, smoothstep(0.22, 0.60, vParam));
    float alpha = clamp(body, 0.0, 0.92);

    // Distant crowns dissolve completely: a half-faded splash reads as a grey
    // smudge over the tiles, and at that size there is no detail left to show.
    alpha *= 1.0 - 0.88 * smoothstep(9.0, 22.0, camDist);

    if (uJet > 0.5) {
        // The re-entry jet is aerated whitewater, not a glass cone.
        float aerated = 0.35 + 0.65 * n1;
        vec3 jcol = mix(refr * 0.90, vec3(0.94, 0.97, 1.0), aerated * 0.70);
        jcol += uLightTint * min(glint * 2.0, 2.0);
        float jalpha = (0.30 + 0.35 * aerated) * (1.0 - 0.30 * vParam);
        jalpha *= 1.0 - 0.80 * smoothstep(10.0, 23.0, camDist);
        fragColor = vec4(jcol, clamp(jalpha, 0.0, 0.90));
        return;
    }
    fragColor = vec4(col, alpha);
}
