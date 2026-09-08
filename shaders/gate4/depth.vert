#version 450
// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later


layout(location = 0) in vec3 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 vertex_color;

void main()
{
    gl_Position = vec4(in_position, 1.0);
    vertex_color = in_color;
}
