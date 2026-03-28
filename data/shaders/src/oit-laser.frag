#version 450

layout(set = 0, binding = 0, r32ui) uniform coherent uimage2D head_image;

layout(set = 1, binding = 0) buffer FragmentPool 
{
    uvec4 fragments[];
};

layout(set = 2, binding = 0) buffer AtomicCounter 
{
    uint counter;
};

void main()
{
    uint index = atomicAdd(counter, 1);
    if (index >= fragments.length()) return;

    // hardcoded red, full opacity
    uint packed = (255u << 24) | (0u << 16) | (0u << 8) | 255u;

    uint old_head = imageAtomicExchange(head_image, ivec2(gl_FragCoord.xy), index);

    fragments[index] = uvec4(packed, floatBitsToUint(gl_FragCoord.z), old_head, 0);
}
