#version 450
// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later


layout(location = 0) in vec4 vertex_color;
layout(location = 0) out vec4 color;

void main()
{
    color = vec4(vertex_color.rgb * 0.5 + 0.25, 1.0);
}
