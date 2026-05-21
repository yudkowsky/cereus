float clipPlane(vec4 plane, vec3 ray_origin, vec3 ray_direction, vec3 frag_model, float t_closest)
{
    float cam_dot = dot(plane, vec4(ray_origin, 1.0));
    float denom = dot(plane.xyz, ray_direction);

    if (cam_dot < 0.0)
    {
        if (abs(denom) <= 0.0001) discard;
        float t_plane = -cam_dot / denom;
        if (t_plane <= 0.0) discard;

        float t_frag = dot(frag_model - ray_origin, ray_direction) / dot(ray_direction, ray_direction);
        if (t_frag < t_plane) discard;

        return max(t_closest, t_plane);
    }
    else if (abs(denom) > 0.0001)
    {
        float t_plane = -cam_dot / denom;
        if (t_plane > 0.0) return min(t_closest, t_plane);
    }

    return t_closest;
}
