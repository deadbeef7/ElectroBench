#version 330 core
// Crown splash: a thin sheet of water erupting around the impact point.
// Physically it is a curved film of water: it both REFLECTS and TRANSMITS
// the environment, so its colour is a Fresnel-weighted blend of the two
// checker tiles sweeping around the ring — it never reads grey. As the sheet
// climbs it tears into FINGERS (alpha stripes around the ring), and the rim
// catches the hidden light in sharp glints.

in vec3 vWorld;
in vec3 vNormal;
in float vParam; // height parameter 0 (water line) .. 1 (rim)
in float vAngle; // 0..1 around the crown

uniform vec3 uEyePos;
uniform vec3 uLightDir;
uniform vec3 uLightTint;
uniform vec3 uSkyA;   // white checker tile (matches the sky dome)
uniform vec3 uSkyB;   // deep red checker tile

out vec4 fragColor;

void main() {
    vec3 V = normalize(uEyePos - vWorld);
    vec3 N = normalize(vNormal);

    // transmitted environment: the checker tiles behind the sheet, sweeping
    // around the crown so neighbouring fingers pick up different tiles
    float sweep = 0.5 + 0.5 * sin(vAngle * 6.2831853 * 3.0 +
                                  vWorld.x * 0.7 + vWorld.z * 0.9);
    vec3 env = mix(uSkyB, uSkyA, sweep);

    float edge = 1.0 - abs(dot(N, V));         // grazing = mirror-like
    float diff = max(dot(N, normalize(uLightDir)), 0.0);
    vec3 H = normalize(V + normalize(uLightDir));
    float spec = pow(max(dot(N, H), 0.0), 64.0);

    // real crowns tear into FINGERS — alpha stripes around the ring that
    // deepen toward the rim as the sheet disintegrates
    float fingers = 0.68 + 0.32 * sin(vAngle * 6.2831853 * 22.0 + vParam * 2.6);

    // the sheet is dense at the base and breaks apart toward the rim
    float sheet = 1.0 - 0.55 * vParam;

    // transmitted env + reflection sheen; the rim catches the light hard
    vec3 col = env * (0.42 + 0.38 * (1.0 - edge))
             + uLightTint * (0.08 + diff * 0.28 + spec * 1.7 + edge * 0.45);

    float alpha = (0.30 + edge * 0.55) * sheet * fingers;
    fragColor = vec4(col, alpha);
}
