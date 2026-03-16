#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal;
layout(location = 3) in vec3 input_color;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 color;
layout(location = 2) out vec3 frag_world_pos;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 uv_rect;
    float alpha;
    float water_base_y;
    float time;
} 
pc;

void main()
{
    vec4 world_pos = pc.model * vec4(position, 1.0);
    gl_Position = pc.proj * pc.view * world_pos;
    normal = mat3(pc.model) * input_normal;
    color = input_color;
	frag_world_pos = world_pos.xyz;
}
