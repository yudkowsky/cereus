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

typedef struct Int2
{
    int32 x, y;
}
Int2;

typedef struct AssetToLoad 
{
	bool _;
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
