#version 450

#include "linearize-depth.glsl"

struct LaserLight
{
    vec4 point_0;
    vec4 point_1;
    vec4 color;
};

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
layout(set = 8, binding = 0) readonly buffer LaserLights
{
    int laser_light_count;
    LaserLight laser_lights[];
}
laser_data;
layout(set = 9, binding = 0) uniform sampler2D reflection_distance_texture;

layout(location = 0) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;

// TODO: annoying and bad to define twice (and now outdated)
const float WATER_PAINT_TILE_COUNT = 64;
const float WATER_PAINT_SIDE = 64 * 16;

// depth tinting
const float max_tint_depth = 2.0;
const vec3 water_depth_tint = vec3(0.0, 0.01, 0.04);
const float tint_min = 0.1;
const float tint_max = 0.9;

// grid
const float grid_push_by_normal = 0.1;
const float grid_thinning = 0.4;
const float grid_opacity = 0.07;
const float grid_line_normal_offset_strength = 0.5;
const float grid_reflection_scaling = 4.0;

// reflection shenanigans
const float reflection_distortion_strength = 0.0001;
const float min_reflection = 0.02; // looking straight down should still have some amount of reflection
const float fresnel_exponent = 4.0;

const vec3 sky_horizon = vec3(0.06, 0.70, 0.50);
const vec3 sky_mid     = vec3(0.06, 0.45, 0.60);
const vec3 sky_zenith  = vec3(0.06, 0.30, 0.40);

const float refraction_fade_range = 0.5;
const float refraction_strength = 0.0005;

// specular
const float glint_half_angle_deg = 0.7;
const float glint_intensity = 5.0;
const vec3 glint_color = vec3(0.15, 0.10, 0.05);

// TODO: inline
float rayToSegmentDistance(vec3 ray_origin, vec3 ray_direction, vec3 segment_start, vec3 segment_end, out vec3 segment_closest_point)
{
    vec3 segment_vector  = segment_end - segment_start;
    vec3 origin_to_start = ray_origin - segment_start;

    float ray_dot_ray                  = dot(ray_direction, ray_direction);
    float ray_dot_segment              = dot(ray_direction, segment_vector);
    float segment_dot_segment          = dot(segment_vector, segment_vector);
    float ray_dot_origin_to_start      = dot(ray_direction, origin_to_start);
    float segment_dot_origin_to_start  = dot(segment_vector, origin_to_start);

    float denominator = ray_dot_ray * segment_dot_segment - ray_dot_segment * ray_dot_segment;

    float segment_param = (denominator > 1e-6)
        ? (ray_dot_ray * segment_dot_origin_to_start - ray_dot_segment * ray_dot_origin_to_start) / denominator
        : segment_dot_origin_to_start / max(segment_dot_segment, 1e-6);
    segment_param = clamp(segment_param, 0.0, 1.0);

    float ray_param = (ray_dot_segment * segment_param - ray_dot_origin_to_start) / ray_dot_ray;
    ray_param = max(ray_param, 0.0);

    segment_param = clamp((ray_dot_segment * ray_param + segment_dot_origin_to_start) / max(segment_dot_segment, 1e-6), 0.0, 1.0);

    segment_closest_point = segment_start + segment_param * segment_vector;
    return distance(ray_origin + ray_param * ray_direction, segment_closest_point);
}

void main() 
{
    vec2 texel = 1.0 / vec2(textureSize(depth_texture, 0));
    vec2 screen_uv = gl_FragCoord.xy * texel;

    // depths
    float water_surface_linear_depth = linearizeDepth(gl_FragCoord.z);
    float scene_center_linear_depth = linearizeDepth(texture(depth_texture, screen_uv).r);
    float underwater_distance = max(scene_center_linear_depth - water_surface_linear_depth, 0.0);

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

    float grid = mix(
        texture(grid_texture, vec3(tile_uv, float(frame_a))).r,
        texture(grid_texture, vec3(tile_uv, float(frame_b))).r,
        frame_blend);

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

    // REFRACTION

    float dist_to_camera = distance(view_constants.camera_position.xyz, frag_world_pos);
    float pixel_world_size = dist_to_camera / view_constants.focal_length;

    float refraction_fade = clamp(underwater_distance / refraction_fade_range, 0.0, 1.0);
    vec2 refraction_offset = (normal.xz * refraction_strength * refraction_fade) / pixel_world_size;
    vec2 refraction_uv = screen_uv + refraction_offset;

    float refracted_scene_depth = linearizeDepth(texture(depth_texture, refraction_uv).r);
    if (refracted_scene_depth < water_surface_linear_depth) refraction_uv = screen_uv;

    vec3 scene = texture(scene_texture, refraction_uv).rgb;

    // TINT 

    float tint_amount = clamp(underwater_distance / max_tint_depth, tint_min, tint_max);
    vec3 base_color = mix(scene, water_depth_tint, tint_amount);

    // grid color contribution
    float grid_mult = grid * paint_value;
    float grid_sub = max(grid - (1.0 - paint_value), 0.0);
    float grid_shaped = mix(grid_mult, grid_sub, grid_thinning);
    base_color += vec3(grid_shaped) * grid_opacity;

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
    float distortion_strength = reflection_distortion_strength / pixel_world_size;
    vec2 reflection_uv = screen_uv + normal.xz * distortion_strength;
    vec4 reflected_geometry = texture(reflection_texture, reflection_uv);
    reflection_color = mix(reflection_color, reflected_geometry.rgb, reflected_geometry.a);

    // amount of reflection from fresnel
    float cos_theta = max(dot(view_dir, normal), 0.0);
    float reflection_dampen_on_foam = mix(1.0, grid_reflection_scaling, clamp(grid * paint_value, 0.0, 1.0));
    float fresnel = (min_reflection + (1.0 - min_reflection) * pow(1.0 - cos_theta, fresnel_exponent)) * reflection_dampen_on_foam;

    base_color = mix(base_color, reflection_color, fresnel); // * edge_mask);

    // SPECULAR REFLECTION

    vec3 to_sun = normalize(-view_constants.light_direction.xyz);
    float sun_align = dot(reflect_dir, to_sun);

    float glint_inner = cos(radians(glint_half_angle_deg));
    float glint = smoothstep(glint_inner, 1.0, sun_align);

    base_color += glint_color * glint * glint_intensity * (1.0 - reflected_geometry.a);

    // LASER REFLECTIONS

    vec3 reflected_camera_pos = vec3(
        view_constants.camera_position.x,
        2.0 * view_constants.water_plane_y - view_constants.camera_position.y,
        view_constants.camera_position.y);

    float reflected_distance = texture(reflection_distance_texture, reflection_uv).r;

    vec3 laser_glow = vec3(0.0);
    for (int laser_index = 0; laser_index < laser_data.laser_light_count; laser_index++)
    {
        LaserLight laser = laser_data.laser_lights[laser_index];
        vec3 segment_closest_point;
        float distance_to_segment = rayToSegmentDistance(frag_world_pos, reflect_dir, laser.point_0.xyz, laser.point_1.xyz, segment_closest_point);
        if (segment_closest_point.y < view_constants.water_plane_y) continue;

        float laser_distance = distance(reflected_camera_pos, segment_closest_point);
        if (reflected_distance < laser_distance) continue;

        float laser_radius = laser.point_0.w;
        float core_radius = laser_radius * 0.25;
        float laser_reflection_amount = smoothstep(laser_radius, core_radius, distance_to_segment);
        laser_glow += laser.color.rgb * laser_reflection_amount * laser.point_1.w;
    }
    base_color += laser_glow;

    out_color = vec4(base_color, 1.0);
}
