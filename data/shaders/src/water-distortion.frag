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

layout(set = 4, binding = 0) uniform sampler2D rt_result;

layout(push_constant) uniform PushConstants 
{
    mat4 view;
    mat4 proj;
    float time;
    float debug_mode;
    float cam_x;
    float cam_y;
    float cam_z;
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
    vec2 tex_size = vec2(textureSize(rt_result, 0));
    vec2 screen_uv = gl_FragCoord.xy / tex_size;
    vec4 rt = texture(rt_result, screen_uv);
    
    // just pass through the raytraced value
    out_color = vec4(rt.r, rt.g, rt.b, 1.0);
}
