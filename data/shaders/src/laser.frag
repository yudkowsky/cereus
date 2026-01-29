#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 frag_pos_model;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PC
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 color;
}
pc;

void main()
{
    float alpha = 0.4;
    vec3 color_cap = vec3(0.5, 0.5, 0.5);
    vec3 clamped_color = min(pc.color.rgb, color_cap);
    out_color = vec4(clamped_color * alpha, alpha);
}
