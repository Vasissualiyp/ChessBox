// SPDX-License-Identifier: GPL-3.0-or-later
//
// One instanced draw per frame covers the whole board: every cell and every piece is
// an instance of the same unit cube, scaled and coloured by per-instance data. The
// draw-call count is therefore independent of board size, which is what lets a
// million-cell lattice be a buffer upload rather than a scene graph.
#version 450

layout(location = 0) in vec3 inPosition;   // unit cube, -0.5 .. 0.5
layout(location = 1) in vec3 inNormal;

layout(location = 2) in vec3 instCenter;
layout(location = 3) in vec3 instScale;
layout(location = 4) in vec4 instColor;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec3 lightDir;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec3 fragNormal;

void main() {
    vec3 world = instCenter + inPosition * instScale;
    gl_Position = push.viewProj * vec4(world, 1.0);
    fragColor = instColor;
    // Non-uniform scale would skew a normal, but every instance here is an
    // axis-aligned box, so the sign-preserving component-wise inverse is exact.
    fragNormal = normalize(inNormal / max(instScale, vec3(1e-6)));
}
