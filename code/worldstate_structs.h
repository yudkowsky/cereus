#pragma once

#include "types.h"

// windows is defining VOID -> void
#ifdef VOID 
#undef VOID
#endif

typedef enum 
{
	SPRITE_2D,
    CUBE_3D,
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
    SPRITE_2D_CRYSTAL,
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

    SPRITE_2D_FONT_SPACE,
    SPRITE_2D_FONT_LAST = SPRITE_2D_FONT_SPACE + 94,

    SPRITE_2D_COUNT,

    CUBE_3D_VOID,
    CUBE_3D_GRID,
    CUBE_3D_WALL,
    CUBE_3D_BOX,
    CUBE_3D_PLAYER,
    CUBE_3D_MIRROR,
    CUBE_3D_CRYSTAL,
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

    ASSET_COUNT
}
SpriteId;

typedef struct AssetToLoad 
{
    SpriteId sprite_id;
	AssetType type;
    Vec3 coords[8192];
	Vec3 scale[8192];
    Vec4 rotation[8192];
    int32 instance_count;
}
AssetToLoad;

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

    bool dash_press;

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
    CRYSTAL,
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

typedef struct Entity
{
    Int3 coords;
    Vec3 position_norm;
    Direction direction;
    Vec4 rotation_quat;
    int32 id;

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

typedef struct TrailingHitbox
{
	Int3 coords;
    Direction direction;
    int32 frames;
    TileType type;
}
TrailingHitbox;

typedef struct WorldState
{
    uint8 buffer[32768]; // 2 bytes info per tile 
    Entity player;
    Entity pack;
    Entity boxes[128];
    Entity mirrors[128];
    Entity sources[128];
    Entity crystals[128];
    Entity perm_mirrors[128];
    Entity win_blocks[128];
    Entity locked_blocks[128];

    bool player_will_fall_next_turn; // used for not being able to walk one extra tile after walking out of red beam
    bool pack_detached;
    char level_name[64];
    bool in_overworld;

	char solved_levels[64][64];

    TrailingHitbox trailing_hitboxes[64];

    // handle pack turning sequence
    int32 pack_intermediate_states_timer;
    Int3 pack_intermediate_coords;
    Direction pack_orthogonal_push_direction;
    bool do_diagonal_push_on_turn ;
    bool do_orthogonal_push_on_turn;
    bool do_player_and_pack_fall_after_turn;
    bool player_hit_by_blue_in_turn;
    Int3 entity_to_fall_after_blue_not_blue_turn_coords;
    int32 entity_to_fall_after_blue_not_blue_turn_timer;

    // patch on diagonal pass through due to pack hitbox being only on the diagonal in the middle of turn
    int32 pack_hitbox_turning_from_timer;
    Int3 pack_hitbox_turning_from_coords;
    Direction pack_hitbox_turning_from_direction;
    int32 pack_hitbox_turning_to_timer;
    Int3 pack_hitbox_turning_to_coords;
    Direction pack_hitbox_turning_to_direction;

    // ghosts from tp
    Int3 player_ghost_coords;
    Int3 pack_ghost_coords;
    Direction player_ghost_direction;
    Direction pack_ghost_direction;
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

// fill with 0 width
typedef struct LaserBuffer
{
    Vec3 start_coords;
    Vec3 end_coords;
    Direction direction;
    Color color;
}
LaserBuffer;
