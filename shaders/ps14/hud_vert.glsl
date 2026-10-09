#version 330 core


layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform vec2 uResolution;

out vec2 vUV;

void main() {
    vec2 clip = vec2(aPos.x / uResolution.x * 2.0 - 1.0,
                     1.0 - aPos.y / uResolution.y * 2.0);
    gl_Position = vec4(clip, 0.0, 1.0);
    vUV = aUV;
}
