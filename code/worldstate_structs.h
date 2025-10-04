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

typedef enum
{
	NORTH = 0,
    WEST  = 1,
    SOUTH = 2,
    EAST  = 3,
}
Direction;

typedef struct WorldState 
{
	char* level_path;

    NormalizedCoords player_coords;
	Direction player_direction;
    NormalizedCoords player_spawn_point;

    NormalizedCoords camera_coords;

    int8 time_until_input_allowed;

	Entity voids[64];
    int16 void_count;

    Entity grids[64];
    int16 grid_count;

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
