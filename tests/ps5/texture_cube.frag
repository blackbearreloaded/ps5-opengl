#version 450

layout(set = 0, binding = 0) uniform samplerCube source_texture;
layout(location = 0) in vec3 direction;
layout(location = 0) out vec4 color;

void main()
{
   color = texture(source_texture, direction);
}
