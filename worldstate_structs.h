#pragma once

#include "types.h"

typedef struct NormalizedCoords // [-1, 1]
{
    float x, y;
}
NormalizedCoords;

typedef struct IntCoords
{
    int16 x, y;
}
IntCoords;

typedef struct WorldState 
{
    NormalizedCoords player_coords;
    NormalizedCoords camera_coords;
}
WorldState;

typedef struct TextureToLoad
{
	char* path;
	NormalizedCoords origin[64];
    NormalizedCoords dimensions[64];
    uint32 instance_count;
}
TextureToLoad;

typedef struct TickInput
{
    bool w_press;
	bool a_press;
    bool s_press;
    bool d_press;
    bool i_press;
	bool j_press;
    bool k_press;
    bool l_press;
}
TickInput;
