#pragma once

#include "types.h"

// windows is defining VOID -> void
#ifdef VOID 
#undef VOID
#endif

typedef struct
{
    void* module_handle;
    void* window_handle;
}
RendererPlatformHandles;

typedef enum 
{
	SPRITE_2D,
    CUBE_3D,
    OUTLINE_3D,
    LASER,
    MODEL_3D
}
AssetType;

typedef enum SpriteId 
{
    NO_ID = -1,

    SPRITE_2D_VOID,
    SPRITE_2D_GRID,
    SPRITE_2D_WALL,
    SPRITE_2D_BOX,
    SPRITE_2D_PLAYER,
	SPRITE_2D_MIRROR,
    SPRITE_2D_GLASS,
    SPRITE_2D_PACK,
    SPRITE_2D_PERM_MIRROR,
    SPRITE_2D_NOT_VOID,
    SPRITE_2D_WIN_BLOCK,
    SPRITE_2D_SOURCE_RED,
    SPRITE_2D_SOURCE_GREEN,
    SPRITE_2D_SOURCE_BLUE,
    SPRITE_2D_SOURCE_MAGENTA,
    SPRITE_2D_SOURCE_YELLOW,
    SPRITE_2D_SOURCE_CYAN,
    SPRITE_2D_SOURCE_WHITE,
    SPRITE_2D_CROSSHAIR,
    SPRITE_2D_LOCKED_BLOCK,
    SPRITE_2D_RESET_BLOCK,
    SPRITE_2D_LADDER,

    SPRITE_2D_FONT_SPACE,
    SPRITE_2D_FONT_LAST = SPRITE_2D_FONT_SPACE + 94,
    SPRITE_2D_COUNT,

    CUBE_3D_VOID,
    CUBE_3D_GRID,
    CUBE_3D_WALL,
    CUBE_3D_BOX,
    CUBE_3D_PLAYER,
    CUBE_3D_MIRROR,
    CUBE_3D_GLASS,
    CUBE_3D_PACK,
    CUBE_3D_PERM_MIRROR,
    CUBE_3D_NOT_VOID,
    CUBE_3D_WIN_BLOCK,
    CUBE_3D_PLAYER_GHOST,
    CUBE_3D_PACK_GHOST,
    CUBE_3D_SOURCE_RED,
    CUBE_3D_SOURCE_GREEN,
    CUBE_3D_SOURCE_BLUE,
    CUBE_3D_SOURCE_MAGENTA,
    CUBE_3D_SOURCE_YELLOW,
    CUBE_3D_SOURCE_CYAN,
    CUBE_3D_SOURCE_WHITE,
    CUBE_3D_LASER_RED,
    CUBE_3D_LASER_GREEN,
    CUBE_3D_LASER_BLUE,
    CUBE_3D_LASER_MAGENTA,
    CUBE_3D_LASER_YELLOW,
    CUBE_3D_LASER_CYAN,
    CUBE_3D_LASER_WHITE,
    CUBE_3D_PLAYER_RED,
    CUBE_3D_PLAYER_GREEN,
    CUBE_3D_PLAYER_BLUE,
    CUBE_3D_PLAYER_MAGENTA,
    CUBE_3D_PLAYER_YELLOW,
    CUBE_3D_PLAYER_CYAN,
    CUBE_3D_PLAYER_WHITE,
    CUBE_3D_LOCKED_BLOCK,
    CUBE_3D_RESET_BLOCK,
    CUBE_3D_LADDER,
    CUBE_3D_WON_BLOCK,

    MODEL_3D_VOID,
    MODEL_3D_GRID,
    MODEL_3D_WALL,
    MODEL_3D_BOX,
    MODEL_3D_PLAYER,
    MODEL_3D_MIRROR,
    MODEL_3D_GLASS,
    MODEL_3D_PACK,
    MODEL_3D_PERM_MIRROR,
    MODEL_3D_NOT_VOID,
    MODEL_3D_WIN_BLOCK,
    MODEL_3D_PLAYER_GHOST,
    MODEL_3D_PACK_GHOST,
    MODEL_3D_SOURCE_RED,
    MODEL_3D_SOURCE_GREEN,
    MODEL_3D_SOURCE_BLUE,
    MODEL_3D_SOURCE_MAGENTA,
    MODEL_3D_SOURCE_YELLOW,
    MODEL_3D_SOURCE_CYAN,
    MODEL_3D_SOURCE_WHITE,
    MODEL_3D_LASER_RED,
    MODEL_3D_LASER_GREEN,
    MODEL_3D_LASER_BLUE,
    MODEL_3D_LASER_MAGENTA,
    MODEL_3D_LASER_YELLOW,
    MODEL_3D_LASER_CYAN,
    MODEL_3D_LASER_WHITE,
    MODEL_3D_PLAYER_RED,
    MODEL_3D_PLAYER_GREEN,
    MODEL_3D_PLAYER_BLUE,
    MODEL_3D_PLAYER_MAGENTA,
    MODEL_3D_PLAYER_YELLOW,
    MODEL_3D_PLAYER_CYAN,
    MODEL_3D_PLAYER_WHITE,
    MODEL_3D_LOCKED_BLOCK,
    MODEL_3D_RESET_BLOCK,
    MODEL_3D_LADDER,
    MODEL_3D_WON_BLOCK,

    ASSET_COUNT
}
SpriteId;

typedef struct DrawCommand
{
    SpriteId sprite_id;
    AssetType type;
    Vec3 coords;
    Vec3 scale;
    Vec4 rotation;
    Vec3 color;
}
DrawCommand;

typedef struct Camera 
{
    Vec3 coords;
    Vec4 rotation;
    float fov;
    float yaw;
    float pitch;
}
Camera;

typedef struct TextInput
{
    uint32 codepoints[64];
    int32 count;
}
TextInput;

typedef struct TickInput
{
	float mouse_dx;
    float mouse_dy;

    bool left_mouse_press;
    bool right_mouse_press;
    bool middle_mouse_press;

	bool a_press;
    bool b_press;
    bool c_press;
    bool d_press;
    bool e_press;
    bool f_press;
    bool g_press;
    bool h_press;
    bool i_press;
    bool j_press;
    bool k_press;
    bool l_press;
    bool m_press;
    bool n_press;
    bool o_press;
    bool p_press;
    bool q_press;
    bool r_press;
    bool s_press;
    bool t_press;
    bool u_press;
    bool v_press;
    bool w_press;
    bool x_press;
    bool y_press;
    bool z_press;

    bool zero_press;
    bool one_press;
    bool two_press;
    bool three_press;
    bool four_press;
    bool five_press;
    bool six_press;
    bool seven_press;
    bool eight_press;
    bool nine_press;

    bool space_press;
    bool shift_press;
    bool tab_press;

    bool dot_press;
    bool comma_press;

    bool backspace_pressed_this_frame;
    bool enter_pressed_this_frame;
    bool escape_press;

    TextInput text;
}	
TickInput;
