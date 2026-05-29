float computeShadow(sampler2DShadow shadow_map, mat4 light_view_proj, vec3 world_pos, vec3 N, vec3 L)
{
    float offset = 0.05 * (1.0 - max(dot(N, L), 0.0));
    vec4 light_clip = light_view_proj * vec4(world_pos + N * offset, 1.0);
    vec3 ndc = light_clip.xyz / light_clip.w;
    vec2 shadow_uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(shadow_uv, vec2(0.0))) || any(greaterThan(shadow_uv, vec2(1.0)))) return 1.0;
    float ref = ndc.z - 0.0002;
    vec2 texel = 1.0 / vec2(textureSize(shadow_map, 0));
    float sum = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            sum += texture(shadow_map, vec3(shadow_uv + vec2(x, y) * texel, ref));
        }
    }
    return sum / 9.0;
}
