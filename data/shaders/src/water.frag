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
    vec4 level_aabb_min;
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

// TODO: annoying and bad to define twice (and now outdated)
const float WATER_PAINT_TILE_COUNT = 64;
const float WATER_PAINT_SIDE = 64 * 16;

// depth tinting
const float max_tint_depth = 1.0;
const vec3 water_depth_tint = vec3(0.0, 0.01, 0.04);
const float tint_min = 0.5;
const float tint_max = 0.9;

// grid
const float grid_push_by_normal = 0.1;
const float grid_thinning = 0.4;
const float grid_opacity = 0.1;
const float grid_line_normal_offset_strength = 0.5;
const float grid_reflection_scaling = 4.0;

// reflection shenanigans
const float reflection_distortion_strength = 0.0001;
const float min_reflection = 0.02; // looking straight down should still have some amount of reflection
const float fresnel_exponent = 4.0;
//const float reflection_edge_fade = 0.5; // TODO: go back and look at this - didn't solve the problem i wanted, but might be a nice effect regardless?

const vec3 sky_horizon = vec3(0.06, 0.70, 0.50);
const vec3 sky_mid     = vec3(0.06, 0.45, 0.60);
const vec3 sky_zenith  = vec3(0.06, 0.30, 0.40);

// specular
const float glint_half_angle_deg = 0.8;
const float glint_intensity = 1.5;
const vec3 glint_color = vec3(1.0, 0.65, 0.40);

void main() 
{
    vec2 texel = 1.0 / vec2(textureSize(depth_texture, 0));
    vec2 screen_uv = gl_FragCoord.xy * texel;
    vec3 scene = texture(scene_texture, screen_uv).rgb;

    // TINT

    float water_surface_linear_depth = linearizeDepth(gl_FragCoord.z);
    float scene_center_linear_depth = linearizeDepth(texture(depth_texture, screen_uv).r);
    float underwater_distance = max(scene_center_linear_depth - water_surface_linear_depth, 0.0);
    float tint_amount = clamp(underwater_distance / max_tint_depth, tint_min, tint_max);
    vec3 base_color = mix(scene, water_depth_tint, tint_amount);

    // GRID
    
    // sample unmodified normal in order to figure out if should be grid here
    vec2 fft_uv = frag_world_pos.xz / view_constants.water_tile_length;
    vec3 unmodified_normal = normalize(texture(water_texture, fft_uv).xyz);

    // get grid pos given movement by unmodified normals
    vec2 normal_push = unmodified_normal.xz * grid_push_by_normal;
    vec2 pushed_xz = frag_world_pos.xz + normal_push;

    // sample paint texture given movement by unmodified normal
    vec2 paint_uv = (pushed_xz - view_constants.level_aabb_min.xz) / WATER_PAINT_TILE_COUNT;
    vec2 snapped = (floor(paint_uv * WATER_PAINT_SIDE) + 0.5) / WATER_PAINT_SIDE;
    float paint_value = texture(paint_texture, snapped).r;

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
    float grid_mult = grid * paint_value;
    float grid_sub = max(grid - (1.0 - paint_value), 0.0);
    float grid_shaped = mix(grid_mult, grid_sub, grid_thinning);

    base_color += vec3(grid_shaped) * grid_opacity;

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

    // REFLECTION

    vec3 view_dir = normalize(view_constants.camera_position.xyz - frag_world_pos);

    // direction surface reflects towards
    vec3 reflect_dir = reflect(-view_dir, normal);

    float t = clamp(reflect_dir.y, 0.0, 1.0);
    //vec3 reflection_color = mix(sky_horizon, sky_zenith, t);
    vec3 reflection_color = (t < 0.5)
        ? mix(sky_horizon, sky_mid,    t * 2.0)
        : mix(sky_mid,     sky_zenith, (t - 0.5) * 2.0);

    // alpha 0 means no scene rendered here, so do the fancy sky
    float dist_to_camera = distance(view_constants.camera_position.xyz, frag_world_pos);
    float pixel_world_size = dist_to_camera / view_constants.focal_length;
    float distortion_strength = reflection_distortion_strength / pixel_world_size;
    vec2 reflection_uv = screen_uv + normal.xz * distortion_strength;
    vec4 reflected_geometry = texture(reflection_texture, reflection_uv);
    reflection_color = mix(reflection_color, reflected_geometry.rgb, reflected_geometry.a);

    // amount of reflection from fresnel
    float cos_theta = max(dot(view_dir, normal), 0.0);
    float reflection_dampen_on_foam = mix(1.0, grid_reflection_scaling, clamp(grid * paint_value, 0.0, 1.0));
    float fresnel = (min_reflection + (1.0 - min_reflection) * pow(1.0 - cos_theta, fresnel_exponent)) * reflection_dampen_on_foam;

    //float edge_mask = smoothstep(0.0, reflection_edge_fade, underwater_distance);
    base_color = mix(base_color, reflection_color, fresnel); // * edge_mask);

    // SPECULAR REFLECTION

    vec3 to_sun = normalize(-view_constants.light_direction.xyz);
    float sun_align = dot(reflect_dir, to_sun);

    float glint_inner = cos(radians(glint_half_angle_deg));
    float glint = smoothstep(glint_inner, 1.0, sun_align);

    base_color += glint_color * glint * glint_intensity * (1.0 - reflected_geometry.a);

    out_color = vec4(base_color, 1.0);
}
