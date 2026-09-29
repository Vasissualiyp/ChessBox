// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

layout(location = 0) in vec2 uv;

layout(push_constant) uniform Push {
    vec4 inner;      // colour at the light's centre
    vec4 outer;      // colour at the far corners
    vec4 params;     // x,y = centre in uv, z = falloff, w = vignette strength
} push;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 d = uv - push.params.xy;
    // Squashed vertically: the light reads as coming from above and behind the board
    // rather than from a lamp bolted to the middle of the screen.
    d.y *= 1.35;
    float r = length(d) * push.params.z;

    float glow = exp(-r * r * 2.2);
    vec3 rgb = mix(push.outer.rgb, push.inner.rgb, glow);

    // Corner falloff, so the panels at the edges sit on the darkest part of the frame.
    vec2 c = abs(uv - 0.5) * 2.0;
    float vignette = 1.0 - push.params.w * pow(max(c.x, c.y), 2.6);
    rgb *= clamp(vignette, 0.0, 1.0);

    outColor = vec4(rgb, 1.0);
}
