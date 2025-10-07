#pragma once

#include "types.h"

typedef struct Vec2
{
    float x, y;
}
Vec2;

typedef struct Vec3
{
    float x, y, z;
}
Vec3;

typedef struct Vec4
{
    float x, y, z, w;
}
Vec4;

typedef struct Int2
{
    int32 x, y;
}
Int2;

typedef struct Int3
{
    int32 x, y, z;
}
Int3;

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
    Vec4 quaternion[256];
    int32 asset_count;
}
AssetToLoad;

typedef struct TickInput
{
    bool w_press;
	bool a_press;
    bool s_press;
    bool d_press;

    bool z_press;
    bool r_press;

	bool e_press;

    bool i_press;
	bool j_press;
    bool k_press;
    bool l_press;

    Vec2 mouse_norm;
    bool left_mouse_press;
    bool right_mouse_press;
    bool middle_mouse_press;
}
TickInput;
