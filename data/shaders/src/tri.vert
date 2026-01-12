#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal;

layout(location = 3) in mat4 instance_model;
layout(location = 7) in vec4 instance_uv_rect;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 normal;

layout(push_constant) uniform PC 
{
    mat4 view;
    mat4 projection;
}
pc;

void main()
{
    mat4 mvp = pc.projection * pc.view * instance_model;
    gl_Position = mvp * vec4(position, 1.0);
    uv = instance_uv_rect.xy + input_uv * (instance_uv_rect.zw - instance_uv_rect.xy);
    normal = vec3(instance_model * vec4(input_normal, 0.0));
}
