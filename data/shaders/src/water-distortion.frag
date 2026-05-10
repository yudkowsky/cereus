#version 450

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D scene_texture;
layout(set = 1, binding = 0) uniform sampler2D depth_texture;

layout(push_constant) uniform PushConstants 
{
    mat4 view;
    mat4 proj;
    float time;
}
pc;

const float z_near = 1.0;
const float z_far = 300.0;
const float max_tint_depth = 1.0;
const vec3 water_tint = vec3(0.00, 0.01, 0.04);

float linearize_depth(float d) 
{
    return z_near * z_far / (z_far - d * (z_far - z_near));
}

void main() 
{
    vec2 screen_uv = gl_FragCoord.xy / vec2(textureSize(scene_texture, 0));
    vec3 scene = texture(scene_texture, screen_uv).rgb;

    // compute depth from water surface to geometry
    float water_surface_depth = linearize_depth(gl_FragCoord.z);
    float scene_depth = linearize_depth(texture(depth_texture, screen_uv).r);
    float underwater_distance = max(scene_depth - water_surface_depth, 0.0);

    float tint_amount = clamp(underwater_distance / max_tint_depth, 0.0, 0.8);

    out_color = vec4(mix(scene, water_tint, tint_amount), 1.0);
}
