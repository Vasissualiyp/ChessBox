// SPDX-License-Identifier: GPL-3.0-or-later
//
// One half of a separable Gaussian. Run twice - once across, once down - it costs
// eighteen taps instead of the eighty-one a square kernel of the same width would.
//
// It exists for one moment in the game: pausing steps the camera back off the board and
// throws it out of focus, so that the menu in front of it is what the eye lands on. The
// board underneath stays *live* rather than being a frozen screenshot, because a player
// who opens Settings from the pause menu and changes how the board looks must see the
// change, not a stale picture of the frame they paused on.
#version 450

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Push {
    vec2 direction;   // one texel across, on the axis being blurred, times the radius
    float strength;   // 0 = untouched, 1 = the full blur
    float dim;        // how far to pull the result towards the ground colour
    vec4 ground;
} push;

void main() {
    vec4 centre = texture(source, fragUv);
    if (push.strength <= 0.0) {
        outColor = centre;
        return;
    }

    // Nine taps, binomial weights. Wide enough to read as defocus rather than as a
    // smudge, cheap enough to run every frame while a menu is open.
    const float w[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);
    vec4 sum = centre * w[0];
    for (int i = 1; i < 5; ++i) {
        vec2 offset = push.direction * float(i) * push.strength;
        sum += texture(source, fragUv + offset) * w[i];
        sum += texture(source, fragUv - offset) * w[i];
    }

    // Desaturating and darkening with the blur is what makes it read as "not the thing
    // you are looking at" rather than as a rendering fault.
    vec3 color = mix(centre.rgb, sum.rgb, push.strength);
    color = mix(color, push.ground.rgb, push.dim);
    outColor = vec4(color, 1.0);
}
