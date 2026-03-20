/*
float waterHeight(vec3 world_pos, float time)
{
    float x_frequency = 3.0;
    float y_frequency = 2.0;
    float x_speed = 1.5;
    float y_speed = 1.0;
    float x_amplitude = 0.04;
    float y_amplitude = 0.03;

    return (sin(world_pos.x * x_frequency + time * x_speed) * x_amplitude) + (sin(world_pos.z * y_frequency + time * y_speed) * y_amplitude);
}
*/

float waterHeight(vec3 world_pos, float time)
{
    float height = 0.0;
    
    // random stuff
    height += 0.040 * sin(dot(vec2(1.0, 0.0),  world_pos.xz) * 3.0 + time * 1.5);
    height += 0.030 * sin(dot(vec2(0.0, 1.0),  world_pos.xz) * 2.0 + time * 1.0);
    height += 0.015 * sin(dot(vec2(0.7, 0.7),  world_pos.xz) * 5.0 + time * 2.0);
    height += 0.010 * sin(dot(vec2(-0.5, 0.8), world_pos.xz) * 8.0 + time * 3.0);
    height += 0.005 * sin(dot(vec2(0.9, -0.4), world_pos.xz) * 13.0 + time * 4.0);
    height += 0.003 * sin(dot(vec2(-0.3, -0.9),world_pos.xz) * 20.0 + time * 5.0);
    
    return height;
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
