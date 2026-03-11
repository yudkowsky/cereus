#version 450

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
    out_color = vec4(pc.color.rgb * 1.5, 1.0);
    out_normal = vec4(0.0);
}
