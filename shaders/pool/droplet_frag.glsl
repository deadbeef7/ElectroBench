#version 330 core

// Spray droplet (scene 3).
//
// Each sprite is shaded as a tiny sphere of water: a dark refracted core, a
// bright Fresnel limb, one hard specular from the ceiling panel (smeared along
// the motion streak) and a small secondary glint. The old version used a soft
// radial blob with a painted highlight, which is what read as "foam puffs".

in vec2 vUV;
in float vBright;
in float vStretch;

uniform vec3 uLightTint;
uniform vec3 uWaterA;
uniform vec3 uWaterB;

out vec4 fragColor;

void main() {
    float r = length(vUV);
    if (r > 1.0) discard;

    vec3 N = vec3(vUV, sqrt(max(1.0 - r * r, 0.0)));
    float NoV = clamp(N.z, 0.0, 1.0);
    float fres = clamp(0.02 + 0.98 * pow(1.0 - NoV, 5.0), 0.02, 0.95);

    // Light passing through the drop: mostly the bright room behind it, tinted
    // by the short water path. A dark drop against a white tile is the one
    // thing that makes spray read as dirt.
    vec3 refr = mix(uWaterA * 0.85, vec3(0.88, 0.93, 0.96), 0.55 + 0.30 * NoV);
    refr = mix(refr, uWaterB, 0.06 * (1.0 - NoV));

    float smear = clamp(1.0 / vStretch, 0.25, 1.0);
    vec2 hp = vUV - vec2(-0.34, 0.36);
    hp.y *= mix(1.0, 1.0 / smear, 0.75);
    float spec = exp(-dot(hp, hp) * 42.0);

    vec2 gp = vUV - vec2(0.30, -0.24);
    float glint2 = exp(-dot(gp, gp) * 90.0);

    vec3 col = mix(refr, vec3(0.90, 0.95, 0.99), fres * 0.80);
    col += uLightTint * spec * 1.5;
    col += vec3(0.92, 0.96, 1.0) * glint2 * 0.35;
    col = min(col, vec3(0.97, 0.96, 0.95));

    float alpha = clamp((0.46 + 0.45 * fres) * vBright, 0.0, 0.95);
    alpha *= smoothstep(1.0, 0.82, r);
    fragColor = vec4(col, alpha);
}
