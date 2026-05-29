#version 450
//#include "shadow.glsl"

layout(set = 0, binding = 0) uniform ViewConstants 
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;
    mat4 light_view_proj;
    vec4 camera_position;
    float water_plane_y;
    float time;
    float water_tile_length;
    float focal_length;
}
view_constants;

layout(set = 1, binding = 0) uniform sampler2D input_texture;
layout(set = 2, binding = 0) uniform sampler2D water_texture;
layout(set = 3, binding = 0) uniform sampler2D shadow_map;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

float computeShadow(vec3 world_pos)
{
    vec4 light_clip = view_constants.light_view_proj * vec4(world_pos, 1.0);
    vec3 ndc = light_clip.xyz / light_clip.w;
    vec2 shadow_uv = ndc.xy * 0.5 + 0.5;
    if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 || shadow_uv.y < 0.0 || shadow_uv.y > 1.0) return 1.0;
    float current_depth = ndc.z;
    float closest_depth = texture(shadow_map, shadow_uv).r;
    float bias = 0.0002;
    return (current_depth - bias > closest_depth) ? 0.0 : 1.0;
}

void main()
{
    vec2 displacement_uv = frag_world_pos.xz / view_constants.water_tile_length;
    float wave_displacement = texture(water_texture, displacement_uv).w;
    float water_surface_y = view_constants.water_plane_y - wave_displacement;
    if (frag_world_pos.y < water_surface_y - 0.01) discard;

    vec4 tex = texture(input_texture, uv);

    float shadow = computeShadow(frag_world_pos);
    float light = mix(0.2, 1.0, shadow);

    out_color = vec4(tex.rgb * light, tex.a);
    out_normal = vec4(normalize(normal), 0.0);
}
