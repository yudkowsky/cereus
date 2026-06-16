#version 450

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

// per vertex
layout(location = 0) in vec3 in_position;

// per instance
layout(location = 4) in vec4 in_center_length;
layout(location = 5) in vec4 in_rotation;
layout(location = 6) in vec4 in_color;
layout(location = 7) in vec4 in_start_clip_world;
layout(location = 8) in vec4 in_end_clip_world;

layout(location = 0)      out vec3 world_pos;
layout(location = 1) flat out mat4 inverse_intersection;
layout(location = 5) flat out vec4 start_clip_plane;
layout(location = 6) flat out vec4 end_clip_plane;
layout(location = 7) flat out vec4 instance_color;
layout(location = 8) flat out float half_length;

void main()
{
    // TODO: naming, cleanup
    vec3  center   = in_center_length.xyz;
    float length_z = in_center_length.w;
    const float width = 2.0;

    vec4 q = in_rotation;
    mat3 R = mat3(
        vec3(1.0 - 2.0*(q.y*q.y + q.z*q.z),  2.0*(q.x*q.y + q.w*q.z),       2.0*(q.x*q.z - q.w*q.y)),
        vec3(2.0*(q.x*q.y - q.w*q.z),        1.0 - 2.0*(q.x*q.x + q.z*q.z), 2.0*(q.y*q.z + q.w*q.x)),
        vec3(2.0*(q.x*q.z + q.w*q.y),        2.0*(q.y*q.z - q.w*q.x),       1.0 - 2.0*(q.x*q.x + q.y*q.y))
    );

    mat3 RT = transpose(R);

    mat4 model = mat4(
        vec4(R[0] * width, 0.0),
        vec4(R[1] * width, 0.0),
        vec4(R[2] * length_z, 0.0),
        vec4(center, 1.0)
    );

    mat4 intersection = mat4(
        vec4(R[0], 0.0),
        vec4(R[1], 0.0),
        vec4(R[2], 0.0),
        vec4(center, 1.0)
    );
    inverse_intersection = mat4(
        vec4(RT[0], 0.0),
        vec4(RT[1], 0.0),
        vec4(RT[2], 0.0),
        vec4(-RT * center, 1.0)
    );

    start_clip_plane = transpose(intersection) * in_start_clip_world;
    end_clip_plane = transpose(intersection) * in_end_clip_world;

    instance_color = in_color;
    half_length = length_z * 0.5;

    vec4 world = model * vec4(in_position, 1.0);
    world_pos = world.xyz;
    gl_Position = view_constants.view_proj * world;
}
