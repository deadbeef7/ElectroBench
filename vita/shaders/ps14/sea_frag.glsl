// GLSL 1.00-style rewrite for vitaGL/SceShaccCg (see that script's RULES); the
// math is identical to the desktop shader except for the documented rewrites.
#version 100
// Re-seeded from the v0.3 release (tag v0.3: shaders/ps14/sea_frag.glsl) and
// re-ported through the same rewrites. The deltas beyond the mechanical
// rewrite are all noted inline: MAX_CLOUDS raised to 9 to match
// src/scene2.cxx, the whole-stage highp below, v0.3's roughness-matched
// reflection LOD carried as the ES 1.00 textureCube LOD-bias overload, v0.3's
// glitter lobes restored verbatim (they only pixelate at mediump), and the
// cloud-shadow deck kept as the cheap envelope-only approximation - the puff
// erosion v0.3 used cost ~100 noise samples per fragment per cloud here,
// which pinned scene 2 to a few FPS on a phone and kept the Vita from coming
// up at all. The envelope tracks the same cloud uniforms, so the shadow still
// lands where the sky's clouds are.
//
// WHY THE WHOLE STAGE IS highp — the pixellated white sparkles in the sun path.
// The normal field below evaluates six analytic sines whose arguments run into
// the hundreds-to-thousands of radians: the sea patch is kSeaSize (4096) across,
// so the world coordinates reach +/-2048 and the phases reach ~2000. At a
// mediump default (fp16 — what both the phone and SceShaccCg compile this to)
// one ulp at 2048 is 0.25 rad, four times the 0.06 rad that separates the
// finite-difference taps at e=0.35. adjoint: the gradient stops being a//   derivative and becomes a staircase, and pow(NdH, 520) lights each step as
// isolated white pixel. That is the sparkle, and it is in BOTH archives: the
// VPK shows it exactly like the APK, which is the evidence that SceShaccCg does
// NOT strip the qualifier (as an earlier note in this tree assumed) but honours
// it.
// Measured on the host render, whose driver lowers mediump the same way, at the
// Vita's probe (256-px, 8-bit cube) and the desktop's 768: pixels with luma
// >170 that are also >45 brighter than all four neighbours, in the sea, fall
// from 280 (mediump) to 15 (highp) — and to 20 at highp on the 768 cube, i.e.
// the sparkle is gone and what is left is the same handful the fp32 reference
// has. The stage also renders 39% brighter at highp on that host, because the
// quantised haze and reflection terms were reading low; highp is what makes the
// phone and the console agree with the desktop, not a brightening on top.
// ES 3.0 mandates fragment highp and the sky shaders of both scenes already
// declare it, so this is the same line, not a new capability.
precision highp float;
// Fragment shader for the sea surface.
//
// Structure intentionally mirrors the phases of a shader_model 1.4 pixel
// shader (as used by the 3DMark2001 SE "Nature" ocean):
//   1) addressing  : sample ripple gradient texture -> perturbation vector
//   2) dependent read: perturbed coords -> environment reflection lookup
//   3) blend/address: add sun glitter, blend with deep-water color
// Six texture fetches per phase is the PS1.4 budget; this uses far fewer.
//
// Noise discipline: every term here is analytic or texture-chaos MODULATED by
// analytic gates. Free-floating glow terms (not tied to the sun path, cloud
// shadows or crest height) show up as pale-teal speckle over the dark water —
// they were the "light blue noise" artifact and must stay gated.
//
// Realism layer (this pass): on top of the displaced swell banks the shading
// adds two octaves of ANALYTIC detail wavelets (extra normals, tiny amplitude,
// no vertex cost), slope-gated crest foam (it appears where waves actually
// face the light and break — not as a uniform tile), sun-tinted glitter, and
// a sky-tinted horizon sheen so the far sea is a mirror of the sky band above
// it rather than dark water with a haze knob.

// These two are declared highp explicitly on BOTH stages even though the
// stage default above is now highp too: the declaration is what the ES 3.0
// linker checks for varying agreement between sea_vert and this stage, and the
// history is worth keeping. The sea patch is kSeaSize (4096) across, so
// vWorld carries coordinates of +/-2048; interpolated at mediump the far half
// of the patch collapsed - dist came back >= 16384 (mediump's maximum) for
// fragments whose vWorld was a perfectly ordinary (1847, 1044) - which drove
// horizonMix and haze to 1.0 over a 24-row band straddling the horizon while
// `body` stayed at its un-hazed value: the near-black band between the sea and
// the sky. At highp the same probes agree with the geometry (row 416 returns
// length(vWorld.xz) = 1606 and dist = 1574) and the haze ramps normally, so
// the water reaches the horizon.
varying highp vec3 vWorld;
varying highp vec2 vUV;

uniform sampler2D uRippleTex;    // two-channel ripple gradient (RG8)
uniform samplerCube uSkyEnvTex;  // sky environment cubemap (RGBA16F)
uniform sampler2D uFoamTex;      // tileable foam / caustic detail
// highp, explicitly, because this is the one uniform the sea's two stages share:
// sea_vert.glsl reads it for the wave displacement and this stage reads it for
// the foam/ripple scroll. A vertex shader's float default is highp while this
// stage's default is the mediump above, and an ES 3.0 linker rejects the pair
// for exactly that (Mesa: "declarations for uniform `uTime` have mismatching
// precision qualifiers") - which costs the whole sea program, silently, while
// the sky and HUD programs link fine because no uniform of theirs is shared.
uniform highp float uTime;
uniform vec3  uEyePos;
uniform vec3  uSunDir;
uniform vec3  uHorizonColor;
uniform vec3  uWaterColor;

// 1 = mirror the environment cubemap, 0 = evaluate the same sky gradient in
// this stage (see analyticSky). The Vita takes 0: its cube is filled by a
// readback + upload per face and cannot carry a mip chain, so base-level
// sampling mirrored the sun disc and the cloud silhouettes onto the water as
// flat, hard-edged sheets that sit still while the camera orbits, i.e. a
// reflection that reads as broken instead of as water. The other ports keep
// the cube (mipmapped, 16F) and are bit-identical to before.
uniform float uEnvCube;
// sky_frag.glsl's palette, verbatim, so the analytic reflection cannot drift
// from the sky actually drawn above the water.
uniform vec3  uSkyZenith;
uniform vec3  uSkyMid;
uniform vec3  uSkyHorizon;

// 9, not v0.3's 7: src/scene2.cxx uploads nine clouds' worth of uniforms
// (MAX_CLOUDS is 9 there and in sky_frag.glsl, which the sea's shadow deck
// must track). An array smaller than the upload makes every glUniform1fv
// GL_INVALID_OPERATION, so the cloud-shadow uniforms would silently stay zero.
#define MAX_CLOUDS 9             // must match shaders/ps14/sky_frag.glsl and src/scene2.cxx
uniform int   uCloudCount;
uniform float uCloudAzim[MAX_CLOUDS];   // centre azimuth, radians
uniform float uCloudElev[MAX_CLOUDS];   // centre elevation, radians
uniform float uCloudRadius[MAX_CLOUDS]; // angular half-height, radians
uniform float uCloudStretch[MAX_CLOUDS];// azimuthal elongation (>1 = wider than tall)



const float PI = 3.14159265359;

// NORMAL FIELD: smooth sines (finite differences of this drive the lighting).
// Scale/speed of each line must match the DISPLACEMENT field in sea_vert.glsl
// so crest banks light up where the geometry actually rises. The displacement
// shader additionally sharpens crests and adds two long swells; that asymmetry
// is deliberate (sharp banks, smooth lighting).
float waveHeight(vec2 p, float t) {
    float h = 0.0;
    h += sin(dot(p, vec2(0.98,  0.20)) * 0.170 + t * 1.30) * 1.55;
    h += sin(dot(p, vec2(-0.64,  0.77)) * 0.240 + t * 1.60) * 1.00;
    h += sin(dot(p, vec2( 0.36, -0.93)) * 0.380 + t * 2.10) * 0.55;
    h += sin(dot(p, vec2(-0.91, -0.42)) * 0.540 + t * 2.70) * 0.34;
    h += sin(dot(p, vec2( 0.59,  0.81)) * 0.860 + t * 3.40) * 0.20;
    h += sin(dot(p, vec2(-0.20,  0.98)) * 1.450 + t * 4.40) * 0.11;
    return h;
}

// CLOUD SHADOWS: intersect each water fragment's sun ray with the same 620 m
// cloud deck used by the sky. The shadow only needs the deck's COVERAGE, so
// this uses the cloud's elliptical envelope: the eight-lobe erosion used for
// the visible sky silhouette cost ~100 noise samples per fragment here (and
// nine clouds of it per pixel), which is what pinned scene 2 to a few FPS on
// a phone and kept it from coming up on the Vita at all. The shadow is soft
// by nature, so the extra silhouette detail never read anyway.
float cloudSmoothMax(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (a - b) / k, 0.0, 1.0);
    return mix(b, a, h) + k * h * (1.0 - h);
}

float projectedCloudDensity(vec3 dir) {
    float density = 0.0;
    dir = normalize(dir);
    for (int i = 0; i < MAX_CLOUDS; i++) {
        if (i >= uCloudCount) break;
        vec3 c = vec3(cos(uCloudElev[i]) * cos(uCloudAzim[i]),
                      sin(uCloudElev[i]),
                      cos(uCloudElev[i]) * sin(uCloudAzim[i]));
        vec3 delta = dir - c;
        vec3 t1 = normalize(vec3(-sin(c.z), 0.0, cos(c.z)));
        vec3 t2 = normalize(cross(c, t1));
        vec2 q = vec2(dot(delta, t1) / max(uCloudStretch[i], 1.0),
                      dot(delta, t2)) / max(uCloudRadius[i], 0.001);
        float r2 = dot(q, q);
        if (r2 > 3.1) continue;
        // 0.55: the envelope represents the cloud's *average* optical depth over
        // its footprint, where the visible sky can sit at 1.0 in the puffs. At
        // full density the shadow floor (exp(-2.4) clamped to 0.12) painted
        // whole bays near-black; half density keeps a readable shadow.
        float local = 0.55 * (1.0 - smoothstep(0.35, 1.25, sqrt(r2)));
        density = cloudSmoothMax(density, local, 0.07);
    }
    return clamp(density, 0.0, 1.0);
}

float cloudShadow(vec3 world, vec3 sd) {
    float travel = max((620.0 - world.y) / max(sd.y, 0.15), 0.0);
    vec3 hitDir = normalize(world - uEyePos + sd * travel);
    float local = projectedCloudDensity(hitDir);
    return clamp(exp(-local * 2.4), 0.12, 1.0);
}

// Three octaves of small analytic wavelets: extra normal detail that would
// be wasted (and aliased) as vertex displacement, but sells micro-chop up
// close. Frequencies chosen so screen-space wavelength stays > ~4px at typical
// orbit distances — content finer than that reads as noise on real GPUs.
void detailNormals(vec2 p, float t, float dist, inout vec2 grad) {
    float fade = exp(-dist * 0.004);   // micro-chop is a NEAR-camera feature
    if (fade < 0.02) return;
    float w1 = sin(dot(p, vec2(0.86, 0.51)) * 1.15 + t * 5.10);
    float w2 = sin(dot(p, vec2(-0.44, 0.90)) * 2.30 + t * 6.80);
    float w3 = sin(dot(p, vec2(0.22, -0.97)) * 4.40 + t * 8.60);
    grad += vec2(0.86, 0.51) * 1.15 * w1 * 0.045;
    grad += vec2(-0.44, 0.90) * 2.30 * w2 * 0.022;
    grad += vec2(0.22, -0.97) * 4.40 * w3 * 0.010;
}

// Analytic twin of sky_frag.glsl's skyGradient() plus its glow terms. Cloud
// silhouettes are deliberately left out: this path exists for the port whose
// reflection probe has no mip chain, and a rough sea's reflection is a blurred
// sky there too, not a sharp one.
//
// The three horizon terms of the sky stage ARE here, though, and that was the
// fix for the hard line where the water meets the sky. sky_frag.glsl draws
// skyGradient() (which carries the +8% low-altitude boost) and then adds the
// warm dust band `vec3(0.16, 0.075, 0.055) * (1 - smoothstep(0, 0.05, h))`
// under the cloud deck. Both are a large fraction of the sky's own colour in
// the first few degrees above the horizon, and this twin had neither: the far
// sea - which is ~80% skyAtHorizon after the haze mix - landed about 30 luma
// below the sky it is supposed to mirror, i.e. a dark straight edge along the
// waterline instead of a continuous band. Everything below is skyGradient()'s
// structure with the sky stage's own numbers.
vec3 analyticSky(vec3 dir, vec3 sd) {
    float h = clamp(dir.y, 0.0, 1.0);
    vec3 s = mix(uSkyHorizon, uSkyMid, smoothstep(0.0, 0.14, h));
    s = mix(s, uSkyZenith, smoothstep(0.10, 0.38, h));
    float sa = max(dot(dir, sd), 0.0);
    float shadowedWarmth = 0.22 + 0.78 * pow(sa, 4.0);
    float horizonBoost = smoothstep(0.0, 0.20, h) * (1.0 - smoothstep(0.20, 0.60, h));
    s *= shadowedWarmth + 0.08 * horizonBoost;
    // The dust band the sky stage adds on top of the gradient (verbatim).
    s += vec3(0.16, 0.075, 0.055) * (1.0 - smoothstep(0.0, 0.05, h)) *
         (0.45 + 0.55 * pow(sa, 2.0));
    float sunAng = length(dir - sd);
    float glow = exp(-11.0 * sunAng * sunAng) * 0.42
               + exp(-25.0 * sunAng * sunAng) * 0.62;
    glow *= 1.0 - smoothstep(0.05, 0.55, h) * 0.85;
    s = mix(s, vec3(1.55, 0.52, 0.18), clamp(glow, 0.0, 0.52));
    return s;
}

void main() {
    vec3 sd = normalize(uSunDir);
    vec3 dir = normalize(vWorld - uEyePos);   // fragment direction (eye -> point)

    // ---- analytic wave normal (finite differences of the wave field) ----
    float dist = length(vWorld.xz - uEyePos.xz);
    float e = 0.35;
    float hC = waveHeight(vWorld.xz, uTime);
    float hX = waveHeight(vWorld.xz + vec2(e, 0.0), uTime);
    float hZ = waveHeight(vWorld.xz + vec2(0.0, e), uTime);
    vec2 grad = vec2(-(hX - hC) / e, -(hZ - hC) / e);
    detailNormals(vWorld.xz, uTime, dist, grad);
    vec3 N = normalize(vec3(grad.x, 1.0, grad.y));

    // ---- phase 1: addressing - ripple normal map, scrolled over the surface
    float rippleFade = exp(-dist * 0.0018);           // ripples die out far away
    vec2 uvA = vUV * 40.0 + vec2(uTime * 0.0040, uTime * 0.0031);
    vec2 uvB = vUV * 97.0 - vec2(uTime * 0.0026, uTime * 0.0042);
    vec2 ripA = texture2D(uRippleTex, uvA).rg;
    vec2 ripB = texture2D(uRippleTex, uvB).rg;
    vec2 pert = (ripA + ripB) * 0.5 * rippleFade;

    // ---- phase 2: dependent read - perturbed reflection of the sky ----
    vec3 V = normalize(uEyePos - vWorld);             // towards the eye
    vec3 R = reflect(-V, N);
    R = normalize(R + vec3(pert.x, 0.0, pert.y) * 1.25);
    // Reflections stretch vertically (the classic flattened-reflection trick):
    // grazing rays would otherwise hug the bright horizon band and light the
    // whole sea up. Biasing the ray up makes off-sun water reflect the dark
    // upper sky while the sun glitter path stays put (it is a separate term).
    R.y = abs(R.y) * 0.30 + 0.45;
    R = normalize(R);
    // azimuthal smear toward the sun: only rays near the SUN azimuth keep
    // crisp reflections; off-path rays mirror-blend toward the dark upper sky
    // so the glow column stays narrow and the sides read deep blue/purple
    vec3 L = normalize(uSunDir);
    vec2 sunXZ = normalize(L.xz);
    vec2 dirXZ = normalize(vWorld.xz - uEyePos.xz + vec2(1e-4));
    float sunAlign = max(dot(dirXZ, sunXZ), 0.0);
    vec3 Rdark = normalize(vec3(R.x, abs(R.y) * 1.8 + 0.62, R.z)); // steep: upper sky
    R = normalize(mix(Rdark, R, pow(sunAlign, 6.0)));

    // Roughness-matched reflection LOD, restored from v0.3. The sun disc
    // occupies a handful of cubemap texels, and sampling them at LOD 0 mirrors
    // them as small SQUARE patches on the water - the glitched blocks inside
    // the sun-lit path. Wave facets are rough at every distance, so the
    // reflection blurs with range, which also smears the sun into a soft
    // vertical glow (real water behaviour) instead of texel squares. v0.3
    // wrote this as
    //   textureLod(uSkyEnvTex, R, clamp(1.5 + reflDist * 0.0012, 1.0, 5.0));
    // ES 1.00 has no textureLod in a fragment stage, but the LOD-bias overload
    // of textureCube carries the same request - vitaGL maps it to Cg
    // texCUBEbias for SceShaccCg. The Vita's cube has no mip chain (vitaGL has
    // no cube mipmaps), so there the bias clamps to base level and is a no-op;
    // on the phone and the host the cube is mipmapped and the squares go. A
    // multi-tap BLUR of this cube was also tried and reverted: it lifts the
    // whole sea by a third by averaging the bright horizon band into every
    // off-sun reflection, and the LOD ramp above is where the eye actually
    // sees the texels.
    float reflDist = dist;
    // The blur is a RANGE ramp, not v0.3's 1.5 minimum everywhere: a 1.5-floor
    // mip pulls the mip-averaged warm horizon band into every reflected pixel,
    // which reads as a horizontal orange zone across the far sea. Near the
    // camera the reflection stays sharp; the square texels only appear where
    // the reflection compresses, which is where the ramp has already blurred.
    vec3 reflColor;
    if (uEnvCube > 0.5) {
        reflColor = textureCube(uSkyEnvTex, R,
            clamp((reflDist - 250.0) * 0.0014, 0.0, 2.5)).rgb;
    } else {
        reflColor = analyticSky(R, sd);
    }
    float offSun = 1.0 - smoothstep(0.08, 0.45, sunAlign);
    reflColor *= mix(1.0, 0.46, offSun);

    // ---- fresnel: sea is a mirror at grazing angles, glass straight down ----
    float NdV = max(dot(N, V), 0.0);
    float fresnel = 0.022 + 0.978 * pow(1.0 - NdV, 5.0);
    // stronger sky reflections: at dusk the whole water surface reads as a
    // dark mirror of the sky — lift the base fresnel and clamp the minimum
    // reflectance higher than the physical 2%
    fresnel = clamp(fresnel * 1.25 + 0.045, 0.0, 0.92);

    // ---- water body: near-black purple deep, warmed by the sky band ----
    vec3 body = uWaterColor * 0.55 + uHorizonColor * 0.03;

    // directional sun lighting on the wave slopes: faces tilted toward the
    // low sun glow warm, backslopes fall to near-black — this is what makes
    // the sea read as lit by the same sun as the sky instead of pasted on.
    // Prefaced by an azimuth gate so the WARM slope light lives inside the
    // sun path; off-path water stays deep blue/purple.
    float sunDiffuse = max(dot(N, L), 0.0);
    float warmGate = pow(sunAlign, 3.0);

    // cloud shadows: the projected puffs gate ALL direct sun terms
    float shadow = cloudShadow(vWorld, sd);

    // MUCH darker water where the sun's light doesn't reach: off-path base
    // drops to near-black indigo, and cloud shadows multiply direct light
    body *= 0.44 + 0.40 * sunDiffuse * warmGate * shadow + 0.12 * sunDiffuse * (0.35 + 0.65 * shadow);
    body *= mix(0.40, 1.0, warmGate * shadow + (1.0 - warmGate) * 0.25 * shadow); // dark off-path + shadowed body
    body *= mix(0.52, 1.0, 1.0 - offSun);
    body += vec3(1.05, 0.42, 0.20) * pow(sunDiffuse, 3.0) * warmGate * shadow * 0.42; // warm slopes in the path

    // ---- slope-gated crest foam ----
    // Foam only where the shading says waves actually BREAK: a height band
    // AND the slope facing the sun/light AND some ripple chaos — the noise
    // tile alone would foam uniformly everywhere, which reads fake.
    // Cloud shadows gate foam too: nothing breaks where light doesn't reach.
    float crest = smoothstep(0.55, 1.25, hC) * mix(0.25, 1.0, shadow);
    float slopeFacing = max(dot(N, L), 0.0) * warmGate + 0.15;
    float chaos = texture2D(uRippleTex, vUV * 190.0 + vec2(uTime * 0.011, -uTime * 0.007)).g;
    // 92 / 161 rather than v0.3's 23 / 41: the patch is 4096 m across, so the
    // old tiling put one foam texel every 0.7 m and 0.4 m - a crest a few
    // metres wide then held two or three texels of a smooth noise field, i.e.
    // one flat value, and every crest rendered as a flat pale card instead of
    // foam. At 0.17 m and 0.10 m the same crest carries real texture, which is
    // what makes it read as broken water.
    // Zero-mean, so that a texture which samples as a CONSTANT cannot paint
    // foam: 4*(a-0.5)*(b-0.5) is 0 for any flat sample and positive only where
    // both layers are dense. That matters because the crest gate below is the
    // only other factor: a sampler that returns one value across the surface
    // (a unit the device did not bind, or a mip level chosen once for a whole
    // crest) then turned every crest into a flat pale card on the water - the
    // pale patches on both sides of the frame. With the zero-mean form the
    // texture has to *vary* for foam to exist at all.
    float foam = max(texture2D(uFoamTex, vUV * 92.0 + vec2(uTime * 0.010, 0.0)).r - 0.5, 0.0)
               * max(texture2D(uFoamTex, vUV * 161.0 - vec2(0.0, uTime * 0.013)).g - 0.5, 0.0)
               * 4.0;
    // Foam is a NEAR-field feature. Past a few hundred metres a crest is
    // narrower than a pixel: the foam texture can only be resolved as its mip
    // average, the crest gate stops varying, and the additive term below then
    // paints flat pale plates on the water instead of foam - the pale patches
    // on both sides of the frame on the console, where the grazing-angle mip
    // selection lands on one level across whole crests. Fading it out by 260 m
    // leaves the far sea to the reflection and the haze, which are continuous.
    float foamFade = 1.0 - smoothstep(70.0, 260.0, dist);
    foam *= crest * slopeFacing * (0.35 + 0.65 * chaos) * foamFade;
    // Foam brightens the water's own shading rather than replacing it. As a
    // pure additive term at the old gain (0.55 of a pale lavender over a body
    // of ~0.05) it flattened the wave's light inside every crest, which is what
    // made the patches read as cards laid on the water rather than as foam.
    body *= 1.0 + foam * 0.70;
    body += vec3(0.48, 0.43, 0.42) * foam * 0.08; // dim warm-gray foam

    // subsurface glow against the light: THIN CRESTS transmit a dim jade-green
    // where sunlight actually passes through the water. Gated to the sun's
    // azimuth AND to un-shadowed sun — ungated it speckled pale-teal noise
    // across the dark off-path sea (the light-blue artifact).
    body += vec3(0.035, 0.095, 0.075) * sunAlign * warmGate * shadow * crest * 0.30;

    vec3 color = mix(body, reflColor, fresnel);

    // ---- horizon sheen: the far sea is a MIRROR of the sky band above it
    // (grazing fresnel -> nearly pure reflection up there), so by geometry
    // the water edge converges into the sky with no haze knob needed.
    vec2 dxdz = vWorld.xz - uEyePos.xz;
    vec3 horizonDir = normalize(vec3(dxdz.x, 0.012, dxdz.y));
    vec3 skyAtHorizon;
    if (uEnvCube > 0.5) {
        skyAtHorizon = textureCube(uSkyEnvTex, horizonDir).rgb;
    } else {
        skyAtHorizon = analyticSky(horizonDir, sd);
    }
    float horizonMix = smoothstep(1500.0, 1900.0, dist);
    color = mix(color, skyAtHorizon, horizonMix * 0.85);

    // ---- aerial haze: near water stays dark and readable, while the far
    // sea melts into the horizon. The haze target is the ACTUAL sky colour
    // at this fragment's azimuth — sampled from the env cubemap just above
    // the horizon line — so the far edge of the patch converges into the sky
    // band above it (bright glow on the sun side, dark maroon away from it).
    float haze = 1.0 - exp(-dist * 0.00075);
    color = mix(color, skyAtHorizon, haze * (0.30 + 0.70 * haze));

    // ---- phase 3: address + blend - sun glitter path ----
    // v0.3's three hand-fitted lobes, restored: a tight sparkle core (520), a
    // grainy mid falloff (90) and a soft sheen (14), gated to the sun's azimuth
    // column so the glow stays a narrow path with dark water either side,
    // killed inside cloud shadows, tinted orange, with the sparkle variance
    // riding the ripple chaos. What made these lobes pixelate here but not on
    // the reference is precision, not the exponents: at a mediump default
    // (fp16) pow(NdH, 520) steps in 40% jumps - each quantised wave normal
    // lights one isolated white pixel, which is the sparkle. The whole-stage
    // highp above puts this arithmetic back at fp32-class precision, where the
    // 520 lobe is the smooth pinpoint the reference shows, so the exponents
    // stay exactly as v0.3 had them.
    vec3 H = normalize(L + V);
    float NdH = max(dot(N, H), 0.0);
    float pathGate = pow(sunAlign, 10.0) * 0.96 + 0.04;
    // The chaos gate and the 520 lobe only make sense where a pixel covers
    // several ripple wavelengths and the lobe is wider than a pixel. Past that
    // they are pure aliasing: isolated orange dots and orange streaks riding the
    // path. Both fade out with range; the 90 and 14 lobes carry the path there.
    float gateFade = smoothstep(250.0, 1100.0, dist);
    float tightFade = 1.0 - smoothstep(300.0, 1200.0, dist);
    float sparkleGate = mix(0.55 + 0.90 * chaos, 1.0, gateFade);
    float glint = pow(NdH, 360.0) * 2.4 * tightFade; // pinpoint sparkles
    float glintMid = pow(NdH, 72.0) * 0.32;          // mid falloff keeps it grainy
    float glintWide = pow(NdH, 12.0) * 0.14;         // soft sheen around the path
    color += vec3(1.0, 0.64, 0.38) * (glint + glintMid + glintWide * pathGate)
             * (0.25 + max(L.y, 0.0) * 1.2) * pathGate * shadow * sparkleGate;

    // HDR tone map + gamma
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    gl_FragColor = vec4(color, 1.0);
}
