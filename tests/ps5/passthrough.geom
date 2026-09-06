#version 450

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

layout(location = 0) in vec4 in_color[];
layout(location = 0) out vec4 out_color;

void main()
{
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        out_color = in_color[i];
        EmitVertex();
    }
    EndPrimitive();
}
