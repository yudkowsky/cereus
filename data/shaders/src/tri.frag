#version 450

#include "water-height.glsl"

layout(set = 0, binding = 0) uniform sampler2D input_texture;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

layout(push_constant) uniform PC 
{
    mat4 view;
    mat4 projection;
    float water_base_y;
    float time;
}
pc;

const vec3 light_direction = vec3(0.3, 1, 0.5);

void main()
{
    if (pc.water_base_y > -100.0)
    {
        float water_y = pc.water_base_y + waterHeight(frag_world_pos.xyz, pc.time);
        if (frag_world_pos.y > water_y) discard;
    }

    vec4 tex = texture(input_texture, uv);
    float light = max(dot(normalize(normal), normalize(light_direction)), 0.2);
    out_color = vec4(tex.rgb * light, tex.a);
    out_normal = vec4(normalize(normal), 0.0);
}
