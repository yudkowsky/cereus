#version 450

#include "laser-clip-plane.glsl"

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

layout(location = 0)      in vec3 world_pos;
layout(location = 1) flat in mat4 inverse_intersection;
layout(location = 5) flat in vec4 start_clip_plane;
layout(location = 6) flat in vec4 end_clip_plane;
layout(location = 7) flat in vec4 instance_color;
layout(location = 8) flat in float half_length;

layout(set = 1, binding = 0, r32ui) uniform coherent uimage2D head_image;
layout(set = 2, binding = 0) buffer FragmentPool { uvec4 fragments[]; };
layout(set = 3, binding = 0) buffer AtomicCounter { uint counter; };

const float beam_radius = 0.5;
const float falloff_exponent = 1.5;
const float outline_intensity_boundary = 0.87;

void main()
{
    vec4 fragment_position_in_model_space = inverse_intersection * vec4(world_pos, 1.0);
    vec4 camera_origin_in_model_space = inverse_intersection * vec4(view_constants.camera_position.xyz, 1.0);

    vec3 frag_model = fragment_position_in_model_space.xyz;
    vec3 ray_direction = frag_model - camera_origin_in_model_space.xyz;
    vec3 ray_origin = camera_origin_in_model_space.xyz;

    float t_closest = -(ray_origin.x * ray_direction.x + ray_origin.y * ray_direction.y) / (ray_direction.x * ray_direction.x + ray_direction.y * ray_direction.y);
    vec3 closest_3d = ray_origin + t_closest * ray_direction;
    if (closest_3d.z < -half_length) t_closest = (-half_length - ray_origin.z) / ray_direction.z;
    if (closest_3d.z >  half_length) t_closest = ( half_length - ray_origin.z) / ray_direction.z;

	// handle clip plane logic for start and end planes
	t_closest = clipPlane(start_clip_plane, ray_origin, ray_direction, frag_model, t_closest);
	t_closest = clipPlane(end_clip_plane,   ray_origin, ray_direction, frag_model, t_closest);

    vec2 closest_point = ray_origin.xy + t_closest * ray_direction.xy;
    float closest_distance = length(closest_point);

    float intensity = pow(1.0 - clamp(closest_distance / beam_radius, 0.0, 1.0), falloff_exponent);

    float outline_distance = beam_radius * (1.0 - pow(outline_intensity_boundary, 1.0 / falloff_exponent));
    float distance_per_pixel = max(length(dFdx(closest_distance)), length(dFdy(closest_distance)));
    float outline_band = 2.0 * distance_per_pixel;

    bool is_outline = closest_distance > outline_distance - 0.5 * distance_per_pixel && closest_distance < outline_distance + outline_band;

    vec4 laser_color;
    if (intensity > outline_intensity_boundary)
    {
        laser_color = vec4(instance_color.rgb + 0.6, 1.0);
        is_outline = false;
    }
    else if (is_outline) laser_color = vec4(instance_color.rgb, intensity / outline_intensity_boundary);
    else laser_color = vec4(instance_color.rgb, intensity / outline_intensity_boundary);

    if (laser_color.a < 0.01) discard;

    uint index = atomicAdd(counter, 1);
    if (index >= fragments.length()) return;

    uint packed = (uint(clamp(laser_color.r, 0.0, 1.0) * 255.0) << 24) |
                  (uint(clamp(laser_color.g, 0.0, 1.0) * 255.0) << 16) |
                  (uint(clamp(laser_color.b, 0.0, 1.0) * 255.0) << 8)  |
                   uint(clamp(laser_color.a, 0.0, 1.0) * 255.0);

    uint old_head = imageAtomicExchange(head_image, ivec2(gl_FragCoord.xy), index);
    fragments[index] = uvec4(packed, floatBitsToUint(gl_FragCoord.z), old_head, uint(is_outline));
}
