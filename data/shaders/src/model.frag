#version 450

layout(location = 0) in vec3 normal;
layout(location = 1) in vec3 color;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

void main()
{
    float low = 20.0 / 255.0;
    float high = 200.0 / 255.0;

    vec3 normal = normalize(normal);
    vec3 light_direction = normalize(vec3(0.3, 1.0, 0.5));
    float lighting = dot(normal, light_direction) * 0.5 + 0.5;

    out_color = vec4(color * vec3(mix(low, high, lighting)), 1.0);

    out_normal = vec4(normal, 0.0);
}
