#version 450

#include "linearize-depth.glsl"

layout(location = 0) in vec2 frag_uv;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D depth_texture;
layout(set = 1, binding = 0) uniform sampler2D normal_texture;

layout(push_constant) uniform PushConstants
{
    vec2 texel_size;
    float depth_threshold;
    float normal_threshold;
    float focal_length;
    float debug_mode;
}
pc;

bool detectDepthEdge(float center, float up, float down, float left, float right, float focal_length, float threshold)
{
    float laplacian = abs(up + down - 2.0 * center) + abs(left + right - 2.0 * center);
    float normalized = (laplacian / center) * focal_length;
    return normalized > threshold ? true : false;
}

bool detectNormalEdge(vec3 center, vec3 up, vec3 down, vec3 left, vec3 right, float threshold)
{
    float max_diff = max(max(1.0 - dot(center, up), 1.0 - dot(center, down)), max(1.0 - dot(center, left), 1.0 - dot(center, right)));
    return max_diff > threshold ? true : false;
}

void main()
{
    vec2 step = pc.texel_size;

	// depth edge detection
    float center = linearizeDepth(texture(depth_texture, frag_uv).r);
    float up     = linearizeDepth(texture(depth_texture, frag_uv + vec2(0.0,    step.y)).r);
    float down   = linearizeDepth(texture(depth_texture, frag_uv - vec2(0.0,    step.y)).r);
    float left   = linearizeDepth(texture(depth_texture, frag_uv - vec2(step.x, 0.0)).r);
    float right  = linearizeDepth(texture(depth_texture, frag_uv + vec2(step.x, 0.0)).r);
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
        vec4 output_color = vec4(0.0);
		if (do_depth_edge == true) output_color += vec4(1.0, 0.0, 0.0, 1.0);
        if (do_normal_edge == true) output_color += vec4(0.0, 0.0, 1.0, 1.0);
        out_color = output_color;
    }
    else
    {
        if (do_depth_edge == true || do_normal_edge == true) out_color = vec4(0.0, 0.0, 0.0, 1.0);
        else discard;
    }
}
