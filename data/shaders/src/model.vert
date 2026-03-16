#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 input_uv;
layout(location = 2) in vec3 input_normal;
layout(location = 3) in vec3 input_color;

layout(location = 0) out vec3 normal;
layout(location = 1) out vec3 color;

layout(push_constant) uniform PushConstants
{
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 uv_rect;
    float alpha;
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
    vec4 world_pos = pc.model * vec4(position, 1.0);
    gl_Position = pc.proj * pc.view * world_pos;
    normal = mat3(pc.model) * input_normal;
    color = input_color;
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
