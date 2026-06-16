#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal;
layout(location = 3) in vec3 input_color;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 color;
layout(location = 2) out vec3 frag_world_pos;

layout(set = 0, binding = 0) uniform ViewConstants 
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;
    mat4 light_view_proj;
    vec4 camera_position;
    vec4 light_direction;
    vec4 level_aabb_min;
    float water_plane_y;
    bool discard_below_water_plane;
    float time;
    float water_tile_length;
    float focal_length;
}
view_constants;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    vec4 color;
} 
pc;

void main()
{
    vec4 world_pos = pc.model * vec4(position, 1.0);
    gl_Position = view_constants.proj * view_constants.view * world_pos;
    normal = mat3(pc.model) * input_normal;
    color = input_color;
	frag_world_pos = world_pos.xyz;
}
