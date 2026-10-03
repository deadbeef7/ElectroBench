#version 330 core
// Crown splash mesh: a cylinder sheet around the impact point. The CPU
// uploads the crown's current radius/height/life; the GPU animates the
// jagged spikes (per-angle pseudo-noise, animated by uTime) so the crown
// tears into strands exactly like a real Worthington crown.
//
// Per-vertex layout (stride 9 floats):
//   [0]    angle (0..1 around the crown)
//   [1]    height parameter (0 = water line, 1 = rim)
//   [2..3] radius scale, height scale (unit mesh; uCrown scales it)

layout(location = 0) in vec2 aAngleH;   // angle, heightParam
layout(location = 1) in vec2 aScale;    // radiusScale (1), unused

uniform mat4 uViewProj;
uniform vec3 uCenter;      // impact point
uniform vec3 uCamPos;
uniform float uRadius;     // current crown radius
uniform float uHeight;     // current crown height
uniform float uTime;
uniform float uSpike;      // spike amplitude
uniform float uPhase;      // per-crown animation phase

out vec3 vWorld;
out vec3 vNormal;
out float vParam;
out float vAngle;

float hash(float n) {
    return fract(sin(n * 127.1) * 43758.5453);
}

// smooth pseudo-noise around the ring: 8 fixed spikes + 13 wobble spikes
// (+ a per-crown phase so a fleet of crowns never pulses in lockstep)
// FUTURE-BENCH CROWN GEOMETRY: real Worthington crowns are not rotationally
// symmetric — the sheet is thicker where the pot's own cavity walls were
// roughest, so the whole crown ring WOBBLES in radius (2nd-order azimuthal
// drift), not just in spike height. Two extra octaves + the radius wobble
// make every crown unique instead of a lathed 8-point star.
float spikeField(float a, float t) {
    // THIN-FINGER SPECTRUM: weight toward the higher octaves so the crown
    // tears into many thin sharp fingers (the reference look) instead of a
    // few fat lobes
    float v = 0.0;
    v += sin(a * 6.2831853 * 8.0 + t * 0.7) * 0.45;
    v += sin(a * 6.2831853 * 13.0 - t * 1.1) * 0.35;
    v += sin(a * 6.2831853 * 21.0 + t * 1.7) * 0.30;
    v += sin(a * 6.2831853 * 34.0 - t * 2.3) * 0.12;   // fine tearing octave
    v += sin(a * 6.2831853 * 5.0 + t * 0.35) * 0.18;   // broad lobe drift
    // CROWN-TOP SHARPENING: real finger tips TAPER to points while the sheet
    // between fingers sags rounded. A nonlinear boost on the positive lobes
    // rises the spikes into sharp crests without touching the troughs.
    v += 0.35 * max(v, 0.0) * max(v, 0.0);
    return v;
}

// second-order radius wobble: the crown is NOT a circle. Low-frequency
// azimuthal drift that multiplies the ring radius itself — the sheet bulges
// where it tears, exactly like the high-speed footage of real crowns.
float radiusWobble(float a, float t, float phase) {
    return 1.0 + 0.18 * sin(a * 6.2831853 * 2.0 + phase * 3.1 + t * 0.9)
              + 0.10 * sin(a * 6.2831853 * 3.0 - phase * 1.7 - t * 1.4);
}

void main() {
    float ang = aAngleH.x * 6.2831853;
    float hp = aAngleH.y; // 0 base .. 1 rim

    // BUILD-P15: THE CROWN IS A TRUMPET BEFORE IT IS A STAR.
    //
    // The rim of a Worthington crown is a smooth, near-circular collar of
    // water thrown clear of the impact. The tearing into fingers happens
    // AFTER, when the cavity beneath it pinches off and the sheet loses its
    // support — it does not spring into existence spikes-first.
    //
    // So the tearing amplitude is now weighted by height on the sheet, not
    // applied uniformly: the base of the collar (hp -> 0) is the last solid
    // part of the sheet and stays smooth, while the free lip (hp -> 1) tears
    // first and hardest. Multiplied by uSpike, which the CPU now ramps 0 ->
    // peak over the 210 ms after the rim rallies, the crown therefore reads
    // as a clean cylinder of water first and a crown of fingers second,
    // which is the order a real splash happens in.
    float tear = smoothstep(0.10, 0.92, hp);
    tear = tear * tear;                       // the base is emphatically smooth

    // spikes grow toward the rim; the ring wobbles as it expands. The last
    // term adds HIGH-frequency tearing that grows with the spike amplitude:
    // the sheet disintegrates into fingers rather than wobbling smoothly.
    float spikes = spikeField(aAngleH.x, uTime + uPhase);
    spikes += uSpike * 0.35 * sin(aAngleH.x * 6.2831853 * 26.0 +
                                  uTime * 2.3 + uPhase * 1.3);
    float hMul = 1.0 + uSpike * spikes * hp * tear;
    // radius wobble is part of the ring shape now (not just spike height):
    // the crown bulges asymmetrically as it tears, like real high-speed
    // footage — this is the "not CGI" geometry cue.
    // BUILD-P2 WORTHINGTON PROFILE: real crown walls NECK IN through the
    // mid-sheet (the cavity drags the film inward) and then the LIP FLARES
    // outward as it unfurls — a trumpet, not a cone. (1 - 0.30hp + 0.38hp²)
    // necks to ~0.94 at hp=0.4 and flares to ~1.08 at the rim.
    float rMul = (1.0 + uSpike * 0.30 * spikes * hp * tear)
               * (1.0 - uSpike * 0.18 * hp)
               * radiusWobble(aAngleH.x, uTime + uPhase, uPhase)
               * (1.0 - 0.30 * hp + 0.38 * hp * hp);   // necked sheet, flared lip

    vec3 pos = uCenter + vec3(cos(ang) * uRadius * rMul,
                              uHeight * hp * hMul,
                              sin(ang) * uRadius * rMul);

    // normal of the wobbly sheet: radial + tilt from the spike gradient
    vec3 radial = normalize(vec3(cos(ang), 0.0, sin(ang)));
    // approximate gradient numerically for a believable normal
    float e = 0.004;
    float s1 = spikeField(aAngleH.x - e, uTime + uPhase);
    float s2 = spikeField(aAngleH.x + e, uTime + uPhase);
    float grad = (s2 - s1) / (2.0 * e * 6.2831853);
    vec3 tang = normalize(vec3(-sin(ang), grad * uSpike * hp * tear, cos(ang)));
    vec3 n = normalize(cross(radial, tang));

    vWorld = pos;
    vNormal = n;
    vParam = hp;
    vAngle = aAngleH.x;
    gl_Position = uViewProj * vec4(pos, 1.0);
}
