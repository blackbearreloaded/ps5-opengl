#version 450
// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later


layout(set = 0, binding = 0) uniform sampler2D first_texture;
layout(set = 0, binding = 1) uniform sampler2D second_texture;
layout(location = 0) out vec4 color;

void main()
{
    vec4 first = texture(first_texture, vec2(0.5));
    vec4 second = texture(second_texture, vec2(0.5));
    color = vec4(first.r, second.g, 0.0, 1.0);
}
