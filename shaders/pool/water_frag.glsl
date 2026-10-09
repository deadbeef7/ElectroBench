#version 330 core


#define MAX_RINGS 54


#define MAX_HULLS 18


#define MAX_BUBBLES 48


in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uTileA;
uniform vec3 uTileB;
uniform float uTime;
uniform vec4  uRings[MAX_RINGS];
uniform vec4  uHulls[MAX_HULLS];


uniform vec4  uSplashes[MAX_HULLS];


uniform vec4  uBubbles[MAX_BUBBLES];


uniform int   uBubbleCount;

out vec4 fragColor;

vec3 normalize3(vec3 v) { return v / max(length(v), 1e-5); }


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

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}


float octaveRes(float foot, float freq) {
    return 1.0 - smoothstep(0.35, 1.10, foot * freq);
}


float F_Schlick(float u, float F0) {


    float f = 1.0 - u;
    float f2 = f * f;
    float r = f2 * f2 * f;
    return F0 + (1.0 - F0) * r;
}


float checker(vec2 p) {


    vec2 w = fwidth(p) + 1e-4;
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5)
                  - abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}


float panelRadiance(vec3 rd, vec3 Ld) {
    vec3 tUp = abs(Ld.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 tx = normalize(cross(tUp, Ld));
    vec3 ty = cross(Ld, tx);
    float cosA = max(dot(rd, Ld), 0.08);
    vec2 q = vec2(dot(rd, tx), dot(rd, ty)) / cosA;
    float box = max(abs(q.x), abs(q.y));


    float edgeT = smoothstep(0.30, 0.165, box);
    float panel = edgeT * (0.62 + 0.38 * smoothstep(0.015, 0.24, edgeT));
    float louvre = 0.5 + 0.5 * cos(q.x * 210.0);
    panel *= 0.86 + 0.14 * louvre;
    panel *= smoothstep(0.300, 0.258, box);
    float mull = smoothstep(0.030, 0.012, abs(q.x))
               + smoothstep(0.030, 0.012, abs(q.y));
    panel *= clamp(1.0 - mull * 0.85, 0.0, 1.0);
    return panel + 0.055 * smoothstep(0.78, 0.30, box);
}


vec3 reflectedCheckerColor(vec3 dirToViewer, vec3 pos, vec3 surfN) {


    vec3 rd = reflect(dirToViewer, surfN);
    rd = normalize(rd);
    rd.y = abs(rd.y) * 0.85 + 0.02;
    float up = clamp(rd.y, 0.02, 1.0);
    vec2 plane = vec2(atan(rd.x, rd.z), asin(clamp(up, -1.0, 1.0))) * 4.0;


    float c = checker(plane + vec2(uTime * 0.006, 0.0));
    vec3 albedo = mix(uTileB, uTileA, c);


    float fres = 0.35 + 0.65 * pow(1.0 - clamp(dot(-dirToViewer, vec3(0.0, 1.0, 0.0)), 0.0, 1.0), 1.5);
    vec3 out_ = albedo * fres;


    out_ += uLightTint * panelRadiance(normalize(rd), normalize(uLightDir)) * 14.0;
    return out_;
}


void main() {
    vec3 V = normalize(uEyePos - vWorld);
    float dist01 = clamp(length(uEyePos - vWorld) / 120.0, 0.0, 1.0);
    float NoV = clamp(dot(vec3(0.0, 1.0, 0.0), V), 1e-3, 1.0);


    float bump = 0.0;
    float foam = 0.0;
    float foamCrest = 0.0;
    for (int i = 0; i < MAX_RINGS; i++) {
        vec4 r = uRings[i];
        if (r.w <= 0.001) continue;
        float d = length(vWorld.xz - r.xy);
        float band = d - r.z;


        float width = 0.26 + r.z * 0.032;
        float ring = exp(-band * band / (width * width));
        bump += ring * r.w * 0.38;
        foam  += ring * r.w;


        float crest = exp(-band * band / (width * width * 0.9));
        foamCrest += crest * r.w * 0.30;


        float halo = exp(-band * band / (width * width * 8.0));
        foam += halo * r.w * r.w * 0.10;
    }


    float hullFoam = 0.0;
    float hullGhost = 0.0;
    float splashGlow = 0.0;
    float viewDist = length(uEyePos - vWorld);


    vec3 rdir = vec3(-V.x, V.y, -V.z);
    float elev = clamp(rdir.y / max(length(rdir.xz), 1e-4), 0.0, 1.5);
    float ground = clamp(1.0 - elev * 1.25, 0.0, 1.0);
    ground *= ground;
    vec2 dirXZ = normalize(rdir.xz + vec2(1e-5));
    for (int i = 0; i < MAX_HULLS; i++) {
        vec4 hu = uHulls[i];
        if (hu.w > 0.001) {
            float d = length(vWorld.xz - hu.xy);
            float rr = max(hu.z, 0.05);


            float collar = exp(-pow((d - rr) * (4.0 / rr), 2.0));
            hullFoam += collar * hu.w;

            bump += collar * hu.w * 0.25;


            vec2 anchor = hu.xy + dirXZ * (rr * 0.8);
            vec2 dv = vWorld.xz - anchor;
            float smear = rr * (0.55 + 1.6 * bump);
            float gh = ground * exp(-dot(dv, dv) / (smear * smear));
            gh *= 1.0 / (1.0 + viewDist * 0.045);
            hullGhost += gh * hu.w;
        }
        vec4 sp = uSplashes[i];
        if (sp.w > 0.001) {
            float d = length(vWorld.xz - sp.xy);


            float g = exp(-pow((d - sp.z) * (2.2 / max(sp.z, 0.1)), 2.0));
            splashGlow += g * sp.w;
        }
    }
    bump = clamp(bump, 0.0, 1.0);
    foam = clamp(foam, 0.0, 1.0);
    foamCrest = clamp(foamCrest, 0.0, 1.0);


    float swell = sin(vWorld.x * 1.9 + uTime * 0.9) * sin(vWorld.z * 1.5 - uTime * 0.7);
    float chop  = sin(vWorld.x * 7.3 + uTime * 2.1) * sin(vWorld.z * 6.1 - uTime * 1.7);
    chop = 0.5 + 0.5 * (0.35 * swell + chop);
    bump = clamp(bump + chop * 0.05, 0.0, 1.0);


    float ax = vWorld.x * 1.9 + uTime * 0.9;
    float az = vWorld.z * 1.5 - uTime * 0.7;
    float bx = vWorld.x * 7.3 + uTime * 2.1;
    float bz = vWorld.z * 6.1 - uTime * 1.7;
    float dSwellX = 1.9 * cos(ax) * sin(az);
    float dSwellZ = 1.5 * sin(ax) * cos(az);
    float dChopX  = 7.3 * cos(bx) * sin(bz);
    float dChopZ  = 6.1 * sin(bx) * cos(bz);


    float foot = fwidth(vWorld.x) + fwidth(vWorld.z) + 1e-4;
    float gSwell = octaveRes(foot, 1.9 / 6.2831853);
    float gChop  = octaveRes(foot, 7.3 / 6.2831853);
    float gFine  = octaveRes(foot, 19.0 / 6.2831853);
    float cx = vWorld.x * 19.0 + uTime * 4.7;
    float cz = vWorld.z * 16.3 - uTime * 4.1;
    float dFineX = 19.0 * cos(cx) * sin(cz);
    float dFineZ = 16.3 * sin(cx) * cos(cz);
    float slopeX = 0.5 * (0.35 * dSwellX * gSwell + dChopX * gChop + 0.13 * dFineX * gFine);
    float slopeZ = 0.5 * (0.35 * dSwellZ * gSwell + dChopZ * gChop + 0.13 * dFineZ * gFine);
    slopeX *= 0.030;
    slopeZ *= 0.030;
    vec3 Nw = normalize(vec3(-slopeX, 1.0, -slopeZ));
    float NoVw = clamp(dot(Nw, V), 1e-3, 1.0);


    vec3 L = normalize(uLightDir);
    vec3 H = normalize(V + L);
    float NoH = max(dot(Nw, H), 0.0);
    float NoL = max(dot(Nw, L), 0.0);
    float aGGX = mix(0.055, 0.16, bump);
    float a2 = aGGX * aGGX;
    float specCT = D_GGX(NoH, a2) * V_SmithGGX(NoVw, NoL, a2) * F_Schlick(NoH, 0.02);
    float spec = min(specCT * 0.9, 8.0) * 4.0;


    float lightFall = 1.0 - 0.45 * dist01;
    spec *= lightFall;
    float sheen = pow(NoH, 14.0) * 0.35 * lightFall;


    vec3 refl = reflectedCheckerColor(-V, vWorld, Nw);


    vec3 absorb = vec3(0.28, 0.07, 0.04);


    float path = length(uEyePos - vWorld) * 0.5 + 0.5;
    vec3 trans = exp(-absorb * path);
    vec3 scatter = vec3(0.085, 0.300, 0.470) * mix(vec3(1.0), trans, 0.55);
    vec3 body = mix(scatter, vec3(0.028, 0.150, 0.300), clamp(dist01, 0.0, 1.0));


    float subsurface = pow(max(dot(V, -normalize(uLightDir)), 0.0), 3.0) * bump * 0.20;
    body += uLightTint * subsurface * 0.10;


    float bubbleSpeck = 0.0;
    float hEye = max(uEyePos.y, 0.25);
    for (int i = 0; i < MAX_BUBBLES; i++) {
        if (i >= uBubbleCount) break;
        vec4 bb = uBubbles[i];
        if (bb.z <= 0.002 || bb.w <= 0.001) continue;
        float k = hEye / (hEye + bb.z);
        vec2 ap = uEyePos.xz + (bb.xy - uEyePos.xz) * k;
        float d2 = length(vWorld.xz - ap);


        float r = max(bb.w, 0.015) * 3.2;
        float vis = 1.0 / (1.0 + bb.z * 0.35);
        float core = 1.0 - d2 * d2 / (r * r);
        bubbleSpeck += max(core, 0.0) * vis;
    }
    bubbleSpeck = clamp(bubbleSpeck, 0.0, 1.0);


    float mirror = F_Schlick(NoVw, 0.02);
    mirror = clamp(mirror * 1.45, 0.05, 0.88);


    vec3 col = mix(body, refl, clamp(mirror, 0.0, 1.0));


    col = mix(col, vec3(0.72, 0.85, 0.90), bubbleSpeck * 0.75);


    col = mix(col, vec3(0.010, 0.014, 0.022), clamp(hullGhost * 1.3, 0.0, 1.0));
    col += vec3(0.90, 0.94, 1.0) * hullFoam * 0.22;
    col += uLightTint * splashGlow * 0.28;


    col *= vec3(0.97, 1.0, 1.02);
    col += uLightTint * (spec * 1.6 + sheen * 0.25 + chop * 0.018);


    float foamLit = foam * (0.55 + 0.45 * (0.5 + 0.5 * dot(V, normalize(uLightDir))));
    col += uLightTint * foamLit * 0.13;
    col += vec3(0.90, 0.94, 1.0) * foamCrest * 0.20;

    col += vec3(0.05, 0.004, 0.005);


    vec2 cp = vWorld.xz * 3.1;
    float web1 = 0.5 + 0.5 * sin(cp.x + sin(cp.y * 1.7 + uTime * 1.9) * 1.4);
    float web2 = 0.5 + 0.5 * sin(cp.y * 1.3 - uTime * 1.4 + sin(cp.x * 1.9 - uTime * 0.8) * 1.4);
    float caustic = pow(web1 * web2, 3.0);


    float bodyShimmer = max(web1 * web2 - 0.25, 0.0) * (1.0 - dist01) * 0.10;


    col += uLightTint * caustic * (0.08 + 0.28 * bump) * lightFall;
    col += uLightTint * bodyShimmer * 0.6;


    float dist = length(uEyePos - vWorld);
    float haze = 1.0 - exp(-dist * 0.004);
    vec3 hazeCol = vec3(0.30, 0.46, 0.56);


    col = mix(col, hazeCol, haze * 0.44);


    col *= 0.92;
    col = clamp((col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14), 0.0, 1.0);
    col = pow(col, vec3(1.0 / 1.15));


    float dith = (hash21(gl_FragCoord.xy + fract(uTime) * 13.0)
                 + hash21(gl_FragCoord.xy * 1.7 + 41.0) - 1.0) * 0.5;
    fragColor = vec4(clamp(col + dith * (1.5 / 255.0), 0.0, 1.0), 1.0);
}
