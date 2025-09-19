#pragma once

typedef struct Vertex 
{
    float pos[2];
    float color[3];
} 
Vertex;

typedef struct Triangle
{
	Vertex point1;
    Vertex point2;
    Vertex point3;
}
Triangle;

typedef struct WorldState 
{
   Triangle triangle; 
}
WorldState;

typedef struct TickInput
{
    bool w_press;
	bool a_press;
    bool s_press;
    bool d_press;
}
TickInput;
