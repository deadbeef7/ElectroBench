#version 330 core

// Scene 1 (ELECTRO RANGE) -- one vertex stage for every non-gun surface:
// the carousel slab, the hangar wall, the light bars and the volume cones.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    // Every instance transform is a rigid move plus a uniform scale, so the
    // upper 3x3 is a valid normal matrix.
    vNormal = normalize(mat3(uModel) * aNormal);
    vUV = aUV;
    gl_Position = uViewProj * wp;
}
