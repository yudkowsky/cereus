#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec3 in_color;

layout(location = 0) out vec3 frag_world_pos;

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
    vec4 world_pos = pc.model * vec4(in_position, 1.0);
    
    world_pos.y += 2.0 * (sin(world_pos.x * 3.0 + pc.time * 1.5) * 0.05 
    			        + sin(world_pos.z * 2.0 + pc.time * 1.1) * 0.03);

    frag_world_pos.xyz = world_pos.xyz;
    gl_Position = pc.proj * pc.view * world_pos;
}
