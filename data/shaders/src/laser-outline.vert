#version 450
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;

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
    vec3 radial_dir = vec3(normalize(in_position.xy), 0.0);
    vec3 expanded = in_position + radial_dir * 0.05;
    gl_Position = pc.projection * pc.view * pc.model * vec4(expanded, 1.0);
}
