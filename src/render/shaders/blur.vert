// SPDX-License-Identifier: GPL-3.0-or-later
//
// A full-screen triangle, generated from the vertex index alone: no vertex buffer, no
// binding, nothing to keep in sync. Three vertices cover the frame with no seam down
// the diagonal that two triangles would have.
#version 450

layout(location = 0) out vec2 fragUv;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    fragUv = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
