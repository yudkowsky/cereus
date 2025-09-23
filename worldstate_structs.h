#pragma once

#include "types.h"

typedef struct NormalizedCoords // [-1, 1]
{
    double x, y;
}
NormalizedCoords;

typedef struct IntCoords
{
    int16 x, y;
}
IntCoords;

typedef struct WorldState 
{
    int pixel_size;

    IntCoords player_position;
}
WorldState;

typedef struct TextureToLoad
{
	char* path;
	NormalizedCoords origin[64];
    NormalizedCoords scale[64];
    uint32 instance_count;
}
TextureToLoad;

typedef struct TickInput
{
    bool w_press;
	bool a_press;
    bool s_press;
    bool d_press;
}
TickInput;
