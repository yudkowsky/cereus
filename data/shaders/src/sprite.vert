#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal; // still declared to match vertex format, but unused

layout(location = 0) out vec2 uv;

layout(push_constant) uniform PC 
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 uv_rect;
}
pc;

void main()
{
    mat4 mvp = pc.projection * pc.view * pc.model;
    gl_Position = mvp * vec4(position, 1.0);
    uv = pc.uv_rect.xy + input_uv * (pc.uv_rect.zw - pc.uv_rect.xy);
}
