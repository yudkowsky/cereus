#version 450

#include "water-height.glsl"

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec3 in_color;
layout(location = 4) in mat4 instance_model;

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
}
pc;

void main()
{
    vec4 grid_world = instance_model * vec4(in_position, 1.0);
    vec2 grid_xz = grid_world.xz;

    vec3 displacement = waterDisplacement(grid_xz, pc.time);
    vec4 world_pos = grid_world + vec4(displacement, 0.0);

    frag_world_pos = world_pos.xyz;
    frag_normal = waterNormal(grid_xz, pc.time);

    gl_Position = pc.proj * pc.view * world_pos;
}
