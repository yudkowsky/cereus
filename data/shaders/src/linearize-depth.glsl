float linearizeDepth(float raw_depth)
{
    float z_near = 1.0;
    float z_far  = 300.0;
    return (z_near * z_far) / (z_far - raw_depth * (z_far - z_near));
}
