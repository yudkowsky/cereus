#version 450

#include "edge-detect.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D scene_texture;
layout(set = 1, binding = 0) uniform sampler2D depth_texture;
layout(set = 2, binding = 0) uniform sampler2D water_depth_texture;
layout(set = 3, binding = 0) uniform sampler2D paint_texture;

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

// paint texture definitions
// TODO: these are same as in everything.h, but can't include that file... a bit messy
#define WATER_PAINT_TILE_COUNT 64
#define WATER_PAINT_RESOLUTION 16
#define WATER_PAINT_SIDE (WATER_PAINT_TILE_COUNT * WATER_PAINT_RESOLUTION)

// water tint
const float max_tint_depth = 1.0;
const vec3 water_depth_tint = vec3(0.00, 0.01, 0.04);
const float tint_min = 0.5;
const float tint_max = 0.9;

// outlines
const float outline_radius_px = 2.0;
const float max_depth_difference = 0.1;

// push grid lines by normal
const float grid_push_by_normal = 0.5;

// grid line dimensions
const float half_grid_line_width = 0.02;
const float corner_size = 0.025;

// grid line graphics
const vec3 grid_line_tint = { 0.2, 0.4, 0.6 };
const float grid_opacity = 0.1;

// used for detecting shoreline in 8 directions
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
    vec3 base_color = mix(scene, water_depth_tint, tint_amount);

    // sample paint texture at this world position
    vec2 paint_uv = (frag_world_pos.xz + 0.5) / WATER_PAINT_TILE_COUNT;
    vec2 snapped = (floor(paint_uv * WATER_PAINT_SIDE) + 0.5) / WATER_PAINT_SIDE;
    float paint_value = texture(paint_texture, snapped).r;

    // grid line width and opacity scale with paint
    float effective_half_width = half_grid_line_width * paint_value;
    float effective_corner_size = corner_size * paint_value;
    float effective_opacity = grid_opacity * paint_value;

    // move around by normals
    vec2 normal_push = frag_normal.xz * grid_push_by_normal;
    vec2 pushed_xz = frag_world_pos.xz + normal_push;
    vec2 grid_pos = pushed_xz - 0.5;

    // is this on the grid?
    vec2 distance_to_line = abs(fract(grid_pos) - 0.5);
    float inner_size = 0.5 - effective_half_width - effective_corner_size;
    vec2 pos_to_inner = distance_to_line - vec2(inner_size);
    float sdf = length(max(pos_to_inner, vec2(0.0))) + min(max(pos_to_inner.x, pos_to_inner.y), 0.0) - effective_corner_size;
    if (sdf > 0.0) 
    {
        base_color = mix(base_color, grid_line_tint, effective_opacity);
    }

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
