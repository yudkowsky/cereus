#version 450

layout(set = 0, binding = 0) uniform ViewConstants 
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;
    mat4 light_view_proj;
    vec4 camera_position;
    float water_plane_y;
    float time;
    float water_tile_length;
    float focal_length;
}
view_constants;

layout(location = 0) in vec3 position;

layout(push_constant) uniform PC 
{
    mat4 model;
}
pc;

void main()
{
    gl_Position = view_constants.view_proj * pc.model * vec4(position, 1.0);
}
