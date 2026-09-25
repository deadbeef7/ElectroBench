#version 330 core
// Pool scene sky: an infinite checkerboard dome, lit by a HIDDEN light
// source. The checker tiles follow the dome's direction (planar projection
// from above, the classic pool-room trick) so the pattern converges nicely
// at the horizon and the whole dome glows softly without any visible sun,
// lamp or fixture anywhere in the scene.
//
// The light is only ever communicated through shading: a soft directional
// gradient that is brighter toward the light azimuth, and the matching
// specular sheen the water and teapot pick up. Nothing renders the light.

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
    // Hard-edged checker, no fwidth: the dome's coarse mesh makes screen-space
    // derivatives of the gnomonic projection enormous, which washed every
    // tile to mid-grey. Aliasing at extreme distance is hidden by the
    // horizon glaze instead.
    vec2 w = abs(fract(p) - 0.5);
    return step(max(w.x, w.y), 0.25);
}

void main() {
    // the dome mesh is drawn around the camera; its unit vertex IS the view
    // direction (position = aPos * uRadius + uCenter, so (vWorld-uCenter) is
    // just a * uRadius — normalise direction from the interpolated vertex
    // directly to avoid fp16-ish precision loss at kDomeRadius scale)
    vec3 dir = normalize(vNormal);
    float up = clamp(dir.y, -1.0, 1.0);

    // planar projection: unroll the dome direction onto a flat grid above
    vec2 plane;
    float blend = smoothstep(0.06, 0.35, up);
    if (blend > 0.001) {
        plane = dir.xz / max(up, 0.06) * 1.05;   // gnomonic from above
    } else {
        // below-horizon fallback keeps the seam invisible at grazing angles
        plane = dir.xz * (6.2831853 / max(0.06 - up, 0.06));
    }
    float cell = 0.16;                            // checker scale: 6+ tiles
                                                  // across the visible sky
    float c = checker(plane / cell + vec2(uTime * 0.006, 0.0));

    // two tones — the WHITE & RED pool-room checker. These are deliberately
    // hot linear values: the tonemap knee + gamma at the end wash colours
    // toward pastel, so this overshoot keeps the tiles reading as blazing
    // white / deep pure red on screen. Must match kTileA/kTileB in
    // src/pool.cxx (the water uniforms).
    vec3 tileA = vec3(2.30, 2.30, 2.26);   // hot white
    vec3 tileB = vec3(1.50, 0.008, 0.010); // deep pure red
    vec3 albedo = mix(tileB, tileA, c);

    // HIDDEN light: a broad directional wash, brighter toward the light.
    // No disc, no lamp model — the sky simply gets brighter that way.
    float toLight = clamp(dot(dir, normalize(uLightDir)), 0.0, 1.0);
    float wash = pow(toLight, 1.6);

    // horizon glaze keeps the dome melting into the water plane
    float horiz = 1.0 - smoothstep(0.0, 0.42, abs(up));

    vec3 col = albedo * uLightTint * (0.85 + 0.55 * wash);
    col += uLightTint * 0.05 * horiz;

    // tonemap + gamma, same pipeline as the other scenes
    col = col / (col + vec3(0.35));               // gentle filmic knee
    col = pow(col, vec3(1.0 / 2.2));
    fragColor = vec4(col, 1.0);
}
