#version 330 core
// Fullscreen triangle; no vertex buffer needed (uses gl_VertexID).

out vec2 vNDC;

const vec2 verts[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));

void main() {
    vNDC = verts[gl_VertexID];
    gl_Position = vec4(verts[gl_VertexID], 0.999, 1.0);
}
