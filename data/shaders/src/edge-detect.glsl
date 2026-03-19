bool detectDepthEdge(float center, float up, float down, float left, float right, float focal_length, float threshold)
{
    float laplacian = abs(up + down - 2.0 * center) + abs(left + right - 2.0 * center);
    float normalized = (laplacian / center) * focal_length;
    return normalized > threshold ? true : false;
}

bool detectNormalEdge(vec3 center, vec3 up, vec3 down, vec3 left, vec3 right, float threshold)
{
    float max_diff = max(max(1.0 - dot(center, up), 1.0 - dot(center, down)), max(1.0 - dot(center, left), 1.0 - dot(center, right)));
    return max_diff > threshold ? true : false;
}
