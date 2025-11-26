#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 normal;

layout(push_constant) uniform PC {
    mat4 model;
    mat4 view;
    mat4 projection;
} pc;

void main()
{
    mat4 mvp = pc.projection * pc.view * pc.model;
    gl_Position = mvp * vec4(position, 1.0);
    uv = input_uv;

    normal = vec3(pc.model * vec4(input_normal, 0.0));
}
