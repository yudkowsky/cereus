#version 450

#include "linearize-depth.glsl"

layout(set = 0, binding = 0) uniform ViewConstants 
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;
    mat4 light_view_proj;
    vec4 camera_position;
    vec4 light_direction;
    float water_plane_y;
    bool discard_below_water_plane;
    float time;
    float water_tile_length;
    float focal_length;
}
view_constants;

layout(set = 1, binding = 0) uniform sampler2D scene_texture;
layout(set = 2, binding = 0) uniform sampler2D depth_texture;
layout(set = 3, binding = 0) uniform sampler2D paint_texture;
layout(set = 4, binding = 0) uniform sampler2D water_texture;
layout(set = 5, binding = 0) uniform sampler2D reflection_texture;
layout(set = 6, binding = 0) uniform sampler2DArray grid_texture;
layout(set = 7, binding = 0) uniform sampler2DArray grid_normal_texture;

layout(location = 0) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out float out_water_depth;

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
const float grid_push_by_normal = 0.3;

// grid line graphics
const float grid_opacity = 0.05;

// reflections
const float reflection_distortion_strength = 0.0001;
const float min_reflection = 0.1;
const float fresnel_exponent = 4.0;
const float min_reflection_on_grid = 0.5;
const float grid_line_normal_offset_strength = 0.75;

void main() 
{
    // write water depth for use in waterline detection
    out_water_depth = gl_FragCoord.z;

    // idea here is to only do water.vert once, so all vertices get to fragment shader, even ones behind geometry.
    // so now i instead blend out all geometry we don't want to actually render; can't discard because that wouldn't
    // output the water depth to texture.
    float scene_depth = texture(depth_texture, gl_FragCoord.xy * (1.0/vec2(textureSize(depth_texture, 0)))).r;
    if (gl_FragCoord.z >= scene_depth)
    {
        out_color = vec4(0.0); // water is behind scene geometry: blend out
        return;
    }

    vec2 texel = 1.0 / vec2(textureSize(depth_texture, 0));
    vec2 screen_uv = gl_FragCoord.xy * texel;
    vec3 scene = texture(scene_texture, screen_uv).rgb;

    // tint underwater scene based on depth
    float water_surface_linear_depth = linearizeDepth(gl_FragCoord.z);
    float scene_center_linear_depth = linearizeDepth(texture(depth_texture, screen_uv).r);
    float underwater_distance = max(scene_center_linear_depth - water_surface_linear_depth, 0.0);
    float tint_amount = clamp(underwater_distance / max_tint_depth, tint_min, tint_max);
    vec3 base_color = mix(scene, water_depth_tint, tint_amount);

    // sample unmodified normal in order to figure out if should be grid here
    vec2 fft_uv = frag_world_pos.xz / view_constants.water_tile_length;
    vec3 unmodified_normal = normalize(texture(water_texture, fft_uv).xyz);

    // get grid pos given movement by unmodified normals
    vec2 normal_push = unmodified_normal.xz * grid_push_by_normal;
    vec2 pushed_xz = frag_world_pos.xz + normal_push;

    // sample paint texture given movement by unmodified normal
    vec2 paint_uv = (pushed_xz + 0.5) / WATER_PAINT_TILE_COUNT;
    vec2 snapped = (floor(paint_uv * WATER_PAINT_SIDE) + 0.5) / WATER_PAINT_SIDE;
    float paint_value = texture(paint_texture, snapped).r;
    float effective_opacity = grid_opacity * paint_value;

    // water grid animation sampling
    float time_per_frame = 0.125;
    int total_frames = 49;
    float frame_blend = fract(view_constants.time / time_per_frame);
    int frame_a = int(view_constants.time / time_per_frame) % total_frames;
    int frame_b = (frame_a + 1) % total_frames;

    vec2 tile_uv = (pushed_xz + 0.5) / 4.0; // reapeat sampler handles

    // grid is single channel
    float grid = mix(
        texture(grid_texture, vec3(tile_uv, float(frame_a))).r,
        texture(grid_texture, vec3(tile_uv, float(frame_b))).r,
        frame_blend);

    // thinner (and dimmer) lines with lower paint
    const float grid_thinning = 0.4;
    float grid_mult = grid * paint_value;
    float grid_sub = max(grid - (1.0 - paint_value), 0.0);
    float grid_shaped = mix(grid_mult, grid_sub, grid_thinning);

    base_color += vec3(grid_shaped) * grid_opacity * paint_value;

    // normal is dual channel in xz
    vec3 ridge_texture = mix(
        texture(grid_normal_texture, vec3(tile_uv, float(frame_a))).rgb,
        texture(grid_normal_texture, vec3(tile_uv, float(frame_b))).rgb,
        frame_blend) * 2.0 - 1.0;

    vec2 peturb = ridge_texture.xy * paint_value * grid_line_normal_offset_strength;
    vec3 normal = normalize(vec3(
        unmodified_normal.x + peturb.x,
        unmodified_normal.y,
        unmodified_normal.z + peturb.y
    ));

    // reflection based on fresnel strength, dampen if on grid foam
    vec3 view_dir = normalize(view_constants.camera_position.xyz - frag_world_pos);
    float cos_theta = max(dot(view_dir, normal), 0.0);
    float reflection_dampen_on_foam = mix(1.0, min_reflection_on_grid, clamp(grid * paint_value, 0.0, 1.0));
    float fresnel = (min_reflection + (1.0 - min_reflection) * pow(1.0 - cos_theta, fresnel_exponent)) * reflection_dampen_on_foam;

    // reflection distortion based on normals and distance to camera
    float dist_to_camera = distance(view_constants.camera_position.xyz, frag_world_pos);
    float pixel_world_size = dist_to_camera / view_constants.focal_length;
    float distortion_strength = reflection_distortion_strength / pixel_world_size;
    vec2 reflection_uv_offset = normal.xz * distortion_strength;
    vec2 reflection_uv = screen_uv + reflection_uv_offset;

    // offset reflection uv by wave height
    float reflection_height_strength = 0.5;
    float wave_height = frag_world_pos.y - view_constants.water_plane_y;
    float grazing = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));
    float height_shift = wave_height * grazing / pixel_world_size * texel.y * reflection_height_strength;
    reflection_uv.y += height_shift;

    vec3 reflection_color = texture(reflection_texture, reflection_uv).rgb;

    // specular reflection
    /*
    vec3 light_dir = normalize(vec3(0.3, 1.0, 0.2)); // TODO: revisit this, with real sun direction
    vec3 halfway = normalize(view_dir + light_dir);
    float spec_dot = max(dot(normal, halfway), 0.0);
    float specular = step(0.95, pow(spec_dot, 400.0));
    vec3 sun_color = vec3(1.0, 0.95, 0.8);
    base_color += specular * sun_color;
    */

    base_color = mix(base_color, reflection_color, fresnel);

    out_color = vec4(base_color, 1.0);
}
