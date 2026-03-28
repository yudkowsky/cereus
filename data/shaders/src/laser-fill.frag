#version 450

layout(location = 0) in vec3 world_pos;

layout(location = 0) out vec4 out_color;

layout(push_constant) uniform PC
{
    mat4 model;
    mat4 inverse_intersection;
    mat4 proj_view;
    vec4 color;
    vec4 start_clip_plane;
    vec4 end_clip_plane;
    vec3 camera_position;
    float half_length;
}
pc;

const float radius = 1.0;

void main()
{
	if (dot(pc.start_clip_plane, vec4(world_pos, 1.0)) < 0.0 || dot(pc.end_clip_plane, vec4(world_pos, 1.0)) < 0.0) discard;

    vec4 fragment_position_in_model_space = pc.inverse_intersection * vec4(world_pos, 1.0);
	vec4 camera_origin_in_model_space = pc.inverse_intersection * vec4(pc.camera_position, 1.0);

    vec3 ray_direction = fragment_position_in_model_space.xyz - camera_origin_in_model_space.xyz; // don't normalize here!
    vec3 ray_origin = camera_origin_in_model_space.xyz;

/*
	float quadratic_a = ray_direction.x * ray_direction.x + ray_direction.y * ray_direction.y;
    float quadratic_b = 2 * (ray_origin.x * ray_direction.x + ray_origin.y * ray_direction.y);
    float quadratic_c = ray_origin.x * ray_origin.x + ray_origin.y * ray_origin.y - radius * radius;

	float discriminant = quadratic_b * quadratic_b - 4 * quadratic_a * quadratic_c;
    if (discriminant <= 0) discard;
	float sqrt_discriminant = sqrt(discriminant);

    float t_entry = (-quadratic_b - sqrt_discriminant) / (2 * quadratic_a);
    float t_exit  = (-quadratic_b + sqrt_discriminant) / (2 * quadratic_a);
*/

	// lasers are always axis aligned, so degenerate to a 2D case where we don't care about the dimension the ray travels in. model has z as travel direction.
	// use minimum value of the quadratic which encodes laser intersection with circle. below is -b / 2a of the quadratic
    float t_closest = -(ray_origin.x * ray_direction.x + ray_origin.y * ray_direction.y) / (ray_direction.x * ray_direction.x + ray_direction.y * ray_direction.y);
    vec3 closest_3d = ray_origin + t_closest * ray_direction;

    // clamp to laser extent in model space to stop the check if going past the range of the mesh - prevents laser from looking 'longer' than it is
    if (closest_3d.z < -pc.half_length) t_closest = (-pc.half_length - ray_origin.z) / ray_direction.z;
    if (closest_3d.z >  pc.half_length) t_closest = ( pc.half_length - ray_origin.z) / ray_direction.z;

    vec2 closest_point = ray_origin.xy + t_closest * ray_direction.xy;
    float closest_distance = length(closest_point);

    float intensity = pow(1.0 - clamp(closest_distance / 0.5, 0.0, 1.0), 4.0);
    //if (intensity > 0.65 && intensity < 0.75) out_color = vec4(vec3(0.0), 1.0);
    if (intensity > 0.65) out_color = vec4(pc.color.rgb + 0.6, 1.0);
    //else out_color = vec4(pc.color.rgb * intensity * 1.0/0.55, 1.0);
    else out_color = vec4(pc.color.rgb, intensity * 1.0/0.55);
}
