#pragma once

#include "types.h"

typedef enum 
{
	SPRITE_2D = 0,
    MODEL_3D = 1,
    CUBE_3D = 2
}
AssetType;

typedef struct AssetToLoad 
{
    char* path;
	AssetType type;
    Vec3 coords[1024];
	Vec3 scale[1024];
    Vec4 rotation[1024];
    int32 instance_count;
}
AssetToLoad;

typedef struct Camera 
{
    Vec3 coords;
    Vec4 rotation;
}
Camera;

typedef enum
{
    NORTH = 0,
    WEST  = 1,
    SOUTH = 2,
    EAST  = 3,
    UP	  = 4,
    DOWN  = 5,
    
    NORTH_WEST = 6,
    NORTH_EAST = 7,
    SOUTH_WEST = 8,
    SOUTH_EAST = 9,
    UP_NORTH   = 10,
    UP_WEST    = 11,
    UP_SOUTH   = 12,
    UP_EAST    = 13,
    DOWN_NORTH = 14,
    DOWN_WEST  = 15,
    DOWN_SOUTH = 16,
    DOWN_EAST  = 17
}
Direction;

typedef struct TickInput
{
    bool w_press;
	bool a_press;
    bool s_press;
    bool d_press;

    bool space_press;
    bool shift_press;

    bool z_press;
    bool r_press;

	bool e_press;

    bool i_press;
	bool j_press;
    bool k_press;
    bool l_press;

	float mouse_dx;
    float mouse_dy;

    bool left_mouse_press;
    bool right_mouse_press;
    bool middle_mouse_press;
}
TickInput;
