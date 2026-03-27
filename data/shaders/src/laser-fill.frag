#version 450

layout(location = 0) in vec3 world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

layout(push_constant) uniform PC
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 color;
    vec4 start_clip_plane;
    vec4 end_clip_plane;
}
pc;

void main()
{
	if (dot(pc.start_clip_plane, vec4(world_pos, 1.0)) < 0.0 || dot(pc.end_clip_plane, vec4(world_pos, 1.0)) < 0.0) discard;

    float intensity = pc.color.a;
    out_color = vec4(pc.color.rgb * intensity, intensity);
    out_normal = vec4(0.0);
}
