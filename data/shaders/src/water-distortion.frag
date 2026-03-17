#version 450

struct BoxAABB
{
	vec3 box_min;
    float pad0;
    vec3 box_max;
    float pad1;
};

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;
layout(location = 1) out float out_water_depth;

layout(set = 0, binding = 0) uniform sampler2D atlas_texture;
layout(set = 1, binding = 0) uniform sampler2D underwater_texture;
layout(set = 2, binding = 0) uniform sampler2D depth_texture;
layout(set = 3, binding = 0) uniform AABBData
{
    int aabb_count;
    BoxAABB boxes[16];
}
aabbs;

layout(push_constant) uniform PushConstants 
{
    mat4 view;
    mat4 proj;
    float time;
    float debug_mode;
}
pc;

const float z_near = 1.0;
const float z_far = 300.0;

float linearize_depth(float d)
{
    return z_near * z_far / (z_far - d * (z_far - z_near));
}

void main()
{
    float t = float(aabbs.aabb_count) / 15.0;
    out_color = vec4(t, 0.0, 1.0 - t, 1.0);
}
