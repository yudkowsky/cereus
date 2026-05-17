#version 450

layout(location = 0) out float out_water_depth;

void main()
{
    out_water_depth = gl_FragCoord.z;
}
