#version 450

layout(location = 0) in vec4 vertex_color;
layout(location = 0) out vec4 color;

void main()
{
    color = vec4(vertex_color.rgb * 0.5 + 0.25, 1.0);
}
