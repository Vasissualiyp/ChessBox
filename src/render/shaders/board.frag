// SPDX-License-Identifier: GPL-3.0-or-later
//
// Deliberately one material and no post-processing: M4's scope is "see and play any
// board the engine supports", not lighting. Flat shading with a single directional term
// keeps cell colours readable, which matters more here than realism - a player has to
// tell a highlighted destination from an ordinary cell at a glance.
#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 fragNormal;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec3 lightDir;
} push;

layout(location = 0) out vec4 outColor;

void main() {
    float lambert = max(dot(normalize(fragNormal), normalize(-push.lightDir)), 0.0);
    float shade = 0.55 + 0.45 * lambert;
    outColor = vec4(fragColor.rgb * shade, fragColor.a);
}
