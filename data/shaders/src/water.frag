#version 450

layout(location = 0) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    mat4 view;
    mat4 proj;
	float time;
}
pc;

void main()
{
	float dx = 2.0 * cos(frag_world_pos.x * 3.0 + pc.time * 1.5) * 0.05;
    float dz = 3.0 * cos(frag_world_pos.z * 2.0 + pc.time * 1.1) * 0.03;
    vec3 normal = normalize(vec3(-dx, 1.0, -dz));

    out_color = vec4(0.1, 0.3, 0.6, 1.0);
    out_normal = vec4(normal, 1.0);
}
