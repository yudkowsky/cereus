#version 450
layout(location = 0) in vec2 in_position;
layout(push_constant) uniform draw_parameters { vec2 translation; };
layout(location = 0) out vec2 texture_coords;

void main()
{
    gl_Position = vec4(in_position + translation, 0.0, 1.0);

    int vertex_id = gl_VertexIndex % 6;

    switch(vertex_id) 
    {
        case 0: // 1st: bottom left
            texture_coords = vec2(0.0, 1.0);
            break;
        case 1: // 1st: bottom right  
            texture_coords = vec2(1.0, 1.0);
            break;
        case 2: // 1st: top right
            texture_coords = vec2(1.0, 0.0);
            break;
        case 3: // 2nd: bottom left
            texture_coords = vec2(0.0, 1.0);
            break;
        case 4: // 2nd: top right
            texture_coords = vec2(1.0, 0.0);
            break;
        case 5: // 2nd: top left
            texture_coords = vec2(0.0, 0.0);
            break;
    }
}