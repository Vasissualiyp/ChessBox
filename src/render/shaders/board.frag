// SPDX-License-Identifier: GPL-3.0-or-later
//
// One material, one light. The scene reads as candlelight on worn wood, so the shading
// is warm-biased and the ambient term is generous - a board has to stay legible, and a
// player should never lose a piece to a shadow.
#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragLocal;
layout(location = 3) in vec4 fragEdge;
layout(location = 4) in float fragEdgeMask;
layout(location = 5) in float fragHeight;
layout(location = 6) in vec2 fragEdgeInset;
layout(location = 7) in float fragMetal;
layout(location = 8) in vec3 fragWorld;

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 lightDir;
    vec4 eyePos;
} push;

layout(location = 0) out vec4 outColor;

bool maskHas(float mask, int bit) {
    return mod(floor(mask / pow(2.0, float(bit))), 2.0) >= 0.5;
}

void main() {
    // Two-sided: turn the normal toward the viewer before lighting, so a fragment on
    // either side of the surface is shaded the same.
    vec3 n = normalize(fragNormal);
    if (dot(n, push.eyePos.xyz - fragWorld) < 0.0) n = -n;
    float lambert = max(dot(n, normalize(-push.lightDir.xyz)), 0.0);

    // A warm key and a cool fill, so vertical faces stay distinct from horizontal ones
    // without going black.
    vec3 warm = vec3(1.06, 0.96, 0.84);
    vec3 cool = vec3(0.80, 0.85, 1.00);
    vec3 shade = mix(cool * 0.52, warm, lambert);

    // Pieces darken slightly toward the foot: a tall piece should read as standing on
    // the board rather than floating above a flat blob.
    shade *= mix(0.80, 1.06, clamp(fragHeight, 0.0, 1.0));

    vec3 rgb = fragColor.rgb * shade;

    // A mirror face is polished metal rather than a painted band: a tight specular
    // highlight and a faint rim, so bright white reads as silvered glass. The view
    // direction is fixed because the board is looked at from above the frame; it is
    // enough to catch the light as the face turns.
    if (fragMetal > 0.5) {
        vec3 toLight = normalize(-push.lightDir.xyz);
        vec3 toView = normalize(vec3(0.0, 0.42, 1.0));
        vec3 halfV = normalize(toLight + toView);
        float spec = pow(max(dot(n, halfV), 0.0), 64.0);
        float rim = pow(1.0 - max(dot(n, toView), 0.0), 3.0);
        rgb = rgb * 0.92 + vec3(0.9) * spec + vec3(0.35) * rim;
    }

    // A seam edge: the cell's side is glued to somewhere else on the board. Drawn only
    // on the sides the geometry actually identified, so a player can pair the two edges
    // by eye rather than being told the whole cell is special.
    if (fragEdge.a > 0.0 && n.z > 0.35) {
        vec2 inset = clamp(fragEdgeInset, vec2(0.0), vec2(0.49));
        bool onEdge =
            (maskHas(fragEdgeMask, 0) && fragLocal.x < -inset.x) ||
            (maskHas(fragEdgeMask, 1) && fragLocal.x >  inset.x) ||
            (maskHas(fragEdgeMask, 2) && fragLocal.y < -inset.y) ||
            (maskHas(fragEdgeMask, 3) && fragLocal.y >  inset.y) ||
            // The sides of the box always take the rim, which is what gives the board's
            // plinth a lit edge you can see from a low angle.
            (fragEdgeMask >= 15.0 && n.z < 0.35);
        if (onEdge) {
            float reach = max(abs(fragLocal.x) - inset.x, abs(fragLocal.y) - inset.y);
            float glow = clamp(reach / max(0.5 - min(inset.x, inset.y), 0.001), 0.0, 1.0);
            rgb = mix(rgb, fragEdge.rgb, clamp(0.30 + 0.55 * glow, 0.0, 1.0));
        }
    }

    outColor = vec4(rgb, fragColor.a);
}
