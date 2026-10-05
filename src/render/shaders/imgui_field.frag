// SPDX-License-Identifier: GPL-3.0-or-later
//
// A copy of Dear ImGui's Vulkan fragment shader; see `imgui_field.vert` for why the field
// needs its own pipeline. Keep this byte-for-byte equivalent to the backend's
// `glsl_shader.frag`.
#version 450 core

layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform sampler2D sTexture;
layout(location = 0) in struct { vec4 Color; vec2 UV; } In;

void main()
{
    fColor = In.Color * texture(sTexture, In.UV.st);
}
