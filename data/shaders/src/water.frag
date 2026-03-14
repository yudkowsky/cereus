#version 450

layout(location = 0) in vec3 frag_world_pos;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

layout(set = 1, binding = 0) uniform sampler2D scene_color_texture;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 proj;
    float time;
}
pc;

void main() 
{
	vec2 screen_uv = gl_FragCoord.xy / vec2(textureSize(scene_color_texture, 0));
    vec3 scene = texture(scene_color_texture, screen_uv).rgb;

    vec3 water_color = vec3(0.1, 0.3, 0.6);
    float blend = 0.4; // how much water tint vs scene shows through

    out_color = vec4(mix(scene, water_color, blend), 1.0);
}
