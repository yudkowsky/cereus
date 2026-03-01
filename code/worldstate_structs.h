#pragma once

#include "types.h"

// windows is defining VOID -> void
#ifdef VOID 
#undef VOID
#endif

#define MAX_UNDO_DELTAS 2000000
#define MAX_UNDO_ACTIONS 200000 // assumes a ratio of deltas:actions of 10:1
#define MAX_LEVEL_CHANGES 50000

typedef enum 
{
	SPRITE_2D,
    CUBE_3D,
    OUTLINE_3D,
    LASER,
    MODEL_3D
}
AssetType;

typedef struct
{
    void* module_handle;
    void* window_handle;
}
RendererPlatformHandles;

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

typedef enum
{
    NO_DIRECTION = -1,
    NORTH = 0,
    WEST  = 1,
    SOUTH = 2,
    EAST  = 3,
    UP	  = 4,
    DOWN  = 5,
}
Direction;

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

typedef struct EditBuffer
{
    char string[64];
    int32 length;
}
EditBuffer;

// TODO(spike): rename NONE -> NO_TILE
typedef enum TileType
{
    NONE = 0,
    VOID,
    GRID,
    WALL,
    BOX,
    PLAYER,
    MIRROR,
    GLASS,
    PACK,
    PERM_MIRROR,
    NOT_VOID,
    WIN_BLOCK,

    SOURCE_RED,
    SOURCE_GREEN,
    SOURCE_BLUE,
    SOURCE_MAGENTA,
    SOURCE_YELLOW,
    SOURCE_CYAN,
    SOURCE_WHITE,

    LOCKED_BLOCK,
    RESET_BLOCK,
    LADDER,
    WON_BLOCK,

    LASER_RED,
    LASER_GREEN,
    LASER_BLUE,
    LASER_MAGENTA,
    LASER_YELLOW,
    LASER_CYAN,
    LASER_WHITE
}
TileType;

typedef enum Color
{
    NO_COLOR = 0,
    RED      = 1,
    GREEN    = 2,
	BLUE     = 3,
	MAGENTA  = 4,
    YELLOW   = 5,
    CYAN     = 6,
    WHITE    = 7
}
Color;

typedef enum PushResult
{
    CAN_PUSH = 0,
	PAUSE_PUSH = 1,
    FAILED_PUSH = 2
}
PushResult;

typedef struct GreenHit
{
    bool north;
    bool west;
    bool south;
    bool east;
    bool up;
    bool down;
}
GreenHit;

// uses id -1 as overwrite value. i think this is okay here.
typedef struct ResetInfo
{
    int32 id;
    Int3 start_coords;
    TileType start_type;
    Direction start_direction;
    Int3 current_coords;
}
ResetInfo;

typedef struct Entity
{
    Int3 coords;
    Vec3 position_norm;
    Direction direction;
    Vec4 rotation_quat;
    int32 id;
    bool removed;

    int32 in_motion;
    Direction moving_direction;

    bool first_fall_already_done;

    // for sources/lasers
    Color color;

    // for player
    bool hit_by_red;
    GreenHit green_hit;
    bool hit_by_blue;

    // for win blocks
    char next_level[64];
    
    // for locked blocks (and other entities that are locked)
    bool locked;
    char unlocked_by[64];

    // for reset blocks;
    ResetInfo reset_info[16];
}
Entity;

typedef struct Animation
{
    int32 id;
    int32 frames_left;
    Vec3* position_to_change;
    Vec4* rotation_to_change;
    Vec3 position[32];
    Vec4 rotation[32];
}
Animation;

typedef enum PopupType
{
    NO_TYPE = 0,
    GAMEPLAY_MODE_CHANGE,
    GAMEPLAY_SPEED_CHANGE,
    DEBUG_STATE_VISIBILITY_CHANGE,
	LEVEL_BOUNDARY_VISIBILITY_CHANGE,
    LEVEL_SAVE,
    MAIN_CAMERA_SAVE,
    ALT_CAMERA_SAVE,
    PHYSICS_TIMESTEP_CHANGE,
}
PopupType;

typedef struct DebugPopup
{
	int32 frames_left;
    char text[64];
    Vec2 coords;
    PopupType type;
}
DebugPopup;

typedef struct TrailingHitbox
{
	Int3 coords;
    Direction hit_direction;
    Direction moving_direction;
    int32 frames;
    TileType type;
}
TrailingHitbox;

typedef struct WorldState
{
    uint8 buffer[2000000]; // 2 bytes info per tile 
    Entity player;
    Entity pack;
    Entity boxes[64];
    Entity mirrors[64];
    Entity sources[64];
    Entity glass_blocks[64];
    Entity win_blocks[64];
    Entity locked_blocks[64];
    Entity reset_blocks[64];

    char level_name[64];

	char solved_levels[64][64]; // TODO(spike): make this enum? after finalised level names, so don't need to change at two places? maybe this is too late for this, but need to figure something out here
}
WorldState;

typedef enum EditorMode
{
	NO_MODE = 0,
    PLACE_BREAK = 1,
    SELECT = 2,
    SELECT_WRITE = 3,
}
EditorMode;

typedef enum WritingField
{
    NO_WRITING_FIELD = 0,
	WRITING_FIELD_NEXT_LEVEL,
    WRITING_FIELD_UNLOCKED_BY
}
WritingField;

typedef struct EditorState
{
    EditorMode editor_mode;
    bool do_wide_camera;
    Int3 selected_coords;
    TileType picked_tile;
    Direction picked_direction;

    int32 selected_id;
    WritingField writing_field;

    EditBuffer edit_buffer;
}
EditorState;

typedef struct RaycastHit
{
    bool hit;
    Int3 hit_coords;
    Int3 place_coords;
}
RaycastHit;
 
typedef struct Push 
{
    Int3 previous_coords;
    Int3 new_coords;
    TileType type;
    Entity* entity;
}
Push;

typedef struct LaserColor
{
    bool red;
    bool green;
    bool blue;
}
LaserColor;

// assumes 0 width
typedef struct LaserBuffer
{
    Vec3 start_coords;
    Vec3 end_coords;
    Direction direction;
    Color color;
}
LaserBuffer;

// difference encoded undo buffer

typedef struct UndoEntityDelta
{
    int32 id;
    Int3 old_coords;
    Direction old_direction;
    bool was_removed;
}
UndoEntityDelta;

typedef struct UndoActionHeader
{
	uint8 entity_count;
    uint32 delta_start_pos;
    bool level_changed;
    bool was_teleport;
    bool was_reset;
}
UndoActionHeader;

typedef struct UndoLevelChange
{
    char from_level[64];
    bool remove_from_solved;
}
UndoLevelChange;

typedef struct UndoBuffer
{
    // entity deltas (circular)
    UndoEntityDelta deltas[MAX_UNDO_DELTAS];
    uint32 delta_write_pos;
    uint32 delta_count;

    // action headers (circular)
    UndoActionHeader headers[MAX_UNDO_ACTIONS];
    uint32 header_write_pos;
    uint32 header_count;
    uint32 oldest_action_index;

    // level changes (sparse)
    UndoLevelChange level_changes[MAX_LEVEL_CHANGES];
    uint8 level_change_indices[MAX_UNDO_ACTIONS];
    uint32 level_change_write_pos;
    uint32 level_change_count;
}
UndoBuffer;
