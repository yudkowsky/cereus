#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_normal;
layout(location = 3) in vec3 in_color;
layout(location = 4) in mat4 instance_model;

layout(set = 4, binding = 0) uniform sampler2D displacement_texture;

layout(location = 0) out vec3 frag_world_pos;
layout(location = 1) out vec3 frag_normal;

layout(push_constant) uniform PushConstants
{
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    float time;
    float focal_length;
    float water_base_y;
    float tile_length;
}
pc;

void main()
{
    vec4 grid_world = instance_model * vec4(in_position, 1.0);
    vec2 grid_xz = grid_world.xz;

    // get water height from texture
    vec2 fft_uv = grid_xz / pc.tile_length;
    float height = texture(displacement_texture, fft_uv).r;
    vec4 world_pos = grid_world + vec4(0.0, height, 0.0, 0.0);
    frag_world_pos = world_pos.xyz;

    // compute normal via forward difference
    float epsilon = 0.1;
    float height_at_x = texture(displacement_texture, (grid_xz + vec2(epsilon, 0.0)) / pc.tile_length).r;
    float height_at_z = texture(displacement_texture, (grid_xz + vec2(0.0, epsilon)) / pc.tile_length).r;
    float dhdx = (height_at_x - height) / epsilon;
    float dhdz = (height_at_z - height) / epsilon;
    frag_normal = normalize(vec3(-dhdx, 1.0, -dhdz));

    gl_Position = pc.proj * pc.view * world_pos;
}
