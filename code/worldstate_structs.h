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
    Vec3 coords[256];
	Vec3 scale[256];
    Vec4 rotation[256];
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
    NORTH      = 0,
    WEST       = 1,
    SOUTH      = 2,
    EAST       = 3
}
Direction;

typedef struct Entity
{
    Int3 coords;
    Direction rotation;
    int32 id;
}
Entity;

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
