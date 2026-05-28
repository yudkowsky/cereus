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

layout(set = 1, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 uv;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PC 
{
    mat4 model;
    vec4 uv_rect;
    vec4 color;
}
pc;

void main()
{
    vec4 color = texture(tex, uv);
    color.a *= pc.color.w;
    out_color = color;
}
