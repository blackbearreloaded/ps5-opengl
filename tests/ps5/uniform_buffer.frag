#version 450

layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(set = 0, binding = 16, std140) uniform Defaults {
   vec4 multiplier;
};
layout(set = 0, binding = 17, std140) uniform Params {
   vec4 color_value;
};
layout(location = 0) out vec4 color;

void main()
{
   color = texture(source_texture, vec2(0.5)) * color_value * multiplier;
}
