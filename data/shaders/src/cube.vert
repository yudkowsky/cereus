#version 450

layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal;
// ignore color in location 3
layout(location = 4) in vec4 model_col0;
layout(location = 5) in vec4 model_col1;
layout(location = 6) in vec4 model_col2;
layout(location = 7) in vec4 model_col3;
layout(location = 8) in vec4 instance_uv_rect;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec3 frag_world_pos;

layout(set = 0, binding = 0) uniform ViewConstants 
{
    mat4 view;
    mat4 proj;
    mat4 view_proj;
    mat4 inv_view_proj;
    mat4 light_view_proj;
    vec4 camera_position;
    vec4 light_direction;
    vec4 level_aabb_min;
    float water_plane_y;
    bool discard_below_water_plane;
    float time;
    float water_tile_length;
    float focal_length;
}
view_constants;

void main()
{
    mat4 instance_model = mat4(model_col0, model_col1, model_col2, model_col3);
    vec4 world_pos = instance_model * vec4(position, 1.0);
    gl_Position = view_constants.proj * view_constants.view * world_pos;
    uv = instance_uv_rect.xy + input_uv * (instance_uv_rect.zw - instance_uv_rect.xy);
    normal = vec3(instance_model * vec4(input_normal, 0.0));
    frag_world_pos = world_pos.xyz;
}
