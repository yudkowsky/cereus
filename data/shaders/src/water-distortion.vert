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
    float time;
}
pc;

void main()
{
    vec4 world_pos = instance_model * vec4(in_position, 1.0);

	world_pos.y += waterHeight(world_pos.xyz, pc.time);
    frag_normal = waterNormal(world_pos.xyz, pc.time);

    frag_world_pos = world_pos.xyz;
    gl_Position = pc.proj * pc.view * world_pos;
}
