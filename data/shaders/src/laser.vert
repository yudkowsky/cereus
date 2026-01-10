#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec3 out_normal;
layout(location = 2) out vec3 out_frag_pos_model;

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
    out_frag_pos_model = in_position;

    out_uv = in_uv; // unused in fragment shader
    out_normal = mat3(pc.model) * in_normal; // also unused in fragment shader

	vec4 world_pos = pc.model * vec4(in_position, 1.0);
    gl_Position = pc.projection * pc.view * world_pos;
}
