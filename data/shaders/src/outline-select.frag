#version 450

#include "water-height.glsl"

layout(location = 2) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;


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
    if (pc.water_base_y > -100.0)
    {
        float water_y = pc.water_base_y + waterHeight(frag_world_pos.xyz, pc.time);
        if (frag_world_pos.y > water_y) discard;
    }

    out_color = vec4(1.0, 0.0, 1.0, 1.0);
    out_normal = vec4(0.0);
}
