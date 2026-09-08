#version 450
// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later


layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 0) out vec2 vertex_uv;

void main()
{
    gl_Position = vec4(in_position, 0.0, 1.0);
    vertex_uv = in_uv;
}
