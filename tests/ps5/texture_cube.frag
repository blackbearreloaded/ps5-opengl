#version 450
// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later


layout(set = 0, binding = 0) uniform samplerCube source_texture;
layout(location = 0) in vec3 direction;
layout(location = 0) out vec4 color;

void main()
{
   color = texture(source_texture, direction);
}
