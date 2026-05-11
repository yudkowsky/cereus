#version 450
#include "edge-detect.glsl"
#include "water-height.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D scene_texture;
layout(set = 1, binding = 0) uniform sampler2D depth_texture;
layout(set = 2, binding = 0) uniform sampler2D water_depth_texture;

layout(push_constant) uniform PushConstants 
{
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    float time;
    float focal_length;
    float water_base_y;
}
pc;

const float max_tint_depth = 1.0;
const vec3 water_tint = vec3(0.00, 0.01, 0.04);
const float tint_min = 0.5;
const float tint_max = 0.9;

const float outline_radius_px = 2.0;
const float max_depth_difference = 0.1; // outline suppressed past this TODO: might want to scale this based on depth

const vec2 offsets[8] = vec2[]
(
    vec2( 1.0,  0.0), vec2(-1.0,  0.0),
    vec2( 0.0,  1.0), vec2( 0.0, -1.0),
    vec2( 0.7071,  0.7071), vec2( 0.7071, -0.7071),
    vec2(-0.7071,  0.7071), vec2(-0.7071, -0.7071)
);

void main() 
{
    vec2 texel = 1.0 / vec2(textureSize(depth_texture, 0));
    vec2 screen_uv = gl_FragCoord.xy * texel;
    vec3 scene = texture(scene_texture, screen_uv).rgb;

    // tint scene
    float water_surface_linear_depth = linearizeDepth(gl_FragCoord.z);
    float scene_center_linear_depth = linearizeDepth(texture(depth_texture, screen_uv).r);
    float underwater_distance = max(scene_center_linear_depth - water_surface_linear_depth, 0.0);
    float tint_amount = clamp(underwater_distance / max_tint_depth, tint_min, tint_max);
    vec3 base_color = mix(scene, water_tint, tint_amount);

    // waterline detection
    bool this_is_outline = false;
    for (int offset_index = 0; offset_index < 8; offset_index++) 
    {
        vec2 sample_uv = screen_uv + offsets[offset_index] * outline_radius_px * texel;

        float scene_raw_depth = texture(depth_texture, sample_uv).r;
        float water_raw_depth = texture(water_depth_texture, sample_uv).r;

        if (scene_raw_depth >= 1.0) continue;
        if (water_raw_depth >= 1.0) continue;

        float scene_linear_depth = linearizeDepth(scene_raw_depth);
        float water_linear_depth = linearizeDepth(water_raw_depth);

        if (scene_linear_depth < water_linear_depth && water_linear_depth - scene_linear_depth < max_depth_difference)
        {
            this_is_outline = true;
            break;
        }
    }

    vec3 final_color = this_is_outline ? vec3(0.0) : base_color;
    out_color = vec4(final_color, 1.0);
}
