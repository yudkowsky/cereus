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

layout(push_constant) uniform PC 
{
    mat4 view;
    mat4 projection;
    float water_base_y;
}
pc;

out gl_PerVertex
{
    vec4 gl_Position;
    float gl_ClipDistance[1];
};

void main()
{
    mat4 instance_model = mat4(model_col0, model_col1, model_col2, model_col3);
    vec4 world_pos = instance_model * vec4(position, 1.0);
    gl_Position = pc.projection * pc.view * world_pos;
    uv = instance_uv_rect.xy + input_uv * (instance_uv_rect.zw - instance_uv_rect.xy);
    normal = vec3(instance_model * vec4(input_normal, 0.0));
    if (pc.water_base_y < -100.0)
    {
        gl_ClipDistance[0] = 1.0;
    }
    else
    {
        float water_y = pc.water_base_y;
        gl_ClipDistance[0] = water_y - world_pos.y;
    }
}
