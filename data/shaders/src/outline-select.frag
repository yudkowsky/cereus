#version 450

layout(set = 0, binding = 0) uniform ViewConstants 
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;
    mat4 light_view_proj;
    vec4 camera_position;
    vec4 light_direction;
    float water_plane_y;
    float time;
    float water_tile_length;
    float focal_length;
}
view_constants;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PC 
{
    mat4 model;
}
pc;

void main()
{
    out_color = vec4(1.0, 0.0, 1.0, 1.0);
}
