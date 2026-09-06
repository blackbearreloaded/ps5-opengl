#version 450

layout(location = 0) in vec2 position;
layout(set = 0, binding = 0) uniform sampler2D source_texture;
layout(set = 0, binding = 16, std140) uniform Defaults {
   float scale;
};
layout(set = 0, binding = 17, std140) uniform Params {
   vec4 offset;
};

void main()
{
   float sampled = textureLod(source_texture, vec2(0.5), 0.0).r;
   gl_Position = vec4(position * scale + offset.xy +
                      vec2(sampled - 1.0, 0.0), 0.0, 1.0);
}
