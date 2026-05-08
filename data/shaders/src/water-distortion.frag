#version 450
#include "edge-detect.glsl"
#include "water-height.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D atlas_texture;
layout(set = 1, binding = 0) uniform sampler2D underwater_texture;
layout(set = 2, binding = 0) uniform sampler2D depth_texture;

layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
    float time;
    float debug_mode;
    vec3 cam_pos;
    float depth_threshold;
    float normal_threshold;
} pc;

const float z_near = 1.0;
const float z_far = 300.0;
const vec3 water_tint = vec3(0.00, 0.01, 0.04);

float linearize_depth(float d) {
    return z_near * z_far / (z_far - d * (z_far - z_near));
}

void main() {
    vec3 N = normalize(frag_normal);
    vec2 texture_size = vec2(textureSize(depth_texture, 0));
    vec2 screen_uv = gl_FragCoord.xy / texture_size;

    float water_surface_depth = linearize_depth(gl_FragCoord.z);
    float scene_depth = linearize_depth(texture(depth_texture, screen_uv).r);
    float water_depth = max(scene_depth - water_surface_depth, 0.0);

    // skip distortion at edges of objects to avoid haloing
    float dead_zone = 0.05;
    float effective_depth = max(water_depth - dead_zone, 0.0);
    float distortion_scale = clamp(effective_depth, 0.0, 1.0);
    float focal_length = pc.proj[1][1];
    float depth_scale = focal_length / max(water_surface_depth, 0.1);

    vec2 distortion = N.xz * 0.1 * distortion_scale * depth_scale;
    vec2 refracted_uv = screen_uv + distortion;

    // reject refracted sample if it sampled something above water (would be wrong)
    float refracted_scene_depth = linearize_depth(texture(depth_texture, refracted_uv).r);
    if (refracted_scene_depth < water_surface_depth) {
        refracted_uv = screen_uv;
    }

    vec3 scene = texture(underwater_texture, refracted_uv).rgb;
    float final_water_depth = max(refracted_scene_depth - water_surface_depth, 0.0);
    float tint_factor = mix(0.3, 0.95, clamp(final_water_depth / 0.7, 0.0, 1.0));

    out_color = vec4(mix(scene, water_tint, tint_factor), 1.0);
}
