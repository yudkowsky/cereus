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

bool whiteTest(vec4 c)
{
    return (dot(c.rgb, vec3(1.0)) > 1.5 && c.a > 0.5);
}

void main()
{
    uint head_of_list = imageLoad(head_image, ivec2(gl_FragCoord.xy)).r;
    if (head_of_list == 0xFFFFFFFF) discard;

    vec2 uv = gl_FragCoord.xy / vec2(textureSize(scene_depth, 0));
    float scene_z = texture(scene_depth, uv).r;

    vec4 colors[MAX_FRAGS];
    float depths[MAX_FRAGS];
    uint flags[MAX_FRAGS];
    int count = 0;

    uint current_entry_in_list = head_of_list;
    while (current_entry_in_list != 0xFFFFFFFF && count < MAX_FRAGS)
    {
        uvec4 frag = fragments[current_entry_in_list];
        float frag_depth = uintBitsToFloat(frag.y);

        if (frag_depth <= scene_z)
        {
            uint packed = frag.x;
            colors[count] = vec4(
                float((packed >> 24) & 0xFFu) / 255.0,
                float((packed >> 16) & 0xFFu) / 255.0,
                float((packed >>  8) & 0xFFu) / 255.0,
                float( packed        & 0xFFu) / 255.0
            );
            depths[count] = linearize(frag_depth);
            flags[count] = frag.w;
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
        uint flag_to_insert = flags[unsorted_index];
        int scan = unsorted_index - 1;
        while (scan >= 0 && depths[scan] > depth_to_insert)
        {
            depths[scan + 1] = depths[scan];
            colors[scan + 1] = colors[scan];
            flags[scan + 1] = flags[scan];
            scan--;
        }
        depths[scan + 1] = depth_to_insert;
        colors[scan + 1] = color_to_insert;
        flags[scan + 1] = flag_to_insert;
    }

    // group by depth threshold, composite front-to-back
    vec3 result = vec3(0.0);
    float alpha_accumulator = 0.0;

    int sorted_index = 0;
    while (sorted_index < count)
    {
        float group_depth = depths[sorted_index];
        bool has_outline = false;
        bool has_white = false;

        // first pass: scan group bounds and check for outline / white
        int scan_for_same_group = sorted_index;
        while (scan_for_same_group < count && (scan_for_same_group == sorted_index || (depths[scan_for_same_group] - group_depth) < pc.depth_threshold))
        {
            if (flags[scan_for_same_group] == 1) has_outline = true;
            if (whiteTest(colors[scan_for_same_group])) has_white = true;
            scan_for_same_group++;
        }

        bool outline_wins = has_outline && !has_white;

        // second pass: build group color
        vec3 group_color = vec3(0.0);
        float group_alpha = 0.0;

        if (outline_wins)
        {
            group_color = vec3(0.0);
            group_alpha = 1.0;
        }
        else
        {
            int group_scan = sorted_index;
            while (group_scan < scan_for_same_group)
            {
                vec4 c = colors[group_scan];
                group_color = max(group_color, c.rgb * c.a);
                group_alpha = max(group_alpha, c.a);
                group_scan++;
            }
            group_color = min(group_color, vec3(1.0));
        }

        result += (1.0 - alpha_accumulator) * group_alpha * group_color;
        alpha_accumulator += (1.0 - alpha_accumulator) * group_alpha;

        sorted_index = scan_for_same_group;
    }

    out_color = vec4(result, alpha_accumulator);
}


