#version 450

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 color;

layout(location = 0) out vec4 out_color;

void main()
{
    out_color = vec4(0.0, 0.0, 0.0, 1.0);
}
