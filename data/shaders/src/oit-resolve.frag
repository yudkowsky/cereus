#version 450

layout(set = 0, binding = 0, r32ui) uniform uimage2D head_image;

layout(set = 1, binding = 0) buffer FragmentPool 
{
    uvec4 fragments[];
};

layout(location = 0) out vec4 out_color;

void main()
{
    uint head = imageLoad(head_image, ivec2(gl_FragCoord.xy)).r;

    if (head == 0xFFFFFFFF) discard;

    int count = 0;
    uint current = head;
    while (current != 0xFFFFFFFF && count < 8)
    {
        current = fragments[current].z; // next pointer
        count++;
    }

    if (count == 1) out_color = vec4(1.0, 0.0, 0.0, 1.0);      // red: single fragment
    else if (count == 2) out_color = vec4(0.0, 1.0, 0.0, 1.0);  // green: two fragments
    else if (count == 3) out_color = vec4(0.0, 0.0, 1.0, 1.0);  // blue: three
    else out_color = vec4(1.0, 1.0, 1.0, 1.0);                  // white: four or more
}
