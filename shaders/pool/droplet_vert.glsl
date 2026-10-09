#version 330 core


layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aVel;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec2 aSize;

uniform mat4 uViewProj;

out vec2 vUV;
out float vBright;


out float vStretch;

void main() {
    float speed = length(aVel);

    vec4 c0 = uViewProj * vec4(aPos, 1.0);


    vec4 c1 = uViewProj * vec4(aPos + aVel * 0.05, 1.0);
    vec2 s0 = c0.xy / max(c0.w, 1e-4);
    vec2 s1 = c1.xy / max(c1.w, 1e-4);
    vec2 dir = s1 - s0;
    float dl = length(dir);
    dir = dl > 1e-5 ? dir / dl : vec2(0.0, 1.0);
    vec2 ortho = vec2(-dir.y, dir.x);


    float stretch = 1.0 + min(speed * 0.85, 4.0);
    vec2 offset = dir * (aUV.y * aSize.x * stretch)
                + ortho * (aUV.x * aSize.x);
    gl_Position = vec4((s0 + offset) * c0.w, c0.z, c0.w);
    vUV = aUV;
    vBright = aSize.y;
    vStretch = stretch;
}
