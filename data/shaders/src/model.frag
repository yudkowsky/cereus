#version 450

#include "water-height.glsl"

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

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
    if (pc.water_base_y > -100.0)
    {
        float water_y = pc.water_base_y + waterHeight(frag_world_pos.xyz, pc.time);
        if (frag_world_pos.y > water_y) discard;
    }

    float low = 20.0 / 255.0;
    float high = 200.0 / 255.0;

    vec3 N = normalize(normal);
    vec3 light_direction = normalize(vec3(0.3, 1.0, 0.5));
    float lighting = dot(N, light_direction) * 0.5 + 0.5;
    out_color = vec4(color * vec3(mix(low, high, lighting)), 1.0);
    out_normal = vec4(N, 0.0);
}
