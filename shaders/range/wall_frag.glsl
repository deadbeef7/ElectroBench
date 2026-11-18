#version 330 core

// Scene 1 -- the hangar wall: dark corrugated steel with vertical amber
// light slots washing the lower half, falling away into black at the top.
// It exists to give the guns and the slab a horizon and something to reflect.

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

out vec4 fragColor;

uniform vec3 uEye;
uniform vec3 uKeyDir;
uniform vec3 uKeyColor;
uniform vec3 uRimPos;
uniform vec3 uRimColor;
uniform vec3 uFillPos;
uniform vec3 uFillColor;
uniform vec3 uSkyAmb;
uniform vec3 uGlowAmb;
uniform float uTime;

float Hash(float n) { return fract(sin(n) * 43758.5453); }

void main() {
    float ang = atan(vWorldPos.z, vWorldPos.x);
    float y = vWorldPos.y;

    // ---- corrugation: deep ribs catching the strip lights ----------------
    float ribs = 0.5 + 0.5 * sin(ang * 140.0);
    float ribEdge = pow(ribs, 6.0);
    vec3 base = vec3(0.020, 0.021, 0.025) * (0.45 + 0.85 * ribs);

    // ---- vertical light slots, brightest near the floor -------------------
    float slotMask = step(0.86, fract(ang * 46.0));
    float lower = smoothstep(3.30, 0.10, y) * smoothstep(0.05, 0.55, y);
    vec3 col = base * (uSkyAmb * 0.55 + uGlowAmb * 0.25);
    col += uKeyColor * base * 0.10 * max(dot(normalize(vNormal), uKeyDir), 0.0);

    vec3 slot = vec3(1.0, 0.47, 0.17);
    col += slot * slotMask * lower * (0.55 + 0.45 * ribEdge) * 1.1;
    col += slot * ribEdge * lower * 0.10;

    // wash of the cool rim light climbing the wall
    float rimAng = atan(vWorldPos.z - uRimPos.z, vWorldPos.x - uRimPos.x);
    col += uRimColor * 0.0025 * (0.5 + 0.5 * cos(rimAng)) * exp(-y * 0.22);

    // grime streaks and a little vertical falloff
    float grain = Hash(floor(ang * 900.0) + floor(y * 40.0) * 17.0);
    col *= 0.85 + 0.30 * grain;
    col *= mix(1.0, 0.10, smoothstep(6.0, 17.0, y));

    // floor contact haze so the wall does not end in a hard line
    col += uGlowAmb * 0.20 * exp(-y * 1.6);

    fragColor = vec4(max(col, vec3(0.0)), 1.0);
}
