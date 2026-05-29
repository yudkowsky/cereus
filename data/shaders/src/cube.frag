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
    float water_plane_y;
    float time;
    float water_tile_length;
    float focal_length;
}
view_constants;

layout(set = 1, binding = 0) uniform sampler2D input_texture;
layout(set = 2, binding = 0) uniform sampler2D water_texture;
layout(set = 3, binding = 0) uniform sampler2DShadow shadow_map;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

void main()
{
    vec2 displacement_uv = frag_world_pos.xz / view_constants.water_tile_length;
    float wave_displacement = texture(water_texture, displacement_uv).w;
    float water_surface_y = view_constants.water_plane_y - wave_displacement;
    if (frag_world_pos.y < water_surface_y - 0.01) discard;

    vec4 tex = texture(input_texture, uv);

    float shadow = computeShadow(shadow_map, view_constants.light_view_proj, frag_world_pos, normalize(normal), normalize(view_constants.light_direction.xyz));
    float light = mix(0.2, 1.0, shadow);

    out_color = vec4(tex.rgb * light, tex.a);
    out_normal = vec4(normalize(normal), 0.0);
}
