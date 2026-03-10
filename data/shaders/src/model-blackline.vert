#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal;
layout(location = 3) in vec3 input_color;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 color;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 uv_rect;
} pc;

void main()
{
    vec3 expanded = position + normalize(input_normal) * 0.05;

    gl_Position = pc.proj * pc.view * pc.model * vec4(expanded, 1.0);

    normal = input_normal;
    color = vec3(0.0);
}
