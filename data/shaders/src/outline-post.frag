#version 450

#include "edge-detect.glsl"

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D depth_texture;
layout(set = 1, binding = 0) uniform sampler2D normal_texture;
layout(set = 2, binding = 0) uniform sampler2D rt_result;

layout(push_constant) uniform PushConstants
{
    vec2 texel_size;
    float depth_threshold;
    float normal_threshold;
    float focal_length;
    float debug_mode;
}
pc;

float linearize(float raw_depth)
{
    float z_near = 1.0;
    float z_far  = 300.0;
    return (z_near * z_far) / (z_far - raw_depth * (z_far - z_near));
}

void main()
{
    vec2 step = pc.texel_size;

	// depth edge detection
    float center = linearize(texture(depth_texture, frag_uv).r);
    float up     = linearize(texture(depth_texture, frag_uv + vec2(0.0,    step.y)).r);
    float down   = linearize(texture(depth_texture, frag_uv - vec2(0.0,    step.y)).r);
    float left   = linearize(texture(depth_texture, frag_uv - vec2(step.x, 0.0)).r);
    float right  = linearize(texture(depth_texture, frag_uv + vec2(step.x, 0.0)).r);
    bool do_depth_edge = detectDepthEdge(center, up, down, left, right, pc.focal_length, pc.depth_threshold);

    // normal edge detection
	bool do_normal_edge = false;
    vec3 n_center = texture(normal_texture, frag_uv).rgb;
    bool has_normal = dot(n_center, n_center) > 0.01;
    if (has_normal)
    {
        vec3 n_up    = texture(normal_texture, frag_uv + vec2(0.0, step.y)).rgb;
        vec3 n_down  = texture(normal_texture, frag_uv - vec2(0.0, step.y)).rgb;
        vec3 n_left  = texture(normal_texture, frag_uv - vec2(step.x, 0.0)).rgb;
        vec3 n_right = texture(normal_texture, frag_uv + vec2(step.x, 0.0)).rgb;
        do_normal_edge = detectNormalEdge(n_center, n_up, n_down, n_left, n_right, pc.normal_threshold);
    }

	if (pc.debug_mode > 0.9)
    {
		if (do_depth_edge == true) out_color = vec4(1.0, 0.0, 0.0, 1.0);
        if (do_normal_edge == true) out_color = vec4(0.0, 0.0, 1.0, 1.0);
        if (!do_depth_edge && !do_normal_edge) out_color = vec4(0.0, 0.0, 0.0, 1.0);
    }
    else
    {
        if (do_depth_edge == true || do_normal_edge == true) out_color = vec4(0.0, 0.0, 0.0, 1.0);
        else discard;
    }
}
