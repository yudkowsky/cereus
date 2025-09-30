#pragma once

#include "types.h"

typedef struct NormalizedCoords
{
    float x, y;
}
NormalizedCoords;

typedef struct IntCoords
{
    int16 x, y;
}
IntCoords;

typedef struct Rect
{
    NormalizedCoords origin;
    NormalizedCoords dimensions;
}
Rect;

typedef struct Entity
{
	NormalizedCoords origin;
	int16 id;
}
Entity;

typedef struct WorldState 
{
    NormalizedCoords player_coords;
    NormalizedCoords player_velocity;
    NormalizedCoords camera_coords;

    int8 w_time_until_allowed;
    int8 a_time_until_allowed;
    int8 s_time_until_allowed;
    int8 d_time_until_allowed;

    Entity walls[64];
    int16 wall_count;

    Entity boxes[64];
    int16 box_count;
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
    bool z_press;

    bool i_press;
	bool j_press;
    bool k_press;
    bool l_press;
}
TickInput;
