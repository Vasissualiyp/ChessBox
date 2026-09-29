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

layout(push_constant) uniform Push {
    mat4 viewProj;
    vec4 lightDir;
} push;

layout(location = 0) out vec4 outColor;

bool maskHas(float mask, int bit) {
    return mod(floor(mask / pow(2.0, float(bit))), 2.0) >= 0.5;
}

void main() {
    vec3 n = normalize(fragNormal);
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

    // A seam edge: the cell's side is glued to somewhere else on the board. Drawn only
    // on the sides the geometry actually identified, so a player can pair the two edges
    // by eye rather than being told the whole cell is special.
    if (fragEdge.a > 0.0 && n.z > 0.35) {
        const float kInner = 0.33;   // the slab's half-extent is 0.46
        bool onEdge =
            (maskHas(fragEdgeMask, 0) && fragLocal.x < -kInner) ||
            (maskHas(fragEdgeMask, 1) && fragLocal.x >  kInner) ||
            (maskHas(fragEdgeMask, 2) && fragLocal.y < -kInner) ||
            (maskHas(fragEdgeMask, 3) && fragLocal.y >  kInner);
        if (onEdge) {
            float glow = smoothstep(kInner, 0.46, max(abs(fragLocal.x), abs(fragLocal.y)));
            rgb = mix(rgb, fragEdge.rgb, clamp(0.35 + 0.65 * glow, 0.0, 1.0));
        }
    }

    outColor = vec4(rgb, fragColor.a);
}
