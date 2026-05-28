#version 450

layout(set = 0, binding = 0) uniform ViewConstants 
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;
    mat4 light_view_proj;
    vec4 camera_position;
    float water_plane_y;
    float time;
    float water_tile_length;
    float focal_length;
}
view_constants;

layout(set = 4, binding = 0) uniform sampler2D water_texture;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec3 in_color;
layout(location = 4) in mat4 instance_model;

layout(location = 0) out vec3 frag_world_pos;
layout(location = 1) out vec3 frag_normal;

void main()
{
    vec4 grid_world = instance_model * vec4(in_position, 1.0);
    vec2 fft_uv = grid_world.xz / view_constants.water_tile_length;
    float height = textureLod(water_texture, fft_uv, 0.0).w;
    grid_world.y += height;
    frag_world_pos = grid_world.xyz;
    gl_Position = view_constants.view_proj * grid_world;
}
