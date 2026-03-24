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
const vec3 water_tint = vec3(0.00, 0.01, 0.04);

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

		// the depth i get from the compute shader is the depth from the water surface. this probably isn't ideal when i'm dealing with outlines, but they're bad right now anyway
        float ray_depth = nd_center.a;
       	float tint_factor = mix(0.3, 0.95, clamp(ray_depth / 0.7, 0.0, 1.0));
        out_color = vec4(mix(color, water_tint, tint_factor), 1.0);
    }
    else
    {
        vec3 N = normalize(frag_normal);
        float water_surface_depth = linearize_depth(gl_FragCoord.z);

        float scene_depth = linearize_depth(texture(depth_texture, screen_uv).r);
        float first_water_depth = max(scene_depth - water_surface_depth, 0.0);
        float dead_zone = 0.05;
        float effective_depth = max(first_water_depth - dead_zone, 0.0);
        float max_distortion_depth = 1.0;
        float distortion_scale = clamp(effective_depth / max_distortion_depth, 0.0, 1.0);

        float focal_length = pc.proj[1][1]; // cotangent of half-fov
        float depth_scale = focal_length / max(water_surface_depth, 0.1);
        vec2 distortion = N.xz * 0.1 * distortion_scale * depth_scale;
        vec2 refracted_uv = screen_uv + distortion;

        float final_depth = linearize_depth(texture(depth_texture, refracted_uv).r);
        float final_water_depth = max(final_depth - water_surface_depth, 0.0);

        vec3 scene = texture(underwater_texture, refracted_uv).rgb;
        vec3 fresnel_contribution = raytrace_output.rgb;
        out_color = vec4(mix(scene, water_tint, 0.35) + fresnel_contribution, 1.0);
	}	
}
