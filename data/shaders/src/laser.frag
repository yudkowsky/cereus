#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 frag_pos_model;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PC
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec4 color;
}
pc;

void main()
{
    float radial = length(frag_pos_model.xy) * 2.0;

    // flat interior fill
	float fill = 0.15;

    // bright edge
    float edge = smoothstep(0.7, 1.0, radial) * 0.8;

    float intensity = fill + edge;
    out_color = vec4(pc.color.rgb * intensity, intensity);
}
