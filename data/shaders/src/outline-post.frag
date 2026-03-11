#version 450
layout(location = 0) in vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D depth_texture;
layout(set = 1, binding = 0) uniform sampler2D normal_texture;

layout(push_constant) uniform PushConstants
{
    vec2 texel_size;
    float depth_threshold;
    float normal_threshold;
}
pc;

float linearize(float raw_depth)
{
    float z_near = 0.1;
    float z_far = 300.0;
    return (z_near * z_far) / (z_far - raw_depth * (z_far - z_near));
}

void main()
{
    vec2 step = pc.texel_size;

    vec3 n_center = texture(normal_texture, frag_uv).rgb;

    if (dot(n_center, n_center) < 0.01) discard; // doesnt have outline fully at the clear color

    vec3 n_up     = texture(normal_texture, frag_uv + vec2(0.0, step.y)).rgb;
    vec3 n_down   = texture(normal_texture, frag_uv - vec2(0.0, step.y)).rgb;
    vec3 n_left   = texture(normal_texture, frag_uv - vec2(step.x, 0.0)).rgb;
    vec3 n_right  = texture(normal_texture, frag_uv + vec2(step.x, 0.0)).rgb;

    float max_normal_diff = max(
        max(1.0 - dot(n_center, n_up),   1.0 - dot(n_center, n_down)),
        max(1.0 - dot(n_center, n_left), 1.0 - dot(n_center, n_right))
    );

    if (max_normal_diff > pc.normal_threshold)
    {
        out_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // depth discontinuity check using second derivative
    float center = linearize(texture(depth_texture, frag_uv).r);
    float up     = linearize(texture(depth_texture, frag_uv + vec2(0.0, step.y)).r);
    float down   = linearize(texture(depth_texture, frag_uv - vec2(0.0, step.y)).r);
    float left   = linearize(texture(depth_texture, frag_uv - vec2(step.x, 0.0)).r);
    float right  = linearize(texture(depth_texture, frag_uv + vec2(step.x, 0.0)).r);

    // second derivative: on a smooth surface (even at grazing angles) this is near zero
    // at a real edge between two objects, the gradient suddenly changes, so this is large
    float laplacian = abs(up + down - 2.0 * center) + abs(left + right - 2.0 * center);

    // normalize by depth so it's distance-independent
    float relative_laplacian = laplacian / (center * center);

    if (relative_laplacian > pc.depth_threshold)
    {
        out_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    discard;
}
