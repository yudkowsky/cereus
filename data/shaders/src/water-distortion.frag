#version 450
#include "edge-detect.glsl"
#include "water-height.glsl"

layout(location = 0) in vec3 frag_world_pos;
layout(location = 1) in vec3 frag_normal;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D scene_texture;
layout(set = 1, binding = 0) uniform sampler2D depth_texture;
layout(set = 2, binding = 0) uniform sampler2D water_depth_texture;

layout(push_constant) uniform PushConstants 
{
    mat4 view;
    mat4 proj;
    mat4 inv_view_proj;
    float time;
    float focal_length;
    float water_base_y;
}
pc;

const float outline_radius_px = 2.0;
const float max_depth_difference = 0.02; // outline suppressed past this

const vec2 offsets[8] = vec2[]
(
    vec2( 1.0,  0.0), vec2(-1.0,  0.0),
    vec2( 0.0,  1.0), vec2( 0.0, -1.0),
    vec2( 0.7071,  0.7071), vec2( 0.7071, -0.7071),
    vec2(-0.7071,  0.7071), vec2(-0.7071, -0.7071)
);

void main() 
{
    vec2 texel = 1.0 / vec2(textureSize(depth_texture, 0));
    vec2 screen_uv = gl_FragCoord.xy * texel;

    float center_water_lin = linearizeDepth(gl_FragCoord.z);

    bool this_is_outline = false;

    for (int i = 0; i < 8; ++i) 
    {
        vec2 sample_uv = screen_uv + offsets[i] * outline_radius_px * texel;

        float scene_raw_depth = texture(depth_texture, sample_uv).r;
        float water_raw_depth = texture(water_depth_texture, sample_uv).r;

        if (scene_raw_depth >= 1.0) continue;
        if (water_raw_depth >= 1.0) continue;

        float scene_linear_depth = linearizeDepth(scene_raw_depth);
        float water_linear_depth = linearizeDepth(water_raw_depth);

        if (scene_linear_depth < water_linear_depth && water_linear_depth - scene_linear_depth < max_depth_difference)
        {
            this_is_outline = true;
            break;
        }
    }

    out_color = vec4(this_is_outline ? vec3(1.0, 0.0, 1.0) : vec3(1.0), 1.0);
}
