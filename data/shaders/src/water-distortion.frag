#version 450

#include "edge-detect.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D atlas_texture;
layout(set = 1, binding = 0) uniform sampler2D underwater_texture;
layout(set = 2, binding = 0) uniform sampler2D depth_texture;
layout(set = 3, binding = 0) uniform sampler2D raytrace_result;
layout(set = 4, binding = 0) uniform sampler2D raytrace_normal_depth;

layout(push_constant) uniform PushConstants 
{
    mat4 view;
    mat4 proj;
    float time;
    float debug_mode;
    float cam_x;
    float cam_y;
    float cam_z;
    float depth_threshold;
    float normal_threshold;
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
    vec2 texture_size = vec2(textureSize(raytrace_result, 0));
    vec2 screen_uv = gl_FragCoord.xy / texture_size;
    vec4 raytrace_output = texture(raytrace_result, screen_uv);
    
    if (raytrace_output.a > 0.0) // alpha is always 1 right now if raytracer wants to output, because it handles all the blending on its side
    {
		vec3 color = raytrace_output.rgb;

		// outlines based on raytracers normal and depth. note that depth is already normalized into world space coords because just passing t_closest value
        vec2 texel = 1.0 / texture_size;

        vec4 nd_center = texture(raytrace_normal_depth, screen_uv);
        vec4 nd_up 	   = texture(raytrace_normal_depth, screen_uv + vec2(0.0, texel.y));
        vec4 nd_down   = texture(raytrace_normal_depth, screen_uv - vec2(0.0, texel.y));
        vec4 nd_left   = texture(raytrace_normal_depth, screen_uv - vec2(texel.x, 0.0));
        vec4 nd_right  = texture(raytrace_normal_depth, screen_uv + vec2(texel.x, 0.0));

		float underwater_edge_difference_constant = 20;
		bool do_depth_edge = detectDepthEdge(nd_center.a, nd_up.a, nd_down.a, nd_left.a, nd_right.a, underwater_edge_difference_constant, pc.depth_threshold);
        bool do_normal_edge = detectNormalEdge(nd_center.rgb, nd_up.rgb, nd_down.rgb, nd_left.rgb, nd_right.rgb, pc.normal_threshold);

        if (do_depth_edge || do_normal_edge)
        {
            color = vec3(0.0);
        }

        float depth = nd_center.a;
        vec3 water_tint = vec3(0.0, 0.01, 0.05);
        float tint_factor = clamp(depth / 1.0, 0.0, 0.8);
        color = mix(color, water_tint, tint_factor);
        
        out_color = vec4(color, 1.0);
    }
    else
    {
        out_color = vec4(0.0, 0.01, 0.05, 1.0);
    }
}
