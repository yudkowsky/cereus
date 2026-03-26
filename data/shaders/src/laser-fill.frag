#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 frag_pos_model;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

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
    float intensity = pc.color.a;
    out_color = vec4(pc.color.rgb * intensity, intensity);
    out_normal = vec4(0.0);
}
