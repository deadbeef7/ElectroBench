#version 330 core
// Splash droplets: billboarded camera-facing quads, one per droplet.
// Per-vertex layout (stride 10 floats):
//   [0..2] droplet world position (pre-built per frame, streamed)
//   [3..4] uv corner of the billboard (-1..1)
//   [5]    droplet radius (world units)
//   [6]    brightness 0..1 (fade with life)

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec2 aSize; // x radius, y brightness

uniform mat4 uViewProj;
uniform vec3 uCamRight;
uniform vec3 uCamUp;

out vec2 vUV;
out float vBright;

void main() {
    vec3 world = aPos
               + uCamRight * (aUV.x * aSize.x)
               + uCamUp * (aUV.y * aSize.x);
    gl_Position = uViewProj * vec4(world, 1.0);
    vUV = aUV;
    vBright = aSize.y;
}
