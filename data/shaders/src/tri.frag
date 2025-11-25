#version 450

layout(set=0, binding=0) uniform sampler2D input_texture; 
layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 out_color;

const vec3 light_direction = vec3(0.3, 1, 0.5);

void main()
{
//  out_color = texture(input_texture, uv) * dot(normal, normalize(light_direction));
	out_color = texture(input_texture, uv);
}
