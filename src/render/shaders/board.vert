// SPDX-License-Identifier: GPL-3.0-or-later
//
// One instanced draw per shape covers the whole board: every cell is an instance of the
// slab mesh, and every piece an instance of its archetype. The draw-call count is the
// number of shapes in play - seven or eight - and is independent of board size, so a
// million-cell lattice is a buffer upload rather than a scene graph.
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in float inHeight;   // 0 at the piece's foot, 1 at its crown

layout(location = 3) in vec3 instCenter;
layout(location = 4) in vec3 instScale;
layout(location = 5) in vec4 instColor;
layout(location = 6) in vec4 instEdge;    // seam colour; alpha 0 means no seam here
layout(location = 7) in float instEdgeMask;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 lightDir;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragLocal;
layout(location = 3) out vec4 fragEdge;
layout(location = 4) out float fragEdgeMask;
layout(location = 5) out float fragHeight;

void main() {
    vec3 world = instCenter + inPosition * instScale;
    gl_Position = push.viewProj * vec4(world, 1.0);
    fragColor = instColor;
    fragNormal = normalize(inNormal / max(abs(instScale), vec3(1e-6)));
    fragLocal = inPosition.xy;
    fragEdge = instEdge;
    fragEdgeMask = instEdgeMask;
    fragHeight = inHeight;
}
