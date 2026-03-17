float waterHeight(vec3 world_pos, float time)
{
    float wave_scale = 0.8;
    return wave_scale * (sin(world_pos.x * 3.0 + time * 1.5) * 0.05) + (sin(world_pos.z * 2.0 + time * 1.0) * 0.03);
}

vec3 waterNormal(vec3 world_pos, float time)
{
    float wave_scale = 0.8;
    float dhdx = wave_scale * cos(world_pos.x * 3.0 + time * 1.5) * 0.05 * 3.0;
    float dhdz = wave_scale * cos(world_pos.z * 2.0 + time * 1.0) * 0.03 * 2.0;
    return normalize(vec3(-dhdx, 1.0, -dhdz));
}
