// SPDX-License-Identifier: GPL-3.0-or-later
//
// A copy of Dear ImGui's Vulkan vertex shader, compiled here so the board screen's
// ambient field can be drawn with its own multisample-matched pipeline. The backend's
// pipeline is single-sample (the overlay pass draws into the resolved colour image) and
// cannot legally draw into the board's 4-sample attachment, so the field - which must be
// drawn *before* the board, inside the board's own pass, to sit behind it - needs a
// pipeline of its own built from the same shader. Keep this byte-for-byte equivalent to
// the backend's `glsl_shader.vert`; it exists only to change the sample count.
#version 450 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(push_constant) uniform uPushConstant { vec2 uScale; vec2 uTranslate; } pc;

out gl_PerVertex { vec4 gl_Position; };
layout(location = 0) out struct { vec4 Color; vec2 UV; } Out;

void main()
{
    Out.Color = aColor;
    Out.UV = aUV;
    gl_Position = vec4(aPos * pc.uScale + pc.uTranslate, 0, 1);
}
