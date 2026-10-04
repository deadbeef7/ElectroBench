#version 330 core
// Pool scene sky: an infinite checkerboard dome — the room itself. The checker
// tiles follow the dome's direction (angular projection, the classic pool-room
// trick) so the pattern converges nicely at the horizon and the whole dome glows
// softly.
//
// BUILD-P10 — two changes that move this from "graphic" to "photographic":
//
// 1. THE LIGHT NOW EXISTS. The room was lit by a HIDDEN source: a gradient that
//    happened to be brighter in one direction, with no lamp anywhere to
//    justify it. Real rooms show their light. A big soft luminous panel now
//    sits in the light's own direction with a dark mullion cross and a bloom
//    skirt, and everything else in the scene — the water's specular streak, the
//    teapot's highlight, the room's own gradient — is suddenly the CONSEQUENCE
//    of something visible rather than an unexplained asymmetry.
//
// 2. CAUSTICS. Refracted light through moving water is the defining optical
//    event of a pool room and it was completely absent. The projection here is
//    the real one: the light ray that lands on a surface point has last touched
//    the water plane upstream, so the surface's coordinate is unrolled into the
//    water plane and the same two crossing webs the water and the teapots use
//    are evaluated there. Two details make it read as light and not as a stain:
//    the filaments are raised to a high power (caustics are FOLDS in the water
//    surface, so they are mostly dark with thin bright lines, not a soft
//    brightening), and they fall off with height, because the floor is close to
//    the water and the upper wall and ceiling are not.

in vec3 vWorld;
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uEyePos;
uniform vec3 uCenter;      // dome centre (camera position)
uniform float uRadius;     // dome radius (drawn huge, camera inside)
uniform vec3 uLightDir;    // direction TOWARD the hidden light
uniform vec3 uLightTint;   // cool pool-room glow
uniform float uTime;

out vec4 fragColor;

const float PI = 3.14159265358979;

float checker(vec2 p) {
    // TRUE alternating checkerboard (analytically box-filtered, iq-style):
    // EQUAL red and white squares — the old test (max(w.x,w.y) <= 0.25) only
    // lit the CENTRE of each cell, which painted the whole sky red with a
    // small white square floating in every tile. fwidth spikes at grazing
    // angles are handled correctly: tiles converge to the 0.5 mean where the
    // pixel footprint spans multiple cells, instead of aliasing to noise.
    vec2 w = fwidth(p) + 1e-4;
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5)
                  - abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}

void main() {
    // the dome mesh is drawn around the camera; its unit vertex IS the view
    // direction (position = aPos * uRadius + uCenter, so (vWorld-uCenter) is
    // just a * uRadius — normalise direction from the interpolated vertex
    // directly to avoid fp16-ish precision loss at kDomeRadius scale)
    vec3 dir = normalize(vNormal);
    float up = clamp(dir.y, -1.0, 1.0);

    // ANGULAR CHECKER GRID (final look): tiles at constant angular size —
    // 0.25 rad (14 degrees) squares in azimuth x elevation. The old gnomonic
    // unroll compressed cells into sub-pixel slivers near the horizon, which
    // (once properly antialiased) melted the whole sky into the red/white
    // MEAN — a pale pink wash. An angular grid keeps BOLD square red then
    // square white tiles at every elevation, converging only at the zenith.
    // The water mirror maps its rays through the SAME grid, so the tile
    // columns line up across the horizon like a real room.
    vec2 plane = vec2(atan(dir.x, dir.z), asin(clamp(up, -1.0, 1.0))) * 4.0;
    float c = checker(plane + vec2(uTime * 0.006, 0.0));

    // two tones — the WHITE & RED pool-room checker. These are deliberately
    // hot linear values: the tonemap knee + gamma at the end wash colours
    // toward pastel, so this overshoot keeps the tiles reading as blazing
    // white / deep pure red on screen. Must match kTileA/kTileB in
    // src/pool.cxx (the water uniforms).
    vec3 tileA = vec3(2.30, 2.30, 2.26);   // hot white
    vec3 tileB = vec3(1.50, 0.008, 0.010); // deep pure red
    vec3 albedo = mix(tileB, tileA, c);

    // GROUT. A checkerboard of pure colour with a razor edge is a graphic, not
    // a tiled room: real ceramic has a recessed joint between every tile that
    // reads as a dark line and, at a grazing angle, as a continuous grey seam.
    // Only drawn where the pixel footprint is small enough to resolve a tile —
    // otherwise the antialiased blend toward 0.5 at the horizon would smear the
    // grout over the entire far field.
    float foot = max(fwidth(plane.x), fwidth(plane.y));
    float grout = (1.0 - smoothstep(0.06, 0.34, foot))
                * smoothstep(0.10, 0.0, abs(c - 0.5));
    albedo *= mix(1.0, 0.62, grout);

    // HIDDEN light: a broad directional wash, brighter toward the light.
    // No disc, no lamp model — the sky simply gets brighter that way.
    vec3 Ld = normalize(uLightDir);
    float toLight = clamp(dot(dir, Ld), 0.0, 1.0);
    float wash = pow(toLight, 1.6);

    // horizon glaze keeps the dome melting into the water plane
    float horiz = 1.0 - smoothstep(0.0, 0.42, abs(up));

    vec3 col = albedo * uLightTint * (0.85 + 0.55 * wash);
    col += uLightTint * 0.05 * horiz;

    // ---- CAUSTICS ON THE ROOM ---------------------------------------------
    // Unroll the surface into the water plane: the caustic that lands here is
    // the one refracted through the water UPSTREAM of this point, which is a
    // fixed offset along the light direction, so a per-fragment unroll is the
    // exact projection rather than an approximation of it.
    // FREQUENCY NOTE: the first attempt unrolled at 9.0, which gives cells 40
    // degrees across — nine cells over the whole dome, so the "caustic" was a
    // smooth gradient with no filaments in it. Caustic cells are decimetres on
    // a floor, and the floor here is metres away, so the frequency has to be
    // an order of magnitude higher. Likewise pow(q1*q2, 6) is nearly zero
    // everywhere (the mean of two half-sine waves is 0.25, and 0.25^6 is
    // 2e-4): the web has to be thresholded, not raised to a power, or only the
    // exact peaks survive and the room gets no light at all.
    // The unroll basis is the room's own ANGULAR grid (`plane`), not the world
    // xz: on a dome the wall at grazing incidence compresses any world-space
    // projection into sub-pixel slivers, so a world-space unroll produced no
    // filaments at all. Angles are uniform across the room, so a constant
    // angular cell size is both stable and honest here.
    vec2 wuv = plane * 20.0;
    float q1 = 0.5 + 0.5 * sin(wuv.x + sin(wuv.y * 0.62 + uTime * 1.15) * 1.9);
    float q2 = 0.5 + 0.5 * sin(wuv.y * 0.74 - uTime * 0.95
                              + sin(wuv.x * 0.68 - uTime * 0.60) * 1.7);
    float caus = pow(smoothstep(0.28, 0.90, q1 * q2), 1.4);
    // vertical falloff: the floor sits at the waterline, the upper wall and the
    // ceiling are metres away from it and get a fraction of the light
    float cfall = exp(-max(up, 0.0) * 4.2) * horiz;
    // light has to actually arrive here
    cfall *= 0.35 + 0.65 * wash;
    col += uLightTint * albedo * caus * cfall * 0.90;

    // ---- THE PANEL: the light source itself --------------------------------
    // A soft rectangular luminous panel in the light's direction, with a dark
    // mullion cross and a bloom skirt that carries onto the surrounding tiles.
    // Built in a tangent frame around Ld so it stays a flat rectangle whatever
    // the light direction is.
    vec3 tUp = abs(Ld.y) > 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    vec3 tx = normalize(cross(tUp, Ld));
    vec3 ty = cross(Ld, tx);
    float cosA = max(dot(dir, Ld), 0.08);
    vec2 q = vec2(dot(dir, tx), dot(dir, ty)) / cosA;
    float half_ = 0.30;
    float box = max(abs(q.x), abs(q.y));
    // soft-edged diffuser: the panel is a diffusing sheet, so its border is a
    // ramp over several degrees, not a step
    float edgeT = smoothstep(half_, half_ * 0.55, box);
    // BUILD-P19: A FITTING IS NOT A SOLID WHITE RECTANGLE. Measured on the
    // 1280x720 t=7 baseline, this panel was ONE 255x89 px mass at 0.97 fill
    // — 2.40% of the frame with a hard edge and nothing inside it. A flat
    // plateau with a hard edge is the loudest "this is a rectangle of maths"
    // signal available, and it is simply wrong: a real troffer has a bezel
    // the diffuser is clipped into, a prismatic louvre across its face, and
    // a diffuser that dims toward the frame because the acrylic is deeper
    // there. Three cheap terms, all of them real, and together they turn a
    // slab of clipped white into a fitting.
    //   diffuser face, dimming toward the frame
    float panel = edgeT * (0.62 + 0.38 * smoothstep(half_ * 0.05, half_ * 0.80, edgeT));
    // the prismatic louvre every tiled ceiling fitting has: ~20 ribs across
    // the face, which is ~12 px per rib on screen, so it survives the
    // resolution without shimmering. It is also what stops the panel being
    // featureless in the range the tonemap has already rolled onto the
    // shoulder — the one part of it that is NOT clipped.
    float louvre = 0.5 + 0.5 * cos(q.x * 210.0);
    panel *= 0.86 + 0.14 * louvre;
    // the bezel: the frame the diffuser is clipped into, sitting OUTSIDE the
    // diffuser edge. It occludes rather than adds, so behind it you see the
    // ceiling tile, which is what a white plastic frame looks like anyway.
    panel *= smoothstep(half_ * 1.00, half_ * 0.86, box);
    // mullion cross — the giveaway that this is a light fitting and not a blob
    float mull = smoothstep(0.030, 0.012, abs(q.x))
               + smoothstep(0.030, 0.012, abs(q.y));
    panel *= clamp(1.0 - mull * 0.85, 0.0, 1.0);
    // BUILD-P19: 5.6 -> 4.2, on top of the ~0.81 mean of the new face term
    // and the ~0.93 of the bezel. Peak radiance on the panel falls ~43%.
    // 13.94% of the baseline frame sat above 0.93 luma, almost all of it
    // here and on the adjacent white tiles; the filmic shoulder can only
    // compress a highlight it is actually given, so the honest fix is to
    // stop handing it a plateau.
    col += uLightTint * panel * 4.2;
    // bloom skirt: the panel is bright enough to light the tiles right around it
    col += uLightTint * smoothstep(half_ * 2.6, half_, box) * 0.30;

    // FILMIC ACES (Narkowicz) tonemap — the same grading the teapots wear.
    // The old x/(x+0.35) knee let the hot-white tiles plateau at clip level
    // (27% of the sky band over 0.97 luma, fused into flat sheets). ACES'
    // shoulder compresses every overshoot into a graded roll-off instead of
    // a clip wall, so tiles stay BLAZING but never flatten — and the deep-red
    // tiles stop washing out to salmon (ACES keeps their hue while crushing
    // the near-zero G/B, so red finally reads RED).
    col *= 0.85;                                  // exposure under the shoulder
    col = clamp((col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14), 0.0, 1.0);
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
