#version 330 core
// SCENE 4 (power lines): the sky.
//
// BUILD-P10 — the old dome was a three-stop gradient (amber zenith, peach
// middle, cream horizon) with a soft fbm cloud smeared over it. It was the
// most obviously synthetic part of the frame: a real dusk sky is not a colour
// ramp, it is an ATMOSPHERE, and the ramp has no sun in it.
//
// This version is a single-scattering analytic atmosphere:
//   * Rayleigh scattering (molecular, 1/lambda^4) — the blue that survives
//     overhead and dies first toward the horizon, which is WHY a low sun makes
//     the horizon orange rather than the zenith;
//   * Mie scattering (aerosol, forward-peaked Henyey-Greenstein, g = 0.76) —
//     the aureole around the sun, and the cream haze band the corridor
//     silhouettes against;
//   * the sun's own extinction along ITS path, so as uSunDir sinks through
//     the run the light reddens on its own instead of a keyframed palette;
//   * a small multiple-scattering floor, without which a single-scatter model
//     at sunset predicts a monochrome red sky with no blue anywhere.
// The optical depth along a ray is its air mass, 1/(h + 0.14), so the whole
// model is a few dozen flops — affordable on a pre-SSE CPU, and the reason it
// can share the frame with the object pass.
//
// The clouds are still fully analytic (no textures): three decks at different
// scales and drift rates for parallax, a density term eroded by a finer
// octave so the edges billow instead of fading, a sun-lit crown, a warm
// shadowed belly, and a silver lining where the cloud is thin AND the sun is
// behind it — the single most recognisable thing about a backlit cumulus.

in vec3 vDir;                 // world position on the dome (radius 400)

out vec4 fragColor;

uniform vec3  uEyePos;        // the ray origin: dir = vDir - uEyePos
uniform vec3  uSunDir;        // normalised, late-afternoon low sun
uniform float uTime;

// 2D value noise with cubic smoothing. The hash is a fract/dot chain, NOT
// the sin-based one from the pool scene: this dome covers half the frame on
// a pre-SSE CPU and sin() costs ~20 cycles there.
float vhash(vec2 p) {
    vec3 q = fract(vec3(p.x, p.y, p.x) * 0.1031);
    q += dot(q, q.yzx + 33.33);
    return fract((q.x + q.y) * q.z);
}
float vnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = vhash(i);
    float b = vhash(i + vec2(1.0, 0.0));
    float c = vhash(i + vec2(0.0, 1.0));
    float d = vhash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm2(vec2 p) {
    return vnoise(p) * 0.66 + vnoise(p * 2.17 + 19.3) * 0.34;
}
float fbm3(vec2 p) {
    return vnoise(p) * 0.54 + vnoise(p * 2.13 + 11.7) * 0.29
         + vnoise(p * 4.31 + 41.2) * 0.17;
}

// ---- scattering coefficients ----------------------------------------------
// Rayleigh in RGB: the real 5.8 : 13.5 : 33.1 x 1e-6 m^-1 ratio, i.e. the
// 1 : 2.3 : 5.7 that puts blue in the sky. Scaled to a convenient magnitude.
const vec3  kBetaR = vec3(0.058, 0.135, 0.331);
// Mie is the aerosol term, and it is GREY: it scatters every wavelength about
// equally, so wherever it is strong it DESATURATES the sky. That is why kBetaM
// is deliberately well below the Rayleigh figure for a hazy city air — the
// real aureole hugs the sun within ~10 degrees and the rest of the dome stays
// saturated, which is exactly what a high clean-air sunset looks like. The
// first pass of this shader used 0.0245 with a 1.25 gain and the whole sky
// came back cream: the aureole had swallowed the entire frame.
//
// BUILD-P15: 0.0125 -> 0.0092. This is the single most effective lever on
// "make the sky orange", because Mie is the ONLY grey in the model: every
// unit of it subtracted from the dome is saturation the airlight cannot buy
// back. Cutting it 26% moves the frame's mean saturation up without
// touching its brightness, which is what stops a warm sky from reading as a
// washed-out yellow one.
const float kBetaM = 0.0092;
const float kSunI  = 330.0;       // scaled solar radiance
// BUILD-P16: 9.8 -> 14.0, and the gain below 4.35 -> 13.0 to hold brightness.
// THE TONE MAP WAS THE PROBLEM, NOT THE PALETTE. P15's own comment claimed
// the airlight had been moved "from 35 deg to 24 deg, i.e. from amber to a
// proper sunset orange", and the shipped frame still measured 41 deg. The
// reason is that hue was being computed on the LINEAR value: this dome is
// authored at hue 11 deg — deeply orange, ratio 1.00 : 0.20 : 0.024 — and
// ACES + gamma 1/2.2 lifts the near-black green and blue channels far more
// than the bright red, displaying it at hue 36 deg. Gamma moves saturated
// reds toward yellow on the way to the screen, so a linear fix that looks
// right on paper reads amber on screen. Solving on the DISPLAYED value (see
// scripts/skytune.py, which reproduces the shipped frame to within 2 deg)
// moves the sun-path depth 9.8 -> 14.0 and the gain 4.35 -> 13.0, taking the
// frame from a measured 41.2 deg to a modelled 29.7 deg: orange. Hue 45 deg
// is where yellow starts, and the old dome was above it at every elevation
// the camera frames.
const float kSunPath = 14.0;      // sun-path optical depth multiplier
// The gain rises with the depth because deepening the extinction darkens the
// dome (mean displayed value 0.585 -> 0.555); this puts the red back without
// touching the green. Peak channel reaches 0.973, so nothing clips.
const float kRayGain = 13.0;      // single-scatter Rayleigh gain

// One analytic atmosphere evaluation. Also used by the object shader (wet road
// and window reflections mirror the real sky, not a guess at it) — the copy in
// object_frag.glsl is kept byte-identical on purpose, exactly like the pool
// scene's shared checker.
vec3 atmosphere(vec3 dir, vec3 sun) {
    float h = dir.y;
    float mu = dot(dir, sun);
    float mView = 1.0 / (max(h, 0.0) + 0.14);
    float mSun  = 1.0 / (max(sun.y, 0.0) + 0.14);

    // extinction along the view ray (red survives the long path, blue does not)
    vec3 Tview = exp(-(kBetaR + vec3(kBetaM)) * mView * 0.92);
    // extinction along the SUN's path: this is what reddens the light as the
    // sun sinks, and it is the whole reason a sunset goes orange by itself
    vec3 Tsun = exp(-kBetaR * mSun * kSunPath - vec3(kBetaM * mSun * 1.15));

    float phR = 0.0596831 * (1.0 + mu * mu);              // 3/(16pi)(1+mu^2)
    const float g = 0.76, gg = g * g;
    float phM = 0.0795775 * (1.0 - gg)
              / max(pow(1.0 + gg - 2.0 * g * mu, 1.5), 1e-3);   // HG, g=0.76

    vec3 single = kBetaR * phR * kRayGain * Tview * Tsun;
    vec3 mie    = vec3(kBetaM * phM * 0.25) * Tview * Tsun;
    // MULTIPLE SCATTERING. A single-scatter model has no blue anywhere at
    // sunset, because every blue photon the sun sends up is scattered away on
    // the way in. Real upper skies are blue purely from second- and
    // third-order scattering, so this is where the blue at altitude comes
    // from, ramped in with height rather than present at the horizon.
    // BUILD-P12: the blue at altitude was creeping down into the visible band
    // and greying the amber out.
    // BUILD-P15: the ramp is pushed to 0.78..1.06. sin(0.78) = 48 deg, so the
    // frame the camera actually sees (about 20 deg above the horizon) has
    // ZERO multiple-scattering blue in it — the sky stays orange right up to
    // the top edge of the image, and the zenith still goes blue if the user
    // ever looks straight up. The previous 0.55 start put 10% of the blue
    // floor into the top of the visible frame, which is exactly the yellow
    // cast the complaint is about.
    float multiK = 0.005 + 0.30 * smoothstep(0.78, 1.06, h);
    vec3 multi  = kBetaR * phR * 0.80 * Tview * multiK;
    return (single + mie + multi) * kSunI;
}

// Filmic shoulder + gamma, matching the object pass. Output is
// DISPLAY-REFERRED, because the object shader gamma-encodes too and the two
// passes have to agree or the corridor silhouettes against the wrong sky.
vec3 encodeSky(vec3 hdr) {
    // ACES (Narkowicz) — rolls the bright horizon and the sun aureole off
    // instead of clipping them into flat paper
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    vec3 x = clamp(hdr, 0.0, 8.0);
    x = clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    return pow(x, vec3(1.0 / 2.2));
}

void main() {
    vec3 dir = normalize(vDir - uEyePos);     // ray direction from the camera
    float h = dir.y;                          // -1 .. 1
    vec3 sun = normalize(uSunDir);
    float mu = dot(dir, sun);

    // ---- the atmosphere ----------------------------------------------------
    vec3 sky;
    if (h >= -0.02) {
        sky = atmosphere(vec3(dir.x, max(h, 0.0), dir.z), sun);
        // AIRLIGHT BAND. The single-scatter term has to fade OUT toward the
        // horizon (the view extinction kills it), but a real sunset horizon is
        // the BRIGHTEST part of the sky: that light has bounced so many times
        // it is effectively an isotropic glow, which no one-scatter model
        // contains. This band is that glow, warm and stronger toward the sun.
        // It is also the surface the corridor silhouettes against, so its
        // colour and level are the two numbers the whole scene reads from.
        float hz = exp(-max(h, 0.0) * 7.0);
        // BUILD-P15: hotter and LESS GREEN. The P13 airlight was (0.40, 0.14,
        // 0.036) — a ratio whose green channel is still 35% of the red, and
        // that green is what the eye reads as "yellow". Pulling green down to
        // 27% of red while lifting red 12% moves the horizon band from 35 deg
        // to 24 deg, i.e. from amber to a proper sunset orange. The band also
        // reaches a little further up the dome (falloff 7.0 -> 5.6) so the
        // orange does not stop abruptly a few degrees above the poles.
        sky += (vec3(0.450, 0.121, 0.031)
              + vec3(0.480, 0.147, 0.038) * pow(max(mu, 0.0), 3.0))
             * exp(-max(h, 0.0) * 5.6);
    } else {
        // below the horizon the dome is only ever seen past the edge of the
        // ground quad: dark warm ground haze, matched to the aerial
        // perspective the object pass mixes in.
        sky = atmosphere(vec3(dir.x, 0.015, dir.z), sun) * 0.42
            + vec3(0.030, 0.018, 0.010);
    }

    // ---- clouds ------------------------------------------------------------
    // Project the ray onto a virtual deck: p = dir.xz / dir.y gives the
    // perspective squash for free. The deck height is CLAMPED — dividing by a
    // near-zero dir.y is what smeared the build-P6 clouds into horizon
    // streaks — and the last few degrees are left to the haze, which is what
    // actually happens at dusk.
    float cover = 0.0;
    vec3 cloudCol = vec3(0.0);
    if (h > 0.030) {
        float hh = max(h, 0.075);
        vec2 cp = dir.xz / hh * 0.30;
        vec2 drift = vec2(uTime * 0.0060, -uTime * 0.0023);  // wind flows up-
                                                            // corridor, like
                                                            // the wires lean
        // BUILD-P21: THE DECK WAS ONE NOISE CELL BIG. `cp` is
        // |dir.xz| / dir.y * 0.30, so over the frame this camera actually sees
        // (52-degree vertical FOV, gazing 6.6 degrees up, i.e. -19 to +33
        // degrees of elevation) its magnitude runs from about 0.47 at the top
        // edge to 2.1 near the horizon. At cp*0.50 that is an input range of
        // 0.23 to 1.05 — the ENTIRE visible sky was sampling less than one
        // noise cell, which is why no cumulus appeared anywhere in the frame
        // and why the y40-y120 bands measured 59-67% of their pixels below a
        // local standard deviation of 0.006 (under 1.5/255). The clouds were
        // not too sparse; there were none. Scaling the deck up puts roughly
        // 2-3 cells across the frame, which is a cumulus field at this focal
        // length. The erosion layer moves with it so it still bites holes in
        // the edges rather than smearing them.
        float base = fbm3(cp * 1.75 + drift);
        // a finer deck at a different speed ERODES the base: cloud edges are
        // billowed by the shear layer above them, they do not fade evenly
        float det  = fbm2(cp * 6.50 - drift * 2.4 + 31.0);
        // density, then threshold: this is what gives a cloud a hard-ish sunlit
        // crown and a soft dissolving skirt instead of one smooth blob
        // BUILD-P21: erosion 0.22 -> 0.32. The deck's density field was too
        // UNIFORM, not too dense: value-noise fbm clusters tightly around 0.5,
        // so a smoothstep threshold applied to it clips almost nothing at all
        // and the sky comes back empty. Widening the high-frequency erosion
        // raises the variance of `dens` first, which is what puts broken cloud
        // on the deck — and because `det` is the finer field, extra erosion
        // eats HOLES IN THE EDGES, which is where the silhouette comes from.
        float dens = base - 0.32 * (det - 0.5);
        // BUILD-P10 TUNING: coverage is deliberately sparse. The first pass
        // thresholded at 0.505 and put a continuous sheet of cloud across the
        // whole top of the frame — a featureless pale band that read as fog,
        // and it is exactly what the old hand-drawn version of this sky did
        // NOT do: it had open orange sky right up to the frame edge.
        //
        // BUILD-P21: 0.620 -> 0.575, and it is worth being precise about why,
        // because "more cloud" was the previous fix and it was wrong. The
        // P10 complaint was not that there was too little cloud, it was that
        // the cloud had no EDGES — a continuous blanket has no silhouette, so
        // the eye reads it as fog however opaque it is. Measured on the t=12
        // frame, 34-67% of every band from y40 to y200 sat below a local
        // standard deviation of 0.006, i.e. under 1.5/255: half the frame was
        // a mathematically smooth gradient with nothing in it at all. Loosening
        // the threshold a little BUYS EDGES — more of the deck crosses into the
        // partial-coverage band (0.62..0.78) and gets a dissolving skirt —
        // rather than pushing more of it to a solid ceiling. The window is also
        // NARROWED at the low end (0.560..0.780), which sharpens the edge it
        // produces.
        cover = smoothstep(0.560, 0.780, dens) * 0.94;
        cover *= 0.80 + 0.20 * smoothstep(0.40, 0.66, det);
        // the haze eats the last few degrees above the horizon
        cover *= smoothstep(0.035, 0.26, h);
        cover *= 1.0 - smoothstep(0.62, 0.99, h);        // thinner at zenith

        // LIGHT the cloud instead of picking two colours: a sunlit crown, a
        // warm shadowed belly (light from BELOW is the ground, not the sky),
        // and a silver lining wherever the cloud is thin and the sun behind it.
        // The crown is PINK-gold, not white: at this sun elevation the tops are
        // lit by light that has already crossed the whole atmosphere, so a
        // neutral-white cumulus is the same mistake as a neutral-white sky.
        // BUILD-P15: warmer still (green 0.96 -> 0.84). A cloud is the largest
        // single area of non-sky in a dusk frame, so its colour sets the
        // frame's white balance as surely as the dome does; leaving the crowns
        // creamy put pale yellow blobs over an orange sky, which reads as haze
        // rather than as cloud.
        // BUILD-P21: green 0.845 -> 0.775, continuing the P15 direction. With
        // real cumulus now in the frame for the first time, the frame's mean
        // saturation fell from 0.479 to 0.372: the clouds are large, bright and
        // were the least saturated thing in a picture whose whole subject is an
        // amber sky. A cloud at this sun elevation has crossed the entire
        // atmosphere to reach the camera, so pulling its green down is the
        // physical answer, not just the flattering one.
        vec3 crown = vec3(1.42, 0.775, 0.400);
        vec3 belly = vec3(0.40, 0.106, 0.058);
        float lift = pow(clamp(cover, 0.0, 1.0), 0.55);
        cloudCol = mix(belly, crown, lift);
        float silver = pow(max(mu, 0.0), 14.0) * (1.0 - cover) * 1.35;
        cloudCol += vec3(1.00, 0.62, 0.30) * silver;
        // a touch of sky in the thin skirt so the cloud does not read as paint
        cloudCol += vec3(0.22, 0.105, 0.062) * (1.0 - lift) * max(mu, 0.0);
    }

    // ---- BUILD-P21: HIGH CIRRUS. The clear part of the sky was the other half
    // of the dead-band problem, and no amount of cumulus fixes it. A dusk sky
    // with open orange between the banks is NOT a gradient: at this sun
    // elevation there is nearly always a thin, fast, high veil streaking
    // across the clear air, and because it is ice aloft it is lit from BELOW
    // and BELHIND — which is why cirrus at sunset is the one cloud that goes
    // pink while everything under it goes grey. It is also almost transparent,
    // so it adds structure to the empty half of the frame without adding
    // coverage or hiding the amber.
    {
        // unrolled in the same deck space as the cumulus, at a much coarser
        // scale and a different drift speed: cirrus is ~8 km up against the
        // cumulus deck, so it moves slower across the sky AND shears the other
        // way, which is what makes the two layers read as separate.
        float ch = max(h, 0.055);
        vec2 vp = dir.xz / ch * 0.30;
        // Fibrous, so the noise is sampled about 4x finer in azimuth than in
        // elevation. The SCALES matter and they are easy to get backwards:
        // |dir.xz| / dir.y is cot(theta), which over the 10-30 degrees the
        // frame actually sees only spans about 1.2 units, so a "stretched"
        // factor of 0.11 collapsed the veil's entire vertical extent to a
        // tenth of one noise cell — it became a function of azimuth alone and
        // the measured dead bands did not move by a single count. Cirrus is
        // stretched along its length, not smeared into a constant.
        vec2 cir = vec2(vp.x * 9.0 - uTime * 0.010, vp.y * 4.5 + uTime * 0.004);
        float cn = fbm2(cir);
        float veil = smoothstep(0.48, 0.82, cn);
        veil *= smoothstep(0.035, 0.20, h) * (1.0 - smoothstep(0.55, 0.95, h));
        // lit from underneath and behind: brightest toward the sun, and it is
        // PINK, not white — it is the last light of the day on high ice
        float toward = pow(max(mu, 0.0), 2.2);
        // BUILD-P21: 0.52 -> 0.34 mix, and a warmer dark end. At full strength
        // the veil was measured to pull the frame's mean saturation from 0.479
        // to 0.427 — it was greying the amber it was supposed to sit in. Ice
        // cloud at this hour is barely opaque; what it contributes is
        // STRUCTURE, and structure survives at a third of the opacity.
        vec3 cirCol = mix(vec3(0.145, 0.070, 0.058), vec3(0.78, 0.32, 0.19),
                          0.24 + 0.76 * toward);
        sky = mix(sky, cirCol, clamp(veil * 0.26, 0.0, 0.26));
    }

    // ---- the sun: a TIGHT disc with limb darkening, plus the aureole the
    // atmosphere term already produced around it -----------------------------
    // angular distance without acos: 1 - cos(d) ~ d^2/2, so d = sqrt(2(1-mu))
    float d2 = 2.0 * (1.0 - mu);
    float R2 = 0.0026;                                    // ~0.66 deg radius
    float disc = smoothstep(R2 * 1.10, R2 * 0.82, d2);
    float limb = 0.55 + 0.45 * sqrt(clamp(1.0 - d2 / (R2 * 1.05), 0.0, 1.0));
    // the disc is seen THROUGH the atmosphere: it is dimmed and reddened by
    // exactly the same extinction the sky is, so it sets on the haze band
    // instead of punching a white hole in it
    vec3 sunTrans = exp(-kBetaR * (1.0 / (max(sun.y, 0.0) + 0.14)) * kSunPath
                        - vec3(kBetaM * 1.15));
    // BUILD-P15: the disc itself is orange, not warm white — it is the same
    // sunlight as the airlight band, just concentrated, so it has to agree
    // with it or the brightest point in the frame reads as a different light.
    sky += vec3(1.0, 0.72, 0.42) * sunTrans * disc * limb * 1.05;

    sky = mix(sky, cloudCol, clamp(cover, 0.0, 1.0));

    // subtle dither: kills gradient banding on smooth drivers
    float dith = vhash(dir.xy * 1913.7 + fract(uTime) * 17.0);
    sky += (dith - 0.5) * (1.5 / 255.0);

    fragColor = vec4(encodeSky(sky), 1.0);
}
