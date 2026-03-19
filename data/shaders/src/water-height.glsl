float waterHeight(vec3 world_pos, float time)
{
    float wave_scale = 0.8;
    return wave_scale * (sin(world_pos.x * 3.0 + time * 1.5) * 0.05) + (sin(world_pos.z * 2.0 + time * 1.0) * 0.03);
}

// forward difference using waterHeight
vec3 waterNormal(vec3 world_pos, float time)
{
    float epsilon = 0.001;
    float height = waterHeight(world_pos, time);
    float height_at_x = waterHeight(world_pos + vec3(epsilon, 0.0, 0.0), time);
    float height_at_z = waterHeight(world_pos + vec3(0.0, 0.0, epsilon), time);
    float dhdx = (height_at_x - height) / epsilon;
    float dhdz = (height_at_z - height) / epsilon;
    return normalize(vec3(-dhdx, 1.0, -dhdz));
}
