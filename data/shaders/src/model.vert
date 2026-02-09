#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 uv_rect;
}
pc;

layout(location = 0) out vec3 normal;

void main()
{
    gl_Position = pc.proj * pc.view * pc.model * vec4(position, 1.0);
    normal = mat3(pc.model) * input_normal;
}
