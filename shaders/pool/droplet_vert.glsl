#version 330 core
// Splash droplets: elongated along their velocity — water in motion, not
// soap bubbles. The quad is rotated in screen space to line up with the
// droplet's projected velocity and stretched along it (faster = longer).
//
// Per-vertex layout (stride 10 floats):
//   [0..2]  world position
//   [3..5]  velocity (world units/s)
//   [6..7]  billboard corner (-1..1)
//   [8]     radius (world units)
//   [9]     brightness 0..1 (fade with life)

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aVel;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec2 aSize; // x radius, y brightness

uniform mat4 uViewProj;

out vec2 vUV;
out float vBright;

void main() {
    float speed = length(aVel);

    vec4 c0 = uViewProj * vec4(aPos, 1.0);
    // project a point slightly ahead along the velocity to get the on-screen
    // motion direction, then build the quad in that rotated frame
    vec4 c1 = uViewProj * vec4(aPos + aVel * 0.05, 1.0);
    vec2 s0 = c0.xy / max(c0.w, 1e-4);
    vec2 s1 = c1.xy / max(c1.w, 1e-4);
    vec2 dir = s1 - s0;
    float dl = length(dir);
    dir = dl > 1e-5 ? dir / dl : vec2(0.0, 1.0);
    vec2 ortho = vec2(-dir.y, dir.x);

    // long axis along the motion: ~2.5x at drop speed, round when slow
    float stretch = 1.0 + min(speed * 0.35, 1.6);
    vec2 offset = dir * (aUV.y * aSize.x * stretch)
                + ortho * (aUV.x * aSize.x);
    gl_Position = vec4((s0 + offset) * c0.w, c0.z, c0.w);
    vUV = aUV;
    vBright = aSize.y;
}
