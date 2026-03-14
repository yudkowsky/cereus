#version 450

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D atlas_texture;
layout(set = 1, binding = 0) uniform sampler2D scene_color_texture;
layout(set = 2, binding = 0) uniform sampler2D depth_texture;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    float time;
} pc;

float linearize_depth(float d)
{
    float z_near = 1.0;
    float z_far = 300.0;
    return z_near * z_far / (z_far - d * (z_far - z_near));
}

void main() 
{
    vec2 tex_size = vec2(textureSize(scene_color_texture, 0));
    vec2 screen_uv = gl_FragCoord.xy / tex_size;

    float scene_depth = linearize_depth(texture(depth_texture, screen_uv).r);
    float water_surface_depth = linearize_depth(gl_FragCoord.z);
    float water_depth = max(scene_depth - water_surface_depth, 0.0);

    vec3 N = normalize(frag_normal);
    vec3 N_view = mat3(pc.view) * N;
    float refract_scale = clamp(water_depth / 1.5, 0.05, 1.0);
    vec2 refract_offset = N_view.xy * 0.0 * refract_scale;
    vec2 refracted_uv = clamp(screen_uv + refract_offset, vec2(0.001), vec2(0.999));

    float refracted_scene_depth = linearize_depth(texture(depth_texture, refracted_uv).r);
    if (refracted_scene_depth < water_surface_depth)
    {
        refracted_uv = screen_uv;
    }

    vec3 scene = texture(scene_color_texture, refracted_uv).rgb;

    vec3 shallow_color = vec3(0.02, 0.07, 0.15);
    vec3 deep_color = vec3(0.001, 0.006, 0.018);
    float depth_factor = clamp(water_depth / 1.0, 0.0, 1.0);
    vec3 water_tint = mix(shallow_color, deep_color, depth_factor);
    float blend = mix(0.45, 0.9, depth_factor);

    out_color = vec4(mix(scene, water_tint, blend), 1.0);
}
