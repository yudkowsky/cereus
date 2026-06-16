#version 450

#include "shadow.glsl"

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

layout(set = 1, binding = 0) uniform sampler2D water_texture;
layout(set = 2, binding = 0) uniform sampler2DShadow shadow_map;

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    vec4 tint;
} 
pc;

void main()
{
    if (view_constants.discard_below_water_plane)
    {
        vec2 displacement_uv = frag_world_pos.xz / view_constants.water_tile_length;
        float wave_displacement = texture(water_texture, displacement_uv).w;
        float water_surface_y = view_constants.water_plane_y - wave_displacement;
        if (frag_world_pos.y < water_surface_y - 0.1) discard;
    }

    vec3 N = normalize(normal);
    vec3 L = normalize(-view_constants.light_direction.xyz);

    float n_dot_l  = max(dot(N, L), 0.0);
    float shadow   = computeShadow(shadow_map, view_constants.light_view_proj, frag_world_pos, N, L);

    float direct   = n_dot_l * shadow;
    float lighting = mix(0.2, 1.0, direct);

    float tint_amount = 0.3;
    out_color  = vec4(color * lighting + (pc.tint.xyz * tint_amount), 1.0);
    out_normal = vec4(N, 0.0);
}
