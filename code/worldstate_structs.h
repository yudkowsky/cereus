#pragma once

#include "types.h"

// windows is defining VOID -> void
#ifdef VOID 
#undef VOID
#endif

typedef enum 
{
	SPRITE_2D = 0,
    CUBE_3D = 1,
    MODEL_3D = 2
}
AssetType;

typedef struct AssetToLoad 
{
    char* path;
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
    bool back_press;
    bool dash_press;
}	
TickInput;

typedef enum TileType
{
    NONE    	= 0,
    VOID        = 1,
    GRID    	= 2,
    WALL    	= 3,
    BOX     	= 4,
    PLAYER  	= 5,
    MIRROR  	= 6,
    CRYSTAL 	= 7,
    PACK        = 8,
    PERM_MIRROR = 9,
    NOT_VOID    = 10,
    WIN_BLOCK   = 11,

    SOURCE_RED     = 32,
    SOURCE_GREEN   = 33,
    SOURCE_BLUE    = 34,
    SOURCE_MAGENTA = 35,
    SOURCE_YELLOW  = 36,
    SOURCE_CYAN    = 37,
    SOURCE_WHITE   = 38,

    LASER_RED = 40,
    LASER_GREEN = 41,
    LASER_BLUE = 42,
    LASER_MAGENTA = 43,
    LASER_YELLOW = 44,
    LASER_CYAN = 45,
    LASER_WHITE = 46
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
    int32 previously_moving_sideways;
    int32 falling_time;

    // for sources/lasers/other colored objects
    Color color;

    // for player
    bool hit_by_red;
    GreenHit green_hit;
    bool hit_by_blue;
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

typedef struct WorldState
{
    uint8 buffer[32768]; // 2 bytes info per tile 
    Entity player;
    Entity pack;
    Entity boxes[32];
    Entity mirrors[32];
    Entity sources[32];
    Entity crystals[32];
    Entity perm_mirrors[32];
    bool player_will_fall_next_turn; // used for not being able to walk one extra tile after walking out of red beam
    bool pack_detached;
    char level_path[256];

    // player's lingering hitbox when hit should still trigger that color
    int32 player_trailing_hitbox_timer;
    Int3 player_trailing_hitbox_coords;

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
    SELECT = 2
}
EditorMode;

typedef struct EditorState
{
    EditorMode editor_mode;
    bool do_wide_camera;
    TileType picked_tile;
    Direction picked_direction;
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

typedef struct LaserBuffer
{
    Direction direction;
    LaserColor color;
    Int3 coords;
}
LaserBuffer;

typedef struct TrailingHitbox
{
	Int3 coords;
    int32 frames;
}
TrailingHitbox;
