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

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal; // still declared to match vertex format, but unused

layout(location = 0) out vec2 uv;

layout(push_constant) uniform PC 
{
    mat4 model;
    vec4 uv_rect;
    vec4 color;
}
pc;

void main()
{
    mat4 mvp = view_constants.view_proj * pc.model;
    gl_Position = mvp * vec4(position, 1.0);
    uv = pc.uv_rect.xy + input_uv * (pc.uv_rect.zw - pc.uv_rect.xy);
}
