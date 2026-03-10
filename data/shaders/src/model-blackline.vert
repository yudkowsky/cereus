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
    mat4 mvp = pc.proj * pc.view * pc.model;

    vec4 clip_pos = mvp * vec4(position, 1.0);

    // get normal direction in clip space
    vec3 clip_normal = mat3(mvp) * input_normal;

    // normalize only xy — we just need the screen-plane direction
    vec2 dir = normalize(clip_normal.xy);

    clip_pos.xy += dir * 0.005 * clip_pos.w;

    gl_Position = clip_pos;

    normal = input_normal;
    color = vec3(0.0);
}
