#version 450

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;
layout(location = 1) out float out_water_depth;

layout(set = 0, binding = 0) uniform sampler2D atlas_texture;
layout(set = 1, binding = 0) uniform sampler2D underwater_texture;
layout(set = 2, binding = 0) uniform sampler2D depth_texture;

layout(push_constant) uniform PushConstants 
{
    mat4 view;
    mat4 proj;
    float time;
}
pc;

const float z_near = 1.0;
const float z_far = 300.0;

float linearize_depth(float d)
{
    return z_near * z_far / (z_far - d * (z_far - z_near));
}

void main()
{
    vec2 tex_size = vec2(textureSize(underwater_texture, 0));
    vec2 screen_uv = gl_FragCoord.xy / tex_size;
    vec3 N = normalize(frag_normal);

    float first_depth = linearize_depth(texture(depth_texture, screen_uv).r);
    float water_surface_depth = linearize_depth(gl_FragCoord.z);
    float first_water_depth = max(first_depth - water_surface_depth, 0.0);

    float dead_zone = 0.05;
    float effective_depth = max(first_water_depth - dead_zone, 0.0);
    float max_distortion_depth = 3.0;
    float distortion_scale = clamp(effective_depth / max_distortion_depth, 0.0, 1.0);

    vec2 distortion = N.xz * 0.04 * distortion_scale;
    vec2 refracted_uv = screen_uv + distortion;

    float refracted_depth = linearize_depth(texture(depth_texture, refracted_uv).r);
    if (refracted_depth < water_surface_depth)
    {
        refracted_uv = screen_uv;
    }

    vec3 scene = texture(underwater_texture, refracted_uv).rgb;
    out_color = vec4(scene, 1.0);
    out_water_depth = gl_FragCoord.z;
}
