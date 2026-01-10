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
    float dist_from_axis = length(frag_pos_model.xy); // treating z as length, which already happens in scale code, i think... if there is a bug that applies only to diagonal / non-diagonal lasers, check this line.
	
    float radius = 1.0;
    float alpha = 0.5;
    /*
    float falloff = 1.0 - smoothstep(0.0, radius, dist_from_axis);
	float alpha = falloff * 0.6;
	float core = 1.0 - smoothstep(0.0, radius * 0.3, dist_from_axis);
    alpha += core * 0.4;
    */

    vec3 laser_color = pc.color.xyz;
    out_color = vec4(laser_color * alpha, alpha);
}
