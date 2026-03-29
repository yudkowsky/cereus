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

void main()
{
    if (dot(pc.start_clip_plane, vec4(world_pos, 1.0)) < 0.0 || dot(pc.end_clip_plane, vec4(world_pos, 1.0)) < 0.0) discard;

    vec4 fragment_position_in_model_space = pc.inverse_intersection * vec4(world_pos, 1.0);
    vec4 camera_origin_in_model_space = pc.inverse_intersection * vec4(pc.camera_position, 1.0);
    vec3 ray_direction = fragment_position_in_model_space.xyz - camera_origin_in_model_space.xyz;
    vec3 ray_origin = camera_origin_in_model_space.xyz;

	// -b/2a of quadratic in terms of t to find minimum distance
    float t_closest = -(ray_origin.x * ray_direction.x + ray_origin.y * ray_direction.y) / (ray_direction.x * ray_direction.x + ray_direction.y * ray_direction.y);
    vec3 closest_3d = ray_origin + t_closest * ray_direction;
    if (closest_3d.z < -pc.half_length) t_closest = (-pc.half_length - ray_origin.z) / ray_direction.z;
    if (closest_3d.z >  pc.half_length) t_closest = ( pc.half_length - ray_origin.z) / ray_direction.z;
    vec2 closest_point = ray_origin.xy + t_closest * ray_direction.xy;
    float closest_distance = length(closest_point);

    float intensity = pow(1.0 - clamp(closest_distance / 0.4, 0.0, 1.0), 1.5);
    vec4 laser_color;
    if (intensity > 0.80) laser_color = vec4(pc.color.rgb + 0.6, 1.0);
    else laser_color = vec4(pc.color.rgb, intensity * 1.0 / 0.8);

    if (laser_color.a < 0.01) discard;

    uint index = atomicAdd(counter, 1);
    if (index >= fragments.length()) return;

    uint packed = (uint(clamp(laser_color.r, 0.0, 1.0) * 255.0) << 24) |
                  (uint(clamp(laser_color.g, 0.0, 1.0) * 255.0) << 16) |
                  (uint(clamp(laser_color.b, 0.0, 1.0) * 255.0) << 8)  |
                   uint(clamp(laser_color.a, 0.0, 1.0) * 255.0);

    uint old_head = imageAtomicExchange(head_image, ivec2(gl_FragCoord.xy), index);

    fragments[index] = uvec4(packed, floatBitsToUint(gl_FragCoord.z), old_head, 0);
}
