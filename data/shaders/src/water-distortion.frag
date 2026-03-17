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
    float debug_mode;
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
    float water_depth = max(first_depth - water_surface_depth, 0.0);

    float distortion_scale = 0.2 + 0.5 * smoothstep(0.0, 0.5, water_depth);

    vec2 distortion = N.xz * 0.09 * distortion_scale * vec2(pc.proj[0][0], pc.proj[1][1]) / water_surface_depth;
    vec2 refracted_uv = screen_uv + distortion;

    float refracted_depth = linearize_depth(texture(depth_texture, refracted_uv).r);
    if (refracted_depth < water_surface_depth)
    {
        refracted_uv = screen_uv;
    }

    vec3 scene = texture(underwater_texture, refracted_uv).rgb;

    float outline_width = 0.008 * water_surface_depth / pc.proj[1][1];
    if (water_depth < outline_width && water_depth > 0.0)
    {
        if (pc.debug_mode < 0.5)
        {
            out_color = vec4(0.0, 0.0, 0.0, 1.0);
        }
        else
        {
            out_color = vec4(0.0, 1.0, 0.0, 1.0);
        }
        out_water_depth = gl_FragCoord.z;
        return;
    }

    // tint
    if (pc.debug_mode < 0.5)
    {
        float tint_depth = max(linearize_depth(texture(depth_texture, refracted_uv).r) - water_surface_depth, 0.0);

        vec3 absorption = exp(-tint_depth * vec3(0.4, 0.2, 0.1));
        scene *= absorption;

        vec3 shallow_color = vec3(0.02, 0.07, 0.15);
        vec3 deep_color = vec3(0.004, 0.01, 0.025);
        float tint_factor = clamp(tint_depth / 0.65, 0.0, 1.0);
        tint_factor = pow(tint_factor, 0.5);
        vec3 water_tint = mix(shallow_color, deep_color, tint_factor);
        float blend = mix(0.45, 0.9, tint_factor);
        scene = mix(scene, water_tint, blend);

        out_color = vec4(scene, 1.0);
        out_water_depth = gl_FragCoord.z;
    }
    else
    {
        discard;
    }
}
