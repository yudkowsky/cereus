#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 vColor;

layout(push_constant) uniform PC {
    mat4 mvp;
} pc;

void main()
{
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    vColor = inColor;
}
