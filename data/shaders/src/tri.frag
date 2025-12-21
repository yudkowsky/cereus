#version 450

layout(set=0, binding=0) uniform sampler2D input_texture; 
layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PC {
    mat4 model;
    mat4 view;
    mat4 projection;
    mat4 uv_rect;
} pc;

const vec3 light_direction = vec3(0.3, 1, 0.5);

void main()
{
    vec4 tex = texture(input_texture, uv);
    float light = max(dot(normalize(normal), normalize(light_direction)), 0.2);

    out_color = vec4(tex.rgb * light, tex.a);
}
