#version 450

layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D depth_texture;

layout(push_constant) uniform PushConstants
{
    vec2 texel_size; // 1.0 / screen resolution
    float depth_threshold;
    float padding;
}
pc;

// function to convert depth to world space, the units of the depth threshold input
float linearize(float raw_depth)
{
    float z_near = 0.1;
    float z_far = 300.0;
    return (z_near * z_far / (z_far - raw_depth * (z_far - z_near)));
}

void main()
{
    float center = linearize(texture(depth_texture, frag_uv).r);
    float up     = linearize(texture(depth_texture, frag_uv + vec2(0.0, pc.texel_size.y)).r);
    float down   = linearize(texture(depth_texture, frag_uv - vec2(0.0, pc.texel_size.y)).r);
    float left   = linearize(texture(depth_texture, frag_uv - vec2(pc.texel_size.x, 0.0)).r);
    float right  = linearize(texture(depth_texture, frag_uv + vec2(pc.texel_size.x, 0.0)).r);

    float max_diff = max(max(abs(center - up), abs(center - down)), max(abs(center - left), abs(center - right)));

    float fov_y = 60.0 * (3.1415926 / 180.0);
    float screen_height = 1080.0 / pc.texel_size.y;

    float pixel_world_size = center * 2.0 * tan(fov_y * 0.5) / screen_height;
    float adaptive_threshold = pc.depth_threshold * pixel_world_size;

    if (max_diff > adaptive_threshold)
    {
        out_color = vec4(0.0, 0.0, 0.0, 1.0);
    }
    else
    {
        discard;
    }
}
