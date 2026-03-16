#version 450

#include "water-height.glsl"

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec3 frag_world_pos;

layout(push_constant) uniform PC 
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 uv_rect;
    float alpha;
    float water_base_y;
    float time;
}
pc;

void main()
{
    vec4 world_pos = pc.model * vec4(position, 1.0);
    gl_Position = pc.projection * pc.view * world_pos;
    uv = pc.uv_rect.xy + input_uv * (pc.uv_rect.zw - pc.uv_rect.xy);
    normal = vec3(pc.model * vec4(input_normal, 0.0));
    frag_world_pos = world_pos.xyz;
}
