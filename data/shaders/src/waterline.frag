#version 450

#include "linearize-depth.glsl"

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D depth_texture;
layout(set = 1, binding = 0) uniform sampler2D water_depth_texture;

layout(push_constant) uniform PushConstants
{
    float texel_width;
    float texel_height;
    float max_depth_difference;
    float outline_radius_px;
}
pc;

const vec2 offsets[8] = vec2[]
(
    vec2( 1.0,  0.0), vec2(-1.0,  0.0),
    vec2( 0.0,  1.0), vec2( 0.0, -1.0),
    vec2( 0.7071,  0.7071), vec2( 0.7071, -0.7071),
    vec2(-0.7071,  0.7071), vec2(-0.7071, -0.7071)
);

bool isWaterPixel(float water_raw, float scene_raw)
{
    if (water_raw >= 1.0) return false;
    return water_raw < scene_raw;
}

void main()
{
    float center_water_raw = texture(water_depth_texture, frag_uv).r;
    float center_scene_raw = texture(depth_texture, frag_uv).r;
    
    bool center_is_water = isWaterPixel(center_water_raw, center_scene_raw);
    
    float center_water_lin = linearizeDepth(center_water_raw);
    float center_scene_lin = linearizeDepth(center_scene_raw);
    
    float relevant_gap = abs(center_scene_lin - center_water_lin);
    if (center_water_raw >= 1.0 || relevant_gap >= pc.max_depth_difference * 4.0)
    {
        discard;
    }
    
    bool is_outline = false;
    
    for (int i = 0; i < 8; i++)
    {
        vec2 sample_uv = frag_uv + offsets[i] * (pc.outline_radius_px * 0.7) * vec2(pc.texel_width, pc.texel_height);
        
        float neighbor_water_raw = texture(water_depth_texture, sample_uv).r;
        float neighbor_scene_raw = texture(depth_texture, sample_uv).r;
        
        bool neighbor_is_water = isWaterPixel(neighbor_water_raw, neighbor_scene_raw);
        
        if (center_is_water != neighbor_is_water)
        {
            float water_lin = linearizeDepth(neighbor_water_raw);
            float scene_lin = linearizeDepth(neighbor_scene_raw);
            
            if (abs(scene_lin - water_lin) < pc.max_depth_difference ||
                abs(center_scene_lin - center_water_lin) < pc.max_depth_difference)
            {
                is_outline = true;
                break;
            }
        }
    }
    
    if (is_outline)
    {
        out_color = vec4(0.0, 0.0, 0.0, 1.0);
    }
    else
    {
        discard;
    }
}
