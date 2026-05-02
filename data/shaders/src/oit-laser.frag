#version 450

layout(location = 0) in vec3 world_pos;

layout(set = 0, binding = 0, r32ui) uniform coherent uimage2D head_image;
layout(set = 1, binding = 0) buffer FragmentPool 
{
    uvec4 fragments[];
};
layout(set = 2, binding = 0) buffer AtomicCounter 
{
    uint counter;
};

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

float clipPlane(vec4 plane, vec3 ray_origin, vec3 ray_direction, vec3 frag_model, float t_closest)
{
    float cam_dot = dot(plane, vec4(ray_origin, 1.0));
    float denom = dot(plane.xyz, ray_direction);

    if (cam_dot < 0.0)
    {
        if (abs(denom) <= 0.0001) discard;
        float t_plane = -cam_dot / denom;
        if (t_plane <= 0.0) discard;

        float t_frag = dot(frag_model - ray_origin, ray_direction) / dot(ray_direction, ray_direction);
        if (t_frag < t_plane) discard;

        return max(t_closest, t_plane);
    }
    else if (abs(denom) > 0.0001)
    {
        float t_plane = -cam_dot / denom;
        if (t_plane > 0.0) return min(t_closest, t_plane);
    }

    return t_closest;
}

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

	// handle clip plane logic for start and end planes
	t_closest = clipPlane(pc.start_clip_plane, ray_origin, ray_direction, frag_model, t_closest);
	t_closest = clipPlane(pc.end_clip_plane,   ray_origin, ray_direction, frag_model, t_closest);

    vec2 closest_point = ray_origin.xy + t_closest * ray_direction.xy;
    float closest_distance = length(closest_point);

    float beam_radius = 0.4;
    float falloff_exponent = 1.5;
    float intensity = pow(1.0 - clamp(closest_distance / beam_radius, 0.0, 1.0), falloff_exponent);
    float outline_intensity_boundary = 0.8;

    float outline_distance = beam_radius * (1.0 - pow(outline_intensity_boundary, 1.0 / falloff_exponent));
    float distance_per_pixel = max(length(dFdx(closest_distance)), length(dFdy(closest_distance)));
    float outline_band = 2.0 * distance_per_pixel;

    bool is_outline = closest_distance > outline_distance - 0.5 * distance_per_pixel && closest_distance < outline_distance + outline_band;

    vec4 laser_color;
    if (intensity > outline_intensity_boundary)
    {
        laser_color = vec4(pc.color.rgb + 0.6, 1.0);
        is_outline = false;
    }
    else if (is_outline) laser_color = vec4(pc.color.rgb, intensity / outline_intensity_boundary);
    else laser_color = vec4(pc.color.rgb, intensity / outline_intensity_boundary);

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
