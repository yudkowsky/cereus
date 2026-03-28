#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec3 world_pos;

layout(push_constant) uniform PC
{
    mat4 model;
    mat4 inverse_intersection;
    mat4 proj_view;
    vec4 color;
    vec4 start_clip_plane;
    vec4 end_clip_plane;
    vec3 camera_position;
    float radius;
}
pc;

void main()
{
    vec4 world = pc.model * vec4(in_position, 1.0);
    world_pos = world.xyz;
    gl_Position = pc.proj_view * world;
}

