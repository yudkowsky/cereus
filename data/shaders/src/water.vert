#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec3 in_color;
layout(location = 4) in mat4 instance_model;

layout(location = 0) out vec3 frag_world_pos;
layout(location = 1) out vec3 frag_normal;

layout(push_constant) uniform PushConstants
{
    mat4 view;
    mat4 proj;
    float time;
} pc;

void main()
{
    vec4 world_pos = instance_model * vec4(in_position, 1.0);

    float wave_scale = 1.5;
    float wx = world_pos.x;
    float wz = world_pos.z;

    float h = wave_scale * (sin(wx * 3.0 + pc.time * 1.5) * 0.05 
                          + sin(wz * 2.5 + pc.time * 1.3) * 0.03);
    world_pos.y += h;

    float dhdx = wave_scale * cos(wx * 3.0 + pc.time * 1.5) * 0.05 * 3.0;
    float dhdz = wave_scale * cos(wz * 2.0 + pc.time * 1.1) * 0.03 * 2.0;

    frag_normal = normalize(vec3(-dhdx, 1.0, -dhdz));
    frag_world_pos = world_pos.xyz;
    gl_Position = pc.proj * pc.view * world_pos;
}
