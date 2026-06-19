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

const float beam_radius = 0.40;
const float core_radius = 0.05;
const float glow_start = 1.2; // glow strength right at core edge
const float falloff_exponent = 0.5; // shape only
const float core_boost = 0.6; // rgb added inside core

void main()
{
    vec3 frag_model    = (inverse_intersection * vec4(world_pos, 1.0)).xyz;
    vec3 ray_origin    = (inverse_intersection * vec4(view_constants.camera_position.xyz, 1.0)).xyz;
    vec3 ray_direction = frag_model - ray_origin;

    float t_closest = -(ray_origin.x * ray_direction.x + ray_origin.y * ray_direction.y) /
                       (ray_direction.x * ray_direction.x + ray_direction.y * ray_direction.y);

    vec3 closest_3d = ray_origin + t_closest * ray_direction;
    if (closest_3d.z < -half_length) t_closest = (-half_length - ray_origin.z) / ray_direction.z;
    if (closest_3d.z >  half_length) t_closest = ( half_length - ray_origin.z) / ray_direction.z;

    t_closest = clipPlane(start_clip_plane, ray_origin, ray_direction, frag_model, t_closest);
    t_closest = clipPlane(end_clip_plane,   ray_origin, ray_direction, frag_model, t_closest);

    float closest_distance = length(ray_origin.xy + t_closest * ray_direction.xy);

    float r = clamp((closest_distance - core_radius) / (beam_radius - core_radius), 0.0, 1.0);
    float glow = glow_start * (1.0 - smoothstep(0.0, 1.0, pow(r, falloff_exponent)));

    float distance_per_pixel = max(length(dFdx(closest_distance)), length(dFdy(closest_distance)));
    bool is_outline = closest_distance > core_radius - 0.5 * distance_per_pixel && closest_distance < core_radius + 2.0 * distance_per_pixel;

    vec4 laser_color;
    if (closest_distance < core_radius)
    {
        laser_color = vec4(instance_color.rgb + core_boost, 1.0);
        is_outline  = false;
    }
    else
    {
        laser_color = vec4(instance_color.rgb, glow);
    }

    if (laser_color.a < 0.01) discard;

    uint index = atomicAdd(counter, 1);
    if (index >= fragments.length()) return;

    uint packed = (uint(clamp(laser_color.r, 0.0, 1.0) * 255.0) << 24) |
                  (uint(clamp(laser_color.g, 0.0, 1.0) * 255.0) << 16) |
                  (uint(clamp(laser_color.b, 0.0, 1.0) * 255.0) <<  8) |
                   uint(clamp(laser_color.a, 0.0, 1.0) * 255.0);

    uint old_head = imageAtomicExchange(head_image, ivec2(gl_FragCoord.xy), index);
    fragments[index] = uvec4(packed, floatBitsToUint(gl_FragCoord.z), old_head, uint(is_outline));
}
