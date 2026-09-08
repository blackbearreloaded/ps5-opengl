#version 450
// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later


layout(location = 0) in vec4 vertex_color;
layout(location = 0) out vec4 out_color;

void main()
{
    out_color = vertex_color;
}
