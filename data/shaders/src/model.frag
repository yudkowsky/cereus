#version 450

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 color;
layout(location = 2) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 uv_rect;
    float alpha;
    float water_plane_y;
    float time;
    float tile_length;
} 
pc;

void main()
{
    // discard if relevant
    if (frag_world_pos.y < pc.water_plane_y) discard;

    // basic shader
    vec3 N = normalize(normal);
    vec3 light_direction = normalize(vec3(0.3, 1.0, 0.5));
    float lighting = dot(N, light_direction) * 0.5 + 0.5;
    out_color = vec4(color * lighting, 1.0);
    out_normal = vec4(N, 0.0);
}
