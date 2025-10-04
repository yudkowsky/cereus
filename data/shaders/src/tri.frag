#version 450
layout(location = 0) in vec2 texture_coords;
layout(set = 0, binding = 0) uniform sampler2D sprite_texture;
layout(location = 0) out vec4 out_color;

void main()
{
	out_color = texture(sprite_texture, texture_coords);
}
