#version 450

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

layout(push_constant) uniform PC
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 uv_rect;
    float alpha;
}
pc;

layout(set = 0, binding = 0) uniform sampler2D tex;

void main()
{
    vec4 color = texture(tex, uv);
    color.a *= pc.alpha;
    out_color = color;

    out_normal = vec4(0.0);
}
