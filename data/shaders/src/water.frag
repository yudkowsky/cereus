#version 450

#include "linearize-depth.glsl"

layout(location = 0) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out float out_water_depth;

layout(set = 0, binding = 0) uniform sampler2D scene_texture;
layout(set = 1, binding = 0) uniform sampler2D depth_texture;
layout(set = 2, binding = 0) uniform sampler2D paint_texture;
layout(set = 3, binding = 0) uniform sampler2D water_texture;
layout(set = 4, binding = 0) uniform sampler2D reflection_texture;
layout(set = 5, binding = 0) uniform sampler2D grid_texture;

layout(push_constant) uniform PushConstants 
{
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    float time;
    float focal_length;
    float water_base_y;
    float tile_length;
    vec3 camera_position;
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
const float max_depth_difference = 0.02;

// push grid lines by normal
const float grid_push_by_normal = 0.5;

// grid line dimensions
const float half_grid_line_width = 0.02;
const float corner_size = 0.025;

// grid line graphics
//const vec3 grid_line_tint = { 0.2, 0.4, 0.6 };
const float grid_opacity = 0.1;

// reflections
const float reflection_distortion_strength = 0.0001;
const float min_reflection = 0.1;
const float fresnel_exponent = 4.0;

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
    // write water depth for use in waterline detection
    out_water_depth = gl_FragCoord.z;

    // idea here is to only do water.vert once, so all vertices get to fragment shader here (unlike before, which would be depth tested)
    // so now i instead blend out all geometry we don't want to actually render; can't discard because that would not output the water depth to texture. TODO: check if no better solution here
    float scene_depth = texture(depth_texture, gl_FragCoord.xy * (1.0/vec2(textureSize(depth_texture, 0)))).r;
    if (gl_FragCoord.z >= scene_depth)
    {
        out_color = vec4(0.0); // water is behind scene geometry: blend out
        return;
    }

    vec2 texel = 1.0 / vec2(textureSize(depth_texture, 0));
    vec2 screen_uv = gl_FragCoord.xy * texel;
    vec3 scene = texture(scene_texture, screen_uv).rgb;

    // tint scene
    float water_surface_linear_depth = linearizeDepth(gl_FragCoord.z);
    float scene_center_linear_depth = linearizeDepth(texture(depth_texture, screen_uv).r);
    float underwater_distance = max(scene_center_linear_depth - water_surface_linear_depth, 0.0);
    float tint_amount = clamp(underwater_distance / max_tint_depth, tint_min, tint_max);
    vec3 base_color = mix(scene, water_depth_tint, tint_amount);

    // move around by normals
    vec2 fft_uv = frag_world_pos.xz / pc.tile_length;
    vec3 normal = normalize(texture(water_texture, fft_uv).xyz);

    vec2 normal_push = normal.xz * grid_push_by_normal;
    vec2 pushed_xz = frag_world_pos.xz + normal_push;
    vec2 grid_pos = pushed_xz - 0.5;

    // sample paint texture at this world position
    vec2 paint_uv = (pushed_xz + 0.5) / WATER_PAINT_TILE_COUNT;
    vec2 snapped = (floor(paint_uv * WATER_PAINT_SIDE) + 0.5) / WATER_PAINT_SIDE;
    float paint_value = texture(paint_texture, snapped).r;

    // grid lines
    vec2 grid_uv = fract(grid_pos / 4.0);
    vec4 grid_color = texture(grid_texture, grid_uv).rgba;
    base_color += grid_color.rgb * grid_color.a * grid_opacity;

    /*
    // grid line width and opacity scale with paint
    float effective_half_width = half_grid_line_width * paint_value;
    float effective_corner_size = corner_size * paint_value;
    float effective_opacity = grid_opacity * paint_value;

    // is this on the grid?
    vec2 distance_to_line = abs(fract(grid_pos) - 0.5);
    float inner_size = 0.5 - effective_half_width - effective_corner_size;
    vec2 pos_to_inner = distance_to_line - vec2(inner_size);
    float sdf = length(max(pos_to_inner, vec2(0.0))) + min(max(pos_to_inner.x, pos_to_inner.y), 0.0) - effective_corner_size;
    if (sdf > 0.0) 
    {
        base_color = mix(base_color, grid_line_tint, effective_opacity);
    }
    */

    // reflection based on fresnel strength
    vec3 view_dir = normalize(pc.camera_position.xyz - frag_world_pos);
    float cos_theta = max(dot(view_dir, normal), 0.0);
    float fresnel = min_reflection + (1.0 - min_reflection) * pow(1.0 - cos_theta, fresnel_exponent);

    // reflection distortion based on normals and distance to camera
    float dist_to_camera = distance(pc.camera_position, frag_world_pos);
    float pixel_world_size = dist_to_camera / pc.focal_length;
    float distortion_strength = reflection_distortion_strength / pixel_world_size;
    vec2 reflection_uv_offset = normal.xz * distortion_strength;
    vec2 reflection_uv = screen_uv + reflection_uv_offset;
    vec3 reflection_color = texture(reflection_texture, reflection_uv).rgb;

    // specular reflection
    /*
    vec3 light_dir = normalize(vec3(0.3, 1.0, 0.2)); // TODO: establish canonical 'sun direction'
    vec3 halfway = normalize(view_dir + light_dir);
    float spec_dot = max(dot(normal, halfway), 0.0);
    float specular = step(0.95, pow(spec_dot, 400.0));
    vec3 sun_color = vec3(1.0, 0.95, 0.8);
    base_color += specular * sun_color;
    */

    base_color = mix(base_color, reflection_color, fresnel);

    out_color = vec4(base_color, 1.0);
}
