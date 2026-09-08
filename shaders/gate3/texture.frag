#version 450
// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later


layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(location = 0) in vec2 vertex_uv;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = texture(source_texture, vertex_uv);
}
