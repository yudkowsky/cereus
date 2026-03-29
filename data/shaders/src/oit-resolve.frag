#version 450

layout(set = 0, binding = 0, r32ui) uniform uimage2D head_image;

layout(set = 1, binding = 0) buffer FragmentPool 
{
    uvec4 fragments[];
};

layout(set = 3, binding = 0) uniform sampler2D scene_depth;

layout(push_constant) uniform PushConstants 
{
    float depth_threshold;
}
pc;

layout(location = 0) out vec4 out_color;

#define MAX_FRAGS 8

const float near = 1.0;
const float far = 300.0;

float linearize(float ndc_z)
{
    return (near * far) / (far - ndc_z * (far - near));
}

bool blackTest(vec4 colors)
{
    return (dot(colors.rgb, vec3(1.0)) < 0.05 && colors.a > 0.5);
}

void main()
{
    // contains index into fragment pool of most recent fragment written at this point
    uint head_of_list = imageLoad(head_image, ivec2(gl_FragCoord.xy)).r;

	// discard fragment if no laser has been written at all
    if (head_of_list == 0xFFFFFFFF) discard;

	// get depth of closest scene geometry. used to reject lasers behind geometry later
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(scene_depth, 0));
    float scene_z = texture(scene_depth, uv).r;

    vec4 colors[MAX_FRAGS];
    float depths[MAX_FRAGS];
    int count = 0;

    uint current_entry_in_list = head_of_list;
    while (current_entry_in_list != 0xFFFFFFFF && count < MAX_FRAGS)
    {
        uvec4 frag = fragments[current_entry_in_list];
        float frag_depth = uintBitsToFloat(frag.y);

        if (frag_depth <= scene_z)
        {
            // unpack 32bit color
            uint packed = frag.x;
            colors[count] = vec4(
                float((packed >> 24) & 0xFFu) / 255.0,
                float((packed >> 16) & 0xFFu) / 255.0,
                float((packed >>  8) & 0xFFu) / 255.0,
                float( packed        & 0xFFu) / 255.0
            );
            depths[count] = linearize(frag_depth);
            count++;
        }

        current_entry_in_list = frag.z;
    }

    if (count == 0) discard;

    // insertion sort by depth
    for (int unsorted_index = 1; unsorted_index < count; unsorted_index++)
    {
        float depth_to_insert = depths[unsorted_index];
        vec4 color_to_insert = colors[unsorted_index];
        int scan = unsorted_index - 1;
        while (scan >= 0 && depths[scan] > depth_to_insert)
        {
            depths[scan + 1] = depths[scan];
            colors[scan + 1] = colors[scan];
            scan--;
        }
        depths[scan + 1] = depth_to_insert;
        colors[scan + 1] = color_to_insert;
    }

    // group by depth threshold, composite front-to-back
    vec3 result = vec3(0.0);
    float alpha_accumulator = 0.0;

    int sorted_index = 0;
    while (sorted_index < count)
    {
        vec3 group_color = colors[sorted_index].rgb * colors[sorted_index].a;
        float group_alpha = colors[sorted_index].a;
        float group_depth = depths[sorted_index];
        bool has_black = blackTest(colors[sorted_index]);

		// absorb colors into one group if they're within depth allowed for the same group
        int scan_for_same_group = sorted_index + 1;
        while (scan_for_same_group < count && (depths[scan_for_same_group] - group_depth) < pc.depth_threshold)
        {
            if (blackTest(colors[scan_for_same_group])) has_black = true;
            group_color += colors[scan_for_same_group].rgb * colors[scan_for_same_group].a;
            group_alpha = max(group_alpha, colors[scan_for_same_group].a);
            scan_for_same_group++;
        }

        if (has_black)
        {
            group_color = vec3(0.0);
            group_alpha = 1.0;
        }
		else
		{
            group_color = min(group_color, vec3(1.0));
        }

        result += (1.0 - alpha_accumulator) * group_alpha * group_color;
        alpha_accumulator += (1.0 - alpha_accumulator) * group_alpha;

        sorted_index = scan_for_same_group;
    }

    out_color = vec4(result, alpha_accumulator);
}
