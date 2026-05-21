#version 450

#include "laser-clip-plane.glsl"

layout(location = 0) in vec3 world_pos;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PC
{
    mat4 model;
    mat4 inverse_intersection;
    mat4 proj_view;
    vec4 color;
    vec4 start_clip_plane;
    vec4 end_clip_plane;
    vec3 camera_position;
    float half_length;
}
pc;

// TODO: a lot of duplicated code here, do i want them to be the same? possibly not, if reflection wants to look a bit different
void main()
{
    vec4 fragment_position_in_model_space = pc.inverse_intersection * vec4(world_pos, 1.0);
    vec4 camera_origin_in_model_space = pc.inverse_intersection * vec4(pc.camera_position, 1.0);
    vec3 frag_model = fragment_position_in_model_space.xyz;
    vec3 ray_direction = frag_model - camera_origin_in_model_space.xyz;
    vec3 ray_origin = camera_origin_in_model_space.xyz;

    float t_closest = -(ray_origin.x * ray_direction.x + ray_origin.y * ray_direction.y) / (ray_direction.x * ray_direction.x + ray_direction.y * ray_direction.y);
    vec3 closest_3d = ray_origin + t_closest * ray_direction;
    if (closest_3d.z < -pc.half_length) t_closest = (-pc.half_length - ray_origin.z) / ray_direction.z;
    if (closest_3d.z >  pc.half_length) t_closest = ( pc.half_length - ray_origin.z) / ray_direction.z;

    t_closest = clipPlane(pc.start_clip_plane, ray_origin, ray_direction, frag_model, t_closest);
    t_closest = clipPlane(pc.end_clip_plane,   ray_origin, ray_direction, frag_model, t_closest);

    vec2 closest_point = ray_origin.xy + t_closest * ray_direction.xy;
    float closest_distance = length(closest_point);

    float beam_radius = 0.4;
    float falloff_exponent = 1.5;
    float intensity = pow(1.0 - clamp(closest_distance / beam_radius, 0.0, 1.0), falloff_exponent);

    float outline_intensity_boundary = 0.8;

    vec4 laser_color;
    if (intensity > outline_intensity_boundary)
    {
        laser_color = vec4(pc.color.rgb + 0.6, 1.0);
    }
    else
    {
        laser_color = vec4(pc.color.rgb, intensity / outline_intensity_boundary);
    }

    if (laser_color.a < 0.01) discard;

    out_color = laser_color;
}
