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

layout(location = 8) in float instRoll;     // rotation about the mesh's own Z, radians
layout(location = 9) in float instMetal;    // 1 for a mirror face, shaded as metal

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
layout(location = 6) out vec2 fragEdgeInset;
layout(location = 7) out float fragMetal;

void main() {
    // Spin the mesh in its own plane before scaling, so one quarter-round corner can be
    // turned into any of the four quadrants it is needed in.
    const float cr = cos(instRoll);
    const float sr = sin(instRoll);
    const vec2 spun = vec2(inPosition.x * cr - inPosition.y * sr,
                           inPosition.x * sr + inPosition.y * cr);
    const vec3 local = vec3(spun, inPosition.z);
    vec3 world = instCenter + local * instScale;
    gl_Position = push.viewProj * vec4(world, 1.0);
    fragColor = instColor;
    const vec2 nspun = vec2(inNormal.x * cr - inNormal.y * sr,
                            inNormal.x * sr + inNormal.y * cr);
    fragNormal = normalize(vec3(nspun, inNormal.z) / max(abs(instScale), vec3(1e-6)));
    fragLocal = inPosition.xy;
    fragEdge = instEdge;
    fragEdgeMask = instEdgeMask;
    fragHeight = inHeight;
    fragMetal = instMetal;

    // Where the seam band begins, in the box's own coordinates. Derived from the
    // instance's scale so the band is a constant *world* width: without this a large
    // instance - the plinth under the board - gets a border a third of its width, which
    // stops reading as a rim and starts reading as a paint job.
    const float kBandWorld = 0.085;
    fragEdgeInset = vec2(0.5) - kBandWorld / max(abs(instScale.xy), vec2(0.001));
}
