#version 450

layout(location = 0) out vec4 vertex_color;

const vec2 positions[3] = vec2[](
    vec2(-0.75, -0.75),
    vec2( 0.75, -0.75),
    vec2( 0.00,  0.75)
);

void main()
{
    vec2 position = positions[gl_VertexIndex];
    gl_Position = vec4(position, 0.0, 1.0);
    vertex_color = vec4(position * 0.5 + 0.5, 0.5, 1.0);
}
