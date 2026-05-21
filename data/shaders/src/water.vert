#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec3 in_color;
layout(location = 4) in mat4 instance_model;

layout(set = 3, binding = 0) uniform sampler2D water_texture;

layout(location = 0) out vec3 frag_world_pos;
layout(location = 1) out vec3 frag_normal;

layout(push_constant) uniform PushConstants
{
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    float time;
    float focal_length;
    float water_base_y;
    float tile_length;
}
pc;

void main()
{
    vec4 grid_world = instance_model * vec4(in_position, 1.0);
    vec2 fft_uv = grid_world.xz / pc.tile_length;
    float height = textureLod(water_texture, fft_uv, 0.0).w;
    grid_world.y += height;
    frag_world_pos = grid_world.xyz;
    gl_Position = pc.proj * pc.view * grid_world;
}
