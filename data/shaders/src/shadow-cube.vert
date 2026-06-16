#version 450

layout(location = 0) in vec3 in_position;

layout(location = 4) in vec4 instance_model_column_0;
layout(location = 5) in vec4 instance_model_column_1;
layout(location = 6) in vec4 instance_model_column_2;
layout(location = 7) in vec4 instance_model_column_3;

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
    mat4 model = mat4(instance_model_column_0, instance_model_column_1, instance_model_column_2, instance_model_column_3);
    gl_Position = view_constants.light_view_proj * model * vec4(in_position, 1.0);
}
