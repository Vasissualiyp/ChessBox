// SPDX-License-Identifier: GPL-3.0-or-later
//
// A full-screen triangle with no vertex buffer at all: three positions derived from the
// vertex index. Drawn before the board to give the scene a ground that is lit rather
// than a flat fill - the difference between a board floating in a void and a board on a
// table with a candle over it.
#version 450

layout(location = 0) out vec2 uv;

void main() {
    uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
