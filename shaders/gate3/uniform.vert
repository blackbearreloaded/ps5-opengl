#version 450

layout(location = 0) in vec2 in_position;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 vertex_color;

layout(std140, set = 0, binding = 0) uniform DrawParams
{
    vec4 tint;
    vec2 offset;
} params;

void main()
{
    gl_Position = vec4(in_position + params.offset, 0.0, 1.0);
    vertex_color = in_color * params.tint;
}
