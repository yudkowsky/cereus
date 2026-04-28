#pragma once

#include "types.h"

// windows is defining VOID -> void
#ifdef VOID 
#undef VOID
#endif

typedef struct RendererPlatformHandles
{
    void* module_handle;
    void* window_handle;
}
RendererPlatformHandles;

typedef struct DisplayInfo
{
    int32 display_width;
    int32 display_height;
    int32 client_width;
    int32 client_height;
    int32 refresh_rate;
}
DisplayInfo;

typedef enum 
{
	SPRITE_2D,
    CUBE_3D,
    OUTLINE_3D,
    LASER,
    MODEL_3D,
    WATER_3D,
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
    SPRITE_2D_WATER,
    SPRITE_2D_WIN_BLOCK,
    SPRITE_2D_SOURCE_RED,
    SPRITE_2D_SOURCE_BLUE,
    SPRITE_2D_SOURCE_MAGENTA,
    SPRITE_2D_CROSSHAIR,
    SPRITE_2D_LOCKED_BLOCK,
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
    CUBE_3D_WATER,
    CUBE_3D_WIN_BLOCK,
    CUBE_3D_SOURCE_RED,
    CUBE_3D_SOURCE_BLUE,
    CUBE_3D_SOURCE_MAGENTA,
    CUBE_3D_PLAYER_RED,
    CUBE_3D_PLAYER_BLUE,
    CUBE_3D_PLAYER_MAGENTA,
    CUBE_3D_LOCKED_BLOCK,
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
    MODEL_3D_WATER,
    MODEL_3D_WIN_BLOCK,
    MODEL_3D_SOURCE_RED,
    MODEL_3D_SOURCE_BLUE,
    MODEL_3D_SOURCE_MAGENTA,
    MODEL_3D_LASER_RED,
    MODEL_3D_LASER_BLUE,
    MODEL_3D_LASER_MAGENTA,
    MODEL_3D_PLAYER_RED,
    MODEL_3D_PLAYER_BLUE,
    MODEL_3D_PLAYER_MAGENTA,
    MODEL_3D_LOCKED_BLOCK,
    MODEL_3D_LADDER,
    MODEL_3D_WON_BLOCK,
    MODEL_3D_WATER_BOTTOM,

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
    Vec4 color;
    bool do_aabb;

    // used only for making sure lasers aren't partially visible through mirrors.
    // TODO: find some more intelligent way of packing this information so not every call needs this
    Vec4 start_clip_plane;
	Vec4 end_clip_plane;
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

#define KEY_0 (1ULL << 0) 
#define KEY_1 (1ULL << 1)
#define KEY_2 (1ULL << 2)
#define KEY_3 (1ULL << 3)
#define KEY_4 (1ULL << 4)
#define KEY_5 (1ULL << 5)
#define KEY_6 (1ULL << 6)
#define KEY_7 (1ULL << 7)
#define KEY_8 (1ULL << 8)
#define KEY_9 (1ULL << 9)

#define KEY_A (1ULL << 10)
#define KEY_B (1ULL << 11)
#define KEY_C (1ULL << 12)
#define KEY_D (1ULL << 13)
#define KEY_E (1ULL << 14)
#define KEY_F (1ULL << 15)
#define KEY_G (1ULL << 16)
#define KEY_H (1ULL << 17)
#define KEY_I (1ULL << 18)
#define KEY_J (1ULL << 19)
#define KEY_K (1ULL << 20)
#define KEY_L (1ULL << 21)
#define KEY_M (1ULL << 22)
#define KEY_N (1ULL << 23)
#define KEY_O (1ULL << 24)
#define KEY_P (1ULL << 25)
#define KEY_Q (1ULL << 26) 
#define KEY_R (1ULL << 27) 
#define KEY_S (1ULL << 28) 
#define KEY_T (1ULL << 29) 
#define KEY_U (1ULL << 30) 
#define KEY_V (1ULL << 31) 
#define KEY_W (1ULL << 32) 
#define KEY_X (1ULL << 33) 
#define KEY_Y (1ULL << 34) 
#define KEY_Z (1ULL << 35)

#define KEY_DOT          (1ULL << 36)
#define KEY_COMMA        (1ULL << 37)
#define KEY_SPACE        (1ULL << 38)
#define KEY_SHIFT        (1ULL << 39)
#define KEY_TAB          (1ULL << 40)
#define KEY_ESCAPE       (1ULL << 41)
#define KEY_BACKSPACE    (1ULL << 42)
#define KEY_ENTER        (1ULL << 43)

#define KEY_LEFT_MOUSE   (1ULL << 44)
#define KEY_RIGHT_MOUSE  (1ULL << 45)
#define KEY_MIDDLE_MOUSE (1ULL << 46)

typedef struct Input
{
	float mouse_dx;
    float mouse_dy;

    uint64 keys_held; // get from platform layer
    uint64 keys_pressed; // calculated in game layer from previous input 
    TextInput text;
}
Input;

typedef enum ShaderMode
{
    OLD,
    OUTLINE,
    OUTLINE_TEST,
}
ShaderMode;
