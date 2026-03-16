#version 450

layout(location = 0) in vec2 frag_uv;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D scene_color_texture;    // blitted swapchain
layout(set = 1, binding = 0) uniform sampler2D water_depth_texture;    // R32F: raw water surface depth
layout(set = 2, binding = 0) uniform sampler2D underwater_depth_texture; // underwater geometry depth

layout(push_constant) uniform PushConstants 
{
    vec2 texel_size;
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
    float raw_water_depth = texture(water_depth_texture, frag_uv).r;
    if (raw_water_depth == 0.0) 
    {
        discard;
    }

    float water_surface_depth = linearize_depth(raw_water_depth);
    float scene_depth = linearize_depth(texture(underwater_depth_texture, frag_uv).r);
    float water_depth = max(scene_depth - water_surface_depth, 0.0);

    vec3 scene_color = texture(scene_color_texture, frag_uv).rgb;

    vec3 shallow_color = vec3(0.02, 0.07, 0.15);
    vec3 deep_color = vec3(0.004, 0.01, 0.025);

    float depth_factor = clamp(water_depth / 0.65, 0.0, 1.0);
    depth_factor = pow(depth_factor, 0.5);

    vec3 water_tint = mix(shallow_color, deep_color, depth_factor);
    float blend = mix(0.45, 0.9, depth_factor);

    out_color = vec4(mix(scene_color, water_tint, blend), 1.0);
}
