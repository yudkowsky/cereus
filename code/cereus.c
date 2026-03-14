#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // many of these imports are temporary, but haven't set up alternatives yet
#include <math.h> 
#include <stdio.h> 

/*
#include <windows.h> // temp for debug
#ifdef VOID
#undef VOID
#endif
*/

#define FOR(i, n) for (int i = 0; i < n; i++)

// GLOBAL STATE

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

typedef enum
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
    WATER,
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

typedef enum
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

typedef struct
{
    bool north;
    bool west;
    bool south;
    bool east;
    bool up;
    bool down;
}
GreenHit;

// used for the reset blocks, which aren't currently active in the game
typedef struct
{
    int32 id;
    Int3 start_coords;
    TileType start_type;
    Direction start_direction;
    Int3 current_coords;
}
ResetInfo;

// coords are integer coordinates of the entity, position_norm is the floating point coords in world space.
// likewise direction is one of 6 orientations, rotation_quat is the actual rotation passed to renderer.
typedef struct
{
    int32 id;
    Int3 coords;
    Vec3 position_norm;
    Direction direction;
    Vec4 rotation_quat;
    bool removed;

    int32 in_motion;
    Direction moving_direction;

    bool first_fall_already_done; // first fall is slower (accelerates to terminal velocity). all other falls are lerped

    // for sources/lasers
    Color color;

    // for player
    bool hit_by_red;
    GreenHit green_hit;
    bool hit_by_blue;

    // for win blocks
    char next_level[64]; // TODO: make level names an enum so don't need to carry around 64 * char * 2 per entity
    
    // for locked blocks (and other entities that are locked)
    bool locked;
    char unlocked_by[64]; 

    // for reset blocks;
    ResetInfo reset_info[16];
}
Entity;

// the 2MB buffer is dense representation of the level encoded by coords
typedef struct
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

	char solved_levels[64][64];
}
WorldState;

// trailing hitboxes are to leave something interactible in a location even if the actual object has moved on to a different integer coord. this is helpful mostly for updateLaserBuffer
typedef struct
{
	Int3 coords;
    Direction hit_direction;
    Direction moving_direction;
    int32 frames;
    TileType type;
}
TrailingHitbox;

typedef struct
{
    int32 id;
    int32 frames_left;
    Vec3* position_to_change;
    Vec4* rotation_to_change;
    Vec3 position[32];
    Vec4 rotation[32];
}
Animation;

typedef enum
{
    CAN_PUSH = 0,
	PAUSE_PUSH = 1,
    FAILED_PUSH = 2
}
PushResult;

typedef struct
{
    Int3 previous_coords;
    Int3 new_coords;
    TileType type;
    Entity* entity;
}
Push;

// this is a bunch of state to do with handling what gets pushed when during the turn. pack_intermediate_states_timer is the main thing: it just ticks down while the player is turning.
// later there is a loop with some numbers that look nice for when things should be turning, based on this timer. the objects don't get pushed the same frame the player turns,
// so need to store the coords of what to push, and what direction to push it (if it is pushing during this turn)
typedef struct
{
    int32 pack_intermediate_states_timer;

    Int3 pack_intermediate_coords;
    Int3 pack_hitbox_turning_to_coords;

    bool do_diagonal_push_on_turn;
    bool do_orthogonal_push_on_turn;
    Direction pack_orthogonal_push_direction;
}
PackTurnState;

typedef enum
{
    WORLD_0,
    WORLD_1,
    GATE_1,
    //WORLD_2,
    //GATE_2
}
GameProgress;

// EDITOR STRUCTS

typedef struct
{
    char string[64];
    int32 length;
}
EditBuffer;

typedef enum
{
	NO_MODE = 0,
    PLACE_BREAK = 1,
    SELECT = 2,
    SELECT_WRITE = 3,
}
EditorMode;

typedef enum
{
    NO_WRITING_FIELD = 0,
	WRITING_FIELD_NEXT_LEVEL,
    WRITING_FIELD_UNLOCKED_BY
}
WritingField;

typedef struct
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

typedef enum
{
    MAIN_WAITING,
    MAIN_TO_ALT,
    ALT_WAITING,
    ALT_TO_MAIN
}
CameraMode;

typedef struct
{
    bool hit;
    Int3 hit_coords;
    Int3 place_coords;
}
RaycastHit;

typedef enum
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
    CHEAT_MODE_TOGGLE,
    SHADER_MODE_CHANGE,
}
PopupType;

typedef struct
{
	int32 frames_left;
    char text[64];
    Vec2 coords;
    PopupType type;
}
DebugPopup;

// LASER STRUCTS

typedef struct
{
    bool red;
    bool green;
    bool blue;
}
LaserColor;

// assumes 0 width
typedef struct
{
    Vec3 start_coords;
    Vec3 end_coords;
    Direction direction;
    Color color;
}
LaserBuffer;

// UNDO BUFFER STRUCTS

typedef struct
{
    int32 id;
    Int3 old_coords;
    Direction old_direction;
    bool was_removed;
}
UndoEntityDelta;

typedef struct
{
	uint8 entity_count;
    uint32 delta_start_pos;
    bool level_changed;
    bool was_teleport;
    bool was_reset;
    bool was_climb;
}
UndoActionHeader;

typedef struct
{
    char from_level[64];
    bool remove_from_solved;
}
UndoLevelChange;

// approx. 5mb undo buffer
#define MAX_UNDO_DELTAS 200000
#define MAX_UNDO_ACTIONS 20000 // assumes a ratio of deltas:actions of 10:1 i.e. 10 entities per level
#define MAX_LEVEL_CHANGES 5000

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

// CONSTS AND GLOBALS
DisplayInfo game_display = {0};

const float TAU = 6.2831853071f;

const Vec3 DEFAULT_SCALE = { 1.0f,  1.0f,  1.0f  };
const Vec3 PLAYER_SCALE  = { 0.75f, 0.75f, 0.75f };

const float LASER_WIDTH = 0.25;
const float MAX_RAYCAST_SEEK_LENGTH = 100.0f;

const int32 STANDARD_TIME_UNTIL_ALLOW_INPUT= 9;
const int32 PLACE_BREAK_TIME_UNTIL_ALLOW_INPUT = 5;
const int32 MOVE_OR_PUSH_ANIMATION_TIME = 9;
const int32 TURN_ANIMATION_TIME = 9; // somewhat hard coded, tied to PUSH_FROM_TURN...
const int32 FALL_ANIMATION_TIME = 8; // hard coded (because acceleration in first fall anim must be constant)
const int32 CLIMB_ANIMATION_TIME = 9;
const int32 PUSH_FROM_TURN_ANIMATION_TIME = 6;
const int32 FAILED_ANIMATION_TIME = 8;
const int32 STANDARD_IN_MOTION_TIME = 7;
const int32 STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH = 4;
const int32 PUSH_FROM_TURN_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH = 2;
const int32 SUCCESSFUL_TP_TIME = 8;
const int32 FAILED_TP_TIME = 8;

const int32 TRAILING_HITBOX_TIME = 5;
const int32 FIRST_TRAILING_PACK_TURN_HITBOX_TIME = 2;
const int32 TIME_BEFORE_ORTHOGONAL_PUSH_STARTS_IN_TURN = 2;
const int32 PACK_TIME_IN_INTERMEDIATE_STATE = 4;

const int32 MAX_ENTITY_INSTANCE_COUNT = 64;
const int32 MAX_ENTITY_PUSH_COUNT = 32;
const int32 MAX_ANIMATION_COUNT = 32;
const int32 MAX_LASER_TRAVEL_DISTANCE = 128;
const int32 MAX_LASER_TURNS_ALLOWED = 16;
const int32 MAX_PSEUDO_SOURCE_COUNT = 32;
const int32 MAX_PUSHABLE_STACK_SIZE = 32;
const int32 MAX_TRAILING_HITBOX_COUNT = 32;
const int32 MAX_LEVEL_COUNT = 64;
const int32 MAX_RESET_COUNT = 16;
const int32 MAX_DEBUG_POPUP_COUNT = 32;

const Int3 AXIS_X = { 1, 0, 0 };
const Int3 AXIS_Y = { 0, 1, 0 };
const Int3 AXIS_Z = { 0, 0, 1 };
const Vec3 VEC3_0 = { 0, 0, 0 };
const Vec4 IDENTITY_QUATERNION  = { 0, 0, 0, 1 };

const int32 PLAYER_ID = 1;
const int32 PACK_ID   = 2;
const int32 OUTLINE_DRAW_ID = ASSET_COUNT;
const int32 ID_OFFSET_BOX     	   = 100 * 1;
const int32 ID_OFFSET_MIRROR  	   = 100 * 2;
const int32 ID_OFFSET_GLASS 	   = 100 * 3;
const int32 ID_OFFSET_SOURCE  	   = 100 * 4;
const int32 ID_OFFSET_WIN_BLOCK    = 100 * 12;
const int32 ID_OFFSET_LOCKED_BLOCK = 100 * 13;
const int32 ID_OFFSET_RESET_BLOCK  = 100 * 14;

const int32 FONT_FIRST_ASCII = 32;
const int32 FONT_LAST_ASCII = 126;
const int32 FONT_CELL_WIDTH_PX = 6;
const int32 FONT_CELL_HEIGHT_PX = 10;
const float DEFAULT_TEXT_SCALE = 30.0f;

const int32 SAVE_WRITE_VERSION = 1;

const int32 CAMERA_CHUNK_SIZE = 24;
const char MAIN_CAMERA_CHUNK_TAG[4] = "CMRA";
const char ALT_CAMERA_CHUNK_TAG[4] = "CAM2";
const int32 WIN_BLOCK_CHUNK_SIZE = 76;
const char WIN_BLOCK_CHUNK_TAG[4] = "WINB";
const int32 LOCKED_INFO_CHUNK_SIZE = 76;
const char LOCKED_INFO_CHUNK_TAG[4] = "LOKB";
const int32 RESET_INFO_SINGLE_ENTRY_SIZE = 6;
const char RESET_INFO_CHUNK_TAG[4] = "RESB";

const int32 OVERWORLD_SCREEN_SIZE_X = 21;
const int32 OVERWORLD_SCREEN_SIZE_Z = 15;

const double DEFAULT_PHYSICS_TIMESTEP = 1.0/60.0;
double physics_timestep = 1.0/60.0;
double physics_accumulator = 0;
double timer_accumulator = 0;
double global_time = 0;

const char debug_level_name[64] = "red-last";
const char relative_start_level_path_buffer[64] = "data/levels/";
const char source_start_level_path_buffer[64] = "../cereus/data/levels/";
const char solved_level_path[64] = "data/meta/solved-levels.meta";
const char undo_meta_path[64] = "data/meta/undo-buffer.meta";
const char overworld_zero_name[64] = "overworld-zero";

// CAMERA
const float CAMERA_SENSITIVITY = 0.005f;
const float CAMERA_MOVE_STEP = 0.2f;
const float CAMERA_FOV = 15.0f;

Camera camera = {0};
Camera camera_with_ow_offset = {0};
CameraMode camera_mode = MAIN_WAITING;

Camera saved_main_camera = {0};
Camera saved_alt_camera = {0};
Camera saved_overworld_camera = {0};
CameraMode saved_overworld_camera_mode = {0};

Int3 camera_screen_offset = {0};
const Int3 OVERWORLD_CAMERA_CENTER_START = { 58, 2, 197 };
bool draw_level_boundary = false;

float camera_lerp_t = 0.0f;
const float CAMERA_T_TIMESTEP = 0.05f;
int32 camera_target_plane = 0; // y level of xz plane which calculates targeted point during camera interpolation function 
                               // TODO: should probably be something defined by level, not just player coords at startup

DrawCommand draw_commands[8192] = {0};
int32 draw_command_count = 0;

WorldState world_state = {0};
WorldState next_world_state = {0};
WorldState leap_of_faith_snapshot = {0}; // TODO: doesn't change the buffer, so could save 2MB memory by using some EntitySnapshot struct
WorldState overworld_zero = {0}; // TODO: probably don't have to carry this around, just read from zeroed overworld file when i need this (on restart in overworld)
Int3 level_dim = {0};

UndoBuffer undo_buffer = {0};
int32 undos_performed = 0;
bool restart_last_turn = false;

// controls how long until player is allowed to make action like movement. will rework when add buffered inputs
int32 time_until_allow_game_input = 0;
int32 time_until_allow_meta_input = 0;

EditorState editor_state = {0};
LaserBuffer laser_buffer[64] = {0};
TrailingHitbox trailing_hitboxes[32]; 
Animation animations[32];

bool in_overworld = false;
bool pack_detached = false;
ShaderMode game_shader_mode = OLD;
GameProgress game_progress = WORLD_0;
bool cheating = false;

bool player_will_fall_next_turn = false;
bool bypass_player_fall;
PackTurnState pack_turn_state = {0};

// ghosts from tp
Int3 player_ghost_coords = {0};
Int3 pack_ghost_coords = {0};
Direction player_ghost_direction = {0};
Direction pack_ghost_direction = {0};
bool do_player_ghost = false;
bool do_pack_ghost = false;

// debug text
Vec2 debug_text_start_coords = {0};
const int32 MAX_DEBUG_TEXT_COUNT = 32;
const float DEBUG_TEXT_Y_DIFF = 40.0f;
char debug_text_buffer[32][256] = {0};
int32 debug_text_count = 0;
bool do_debug_text = false;

// debug popups
Vec2 debug_popup_start_coords = {0};
DebugPopup debug_popups[32];
const float DEBUG_POPUP_STEP_SIZE = 30.0f;
const int32 DEFAULT_POPUP_TIME = 100;

// CAMERA HELPERS

void cameraBasisFromYaw(float yaw, Vec3* right, Vec3* forward)
{
    float sine_yaw = sinf(yaw), cosine_yaw = cosf(yaw);
    *right   = (Vec3){ cosine_yaw, 0,   -sine_yaw };
    *forward = (Vec3){ -sine_yaw,  0, -cosine_yaw };
}

Vec4 quaternionFromAxisAngle(Vec3 axis, float angle)
{
    float sine = sinf(angle*0.5f), cosine = cosf(angle*0.5f);
    return (Vec4){ axis.x*sine, axis.y*sine, axis.z*sine, cosine};
}

bool quaternionIsZero(Vec4 quaternion)
{
    return (quaternion.x == 0 && quaternion.y == 0 && quaternion.z == 0 && quaternion.w == 0);
}

Vec4 quaternionScalarMultiply(Vec4 quaternion, float scalar)
{
    return (Vec4){ quaternion.x*scalar, quaternion.y*scalar, quaternion.z*scalar, quaternion.w*scalar };
}

Vec4 quaternionAdd(Vec4 a, Vec4 b)
{
    return (Vec4){ a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w };
}

Vec4 quaternionSubtract(Vec4 a, Vec4 b)
{
    return (Vec4){ a.x-b.x, a.y-b.y, a.z-b.z, a.w-b.w };
}

float quaternionDot(Vec4 a, Vec4 b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
}

Vec4 quaternionConjugate(Vec4 quaternion)
{
    return (Vec4){ -quaternion.x, -quaternion.y, -quaternion.z, quaternion.w };
}

Vec4 quaternionNegate(Vec4 quaternion)
{
    return (Vec4){ -quaternion.x, -quaternion.y, -quaternion.z, -quaternion.w };
}

Vec4 quaternionMultiply(Vec4 a, Vec4 b)
{
    return (Vec4){ a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
        		   a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
        		   a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
        		   a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z};
}

Vec4 quaternionNormalize(Vec4 quaternion)
{
    float length_squared = quaternion.x*quaternion.x + quaternion.y*quaternion.y + quaternion.z*quaternion.z + quaternion.w*quaternion.w;
    if (length_squared <= 1e-8f) return (Vec4){0, 0, 0, 1}; 
    float inverse_length = 1.0f / sqrtf(length_squared);
    return quaternionScalarMultiply(quaternion, inverse_length);
}

Vec3 vec3OuterProduct(Vec3 a, Vec3 b)
{
    return (Vec3){ a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

Vec3 vec3RotateByQuaternion(Vec3 input_vector, Vec4 quaternion)
{
    Vec3 quaternion_vector_part = (Vec3){ quaternion.x, quaternion.y, quaternion.z };
    float quaternion_scalar_part = quaternion.w;
    Vec3 q_cross_v = vec3OuterProduct(quaternion_vector_part, input_vector);
    Vec3 temp_vector = (Vec3){ q_cross_v.x + quaternion_scalar_part * input_vector.x,
    q_cross_v.y + quaternion_scalar_part * input_vector.y,
    q_cross_v.z + quaternion_scalar_part * input_vector.z};
    Vec3 q_cross_t = vec3OuterProduct(quaternion_vector_part, temp_vector);
    return (Vec3){ input_vector.x + 2.0f * q_cross_t.x,
    input_vector.y + 2.0f * q_cross_t.y,
    input_vector.z + 2.0f * q_cross_t.z};
}

// MATH HELPER FUNCTIONS

Vec3 intCoordsToNorm(Int3 int_coords) 
{
    return (Vec3){ (float)int_coords.x, (float)int_coords.y, (float)int_coords.z };
}

Int3 normCoordsToInt(Vec3 norm_coords) 
{
    return (Int3){ (int32)floorf(norm_coords.x + 0.5f), (int32)floorf(norm_coords.y + 0.5f), (int32)floorf(norm_coords.z + 0.5f) }; 
}

bool intCoordsWithinLevelBounds(Int3 coords) 
{
    return (coords.x >= 0 && coords.y >= 0 && coords.z >= 0 && coords.x < level_dim.x && coords.y < level_dim.y && coords.z < level_dim.z); 
}

bool normCoordsWithinLevelBounds(Vec3 coords) 
{
    return (coords.x > 0 && coords.y > 0 && coords.z >= 0 && coords.x < level_dim.x && coords.y < level_dim.y && coords.z < level_dim.z); 
}

bool int3IsEqual(Int3 a, Int3 b) 
{
    return (a.x == b.x && a.y == b.y && a.z == b.z); 
}

bool vec3IsEqual(Vec3 a, Vec3 b)
{
    return (a.x == b.x && a.y == b.y && a.z == b.z); 
}

Int3 int3Negate(Int3 coords) 
{
    return (Int3){ -coords.x, -coords.y, -coords.z }; 
}

Vec3 vec3Negate(Vec3 coords) 
{
    return (Vec3){ -coords.x, -coords.y, -coords.z }; 
}

bool int3IsZero(Int3 coords)
{
    return (coords.x == 0 && coords.y == 0 && coords.z == 0);
}

bool vec3IsZero(Vec3 coords) 
{
    return (coords.x == 0 && coords.y == 0 && coords.z == 0); 
}

Int3 int3Add(Int3 a, Int3 b)
{
    return (Int3){ a.x+b.x, a.y+b.y, a.z+b.z }; 
}

Vec3 vec3Add(Vec3 a, Vec3 b) 
{
    return (Vec3){ a.x+b.x, a.y+b.y, a.z+b.z };
}

Int3 int3Subtract(Int3 a, Int3 b) 
{
    return (Int3){ a.x-b.x, a.y-b.y, a.z-b.z }; 
}

Vec3 vec3Subtract(Vec3 a, Vec3 b) 
{
    return (Vec3){ a.x-b.x, a.y-b.y, a.z-b.z }; 
}

Vec3 vec3Abs(Vec3 a) 
{
    return (Vec3){ (float)fabs(a.x), (float)fabs(a.y), (float)fabs(a.z) }; 
}

Int3 int3ScalarMultiply(Int3 position, int32 scalar) 
{
    return (Int3){ position.x*scalar, position.y*scalar, position.z*scalar }; 
}

Vec3 vec3ScalarMultiply(Vec3 position, float scalar) 
{
    return (Vec3){ position.x*scalar, position.y*scalar, position.z*scalar }; 
}

float vec3Inner(Vec3 a, Vec3 b)
{
	return a.x*b.x + a.y*b.y + a.z*b.z;
}

float vec3Length(Vec3 v)
{
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
}

Vec3 vec3Normalize(Vec3 v)
{
    float length_squared = v.x*v.x + v.y*v.y + v.z*v.z;
    if (length_squared <= 1e-8f) return VEC3_0; 
    float inverse_length = 1.0f / sqrtf(length_squared);
    return vec3ScalarMultiply(v, inverse_length);
}

Vec3 vec3Hadamard(Vec3 a, Vec3 b)
{
    return (Vec3){ a.x*b.x, a.y*b.y, a.z*b.z };
}

// BUFFER / STATE INTERFACING

int32 coordsToBufferIndexType(Int3 coords)
{
    return 2*(level_dim.x*level_dim.z*coords.y + level_dim.x*coords.z + coords.x); 
}
int32 coordsToBufferIndexDirection(Int3 coords)
{
    return 2*(level_dim.x*level_dim.z*coords.y + level_dim.x*coords.z + coords.x) + 1; 
}

Int3 bufferIndexToCoords(int32 buffer_index)
{
    Int3 coords = {0};
    coords.x = (buffer_index/2) % level_dim.x;
    coords.y = (buffer_index/2) / (level_dim.x * level_dim.z);
    coords.z = ((buffer_index/2) / level_dim.x) % level_dim.z;
    return coords;
}

void setTileType(TileType type, Int3 coords) 
{
    next_world_state.buffer[coordsToBufferIndexType(coords)] = type; 
}

void setTileDirection(Direction direction, Int3 coords)
{
    next_world_state.buffer[coordsToBufferIndexDirection(coords)] = direction;
}

TileType getTileType(Int3 coords) 
{
    return next_world_state.buffer[coordsToBufferIndexType(coords)]; 
}

Direction getTileDirection(Int3 coords) 
{
    return next_world_state.buffer[coordsToBufferIndexDirection(coords)]; 
}

// sets coords and position of an entity to some values, and updates the buffer accordingly 
void moveEntityInBufferAndState(Entity* e, Int3 end_coords, Direction end_direction)
{
    TileType type = getTileType(e->coords); // could also get from id
    setTileType(NONE, e->coords);
    setTileDirection(NO_DIRECTION, e->coords);
	e->coords = end_coords;
    e->direction = end_direction;
    setTileType(type, end_coords);
    setTileDirection(end_direction, end_coords);
}

bool isSource(TileType tile) 
{
    return (tile == SOURCE_RED || tile == SOURCE_GREEN || tile == SOURCE_BLUE || tile == SOURCE_MAGENTA || tile == SOURCE_YELLOW || tile == SOURCE_CYAN|| tile == SOURCE_WHITE);
}

// only checks tile types - doesn't do what canPush does
bool isPushable(TileType tile)
{
    if (tile == BOX || tile == GLASS || tile == MIRROR || tile == PACK || isSource(tile)) return true;
    else return false;
}

bool isEntity(TileType tile)
{
    if (tile == BOX || tile == GLASS || tile == MIRROR || tile == PACK || tile == PLAYER || tile == WIN_BLOCK || tile == LOCKED_BLOCK || tile == RESET_BLOCK || isSource(tile)) return true;
    else return false;
}

TileType getTileTypeFromId(int32 id)
{
    if (id == PLAYER_ID) return PLAYER;
    if (id == PACK_ID) return PACK;
    int32 check = (id / 100 * 100);
    if (check >= ID_OFFSET_SOURCE && check < ID_OFFSET_WIN_BLOCK)
    {
        Color source_color = (id - ID_OFFSET_SOURCE) / 100;
        switch (source_color)
        {
            case RED: 	  return SOURCE_RED;
            case GREEN:   return SOURCE_GREEN;
            case BLUE:    return SOURCE_BLUE;
            case MAGENTA: return SOURCE_MAGENTA;
            case YELLOW:  return SOURCE_YELLOW;
            case CYAN: 	  return SOURCE_CYAN;
            case WHITE:   return SOURCE_WHITE;
            default: return NONE;
        }
    }
    else if (check == ID_OFFSET_BOX) 		  return BOX;
    else if (check == ID_OFFSET_MIRROR) 	  return MIRROR;
    else if (check == ID_OFFSET_GLASS) 	 	  return GLASS;
    else if (check == ID_OFFSET_WIN_BLOCK) 	  return WIN_BLOCK;
    else if (check == ID_OFFSET_LOCKED_BLOCK) return LOCKED_BLOCK;
    else if (check == ID_OFFSET_RESET_BLOCK)  return RESET_BLOCK;
    else return NONE;
}

Color getEntityColor(Int3 coords)
{
    switch (getTileType(coords))
    {
        case SOURCE_RED:     return RED;
        case SOURCE_GREEN:   return GREEN;
        case SOURCE_BLUE:	 return BLUE;
        case SOURCE_MAGENTA: return MAGENTA;
        case SOURCE_YELLOW:  return YELLOW;
        case SOURCE_CYAN:    return CYAN;
        case SOURCE_WHITE:	 return WHITE;
        default: return NO_COLOR;
    }
}

Entity* getEntityAtCoords(Int3 coords)
{
    TileType tile = getTileType(coords);
    Entity *entity_group = 0;
    if (isSource(tile)) entity_group = next_world_state.sources;
    else switch(tile)
    {
        case BOX:     	   entity_group = next_world_state.boxes;    	  break;
        case MIRROR:  	   entity_group = next_world_state.mirrors;  	  break;
        case GLASS: 	   entity_group = next_world_state.glass_blocks;  break;
        case WIN_BLOCK:    entity_group = next_world_state.win_blocks;    break;
        case LOCKED_BLOCK: entity_group = next_world_state.locked_blocks; break;
        case RESET_BLOCK:  entity_group = next_world_state.reset_blocks;  break;
        case PLAYER: return &next_world_state.player;
        case PACK:	 return &next_world_state.pack;
        default: return 0;
    }
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (entity_group[entity_index].removed) continue;
        if (int3IsEqual(entity_group[entity_index].coords, coords)) return &entity_group[entity_index];
    }
    return 0;
}

int32 getEntityId(Int3 coords)
{
    Entity *entity = getEntityAtCoords(coords);
    return entity->id;
}

Entity* getEntityFromId(int32 id)
{
    if (id < 0) return 0;
    if (id == PLAYER_ID) return &next_world_state.player;
    else if (id == PACK_ID) return &next_world_state.pack;
    else 
    {
        Entity* entity_group = 0;
        int32 switch_value =  ((id / 100) * 100);
        if 		(switch_value == ID_OFFSET_BOX)    		 entity_group = next_world_state.boxes; 
        else if (switch_value == ID_OFFSET_MIRROR) 		 entity_group = next_world_state.mirrors;
        else if (switch_value == ID_OFFSET_GLASS) 	 	 entity_group = next_world_state.glass_blocks;
        else if (switch_value >= ID_OFFSET_SOURCE && switch_value < ID_OFFSET_WIN_BLOCK) entity_group = next_world_state.sources;
        else if (switch_value == ID_OFFSET_WIN_BLOCK) 	 entity_group = next_world_state.win_blocks;
        else if (switch_value == ID_OFFSET_LOCKED_BLOCK) entity_group = next_world_state.locked_blocks;
        else if (switch_value == ID_OFFSET_RESET_BLOCK)  entity_group = next_world_state.reset_blocks;

        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            if (entity_group[entity_index].id == id) return &entity_group[entity_index];
        }
        return 0;
    }
}

Direction getEntityDirection(Int3 coords) 
{
    Entity *entity = getEntityAtCoords(coords);
    return entity->direction;
}

Direction oppositeDirection(Direction direction)
{
    switch (direction)
    {
        case NORTH: return SOUTH;
        case SOUTH: return NORTH;
        case WEST:  return EAST;
        case EAST:  return WEST;
        case UP:    return DOWN;
        case DOWN:  return UP;

        default: return NO_DIRECTION;
    }
}

Direction getNextRotationalDirection(Direction direction, bool clockwise)
{
    Direction next_direction = NO_DIRECTION;
    if (!clockwise)
    {
        next_direction = direction - 1;
        if (next_direction == -1) next_direction = EAST;
    }
    else
    {
        next_direction = direction + 1;
        if (next_direction == 4) next_direction = NORTH;
    }
    return next_direction;
}

// gets count of currently active (not id == -1 or removed)
int32 getEntityCount(Entity *entity_group)
{
    int32 count = 0;
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (entity_group[entity_index].id == -1 || entity_group[entity_index].removed) continue;
        count++;
    }
    return count;
}

int32 sourceColorIdOffset(Color color)
{
    switch (color)
    {
        case RED: 	  return RED * 100; 
        case GREEN:   return GREEN * 100;
        case BLUE: 	  return BLUE * 100;
        case MAGENTA: return MAGENTA * 100;
        case YELLOW:  return YELLOW * 100;
        case CYAN: 	  return CYAN * 100;
        case WHITE:   return WHITE * 100;
        default: return 0;
    }
}

int32 entityIdOffset(Entity *entity, Color color)
{
    if 		(entity == next_world_state.boxes)    	   return ID_OFFSET_BOX;
    else if (entity == next_world_state.mirrors)  	   return ID_OFFSET_MIRROR;
    else if (entity == next_world_state.glass_blocks)  return ID_OFFSET_GLASS;
    else if (entity == next_world_state.win_blocks)    return ID_OFFSET_WIN_BLOCK;
    else if (entity == next_world_state.locked_blocks) return ID_OFFSET_LOCKED_BLOCK;
    else if (entity == next_world_state.reset_blocks)  return ID_OFFSET_RESET_BLOCK;
    else if (entity == next_world_state.sources)  	   return ID_OFFSET_SOURCE + sourceColorIdOffset(color);
    return 0;
}

Vec3 directionToVector(Direction direction)
{
    switch (direction)
    {
        case NORTH: return (Vec3){  0,  0, -1 };
        case WEST:  return (Vec3){ -1,  0,  0 };
        case SOUTH: return (Vec3){  0,  0,  1 };
        case EAST:  return (Vec3){  1,  0,  0 };
        case UP:    return (Vec3){  0,  1,  0 };
        case DOWN:  return (Vec3){  0, -1,  0 };

        default: return VEC3_0;
    }
}

Vec4 directionToQuaternion(Direction direction, bool roll_z)
{
    float yaw = 0.0f;
    float roll = 0.0f;
    bool do_yaw = false;
    bool do_roll = false;
    switch (direction)
    {
        case NORTH: 
        {
            yaw = 0.0f;
            do_yaw = true;
            break;
        }
        case WEST: 
        {
            yaw = 0.25f  * TAU;
            do_yaw = true;
            break;
        }
        case SOUTH: 
        {
            yaw = 0.5f   * TAU;
            do_yaw = true;
            break;
        }
        case EAST:
        {
            yaw = -0.25f * TAU;
            do_yaw = true;
            break;
        }
        case UP:
        {
            roll = 0.25f * TAU;
            do_roll = true;
            break;
        }
        case DOWN:
        {
            roll = -0.25f * TAU;
            do_roll = true;
            break;
        }
        default: return (Vec4){ 0, 0, 0, 0 };
    }

    if (do_yaw && !do_roll) return quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), yaw);
    if (!do_yaw && do_roll) return quaternionFromAxisAngle(intCoordsToNorm(roll_z ? AXIS_Z : AXIS_X), roll);
    return IDENTITY_QUATERNION;
}

int32 setEntityInstanceInGroup(Entity* entity_group, Int3 coords, Direction direction, Color color) 
{
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (entity_group[entity_index].id != -1) continue;
        entity_group[entity_index].coords = coords;
        entity_group[entity_index].position_norm = intCoordsToNorm(coords); 
        entity_group[entity_index].direction = direction;
        entity_group[entity_index].rotation_quat = directionToQuaternion(direction, true);
        entity_group[entity_index].color = color;
        entity_group[entity_index].id = entity_index + entityIdOffset(entity_group, color);
        setTileDirection(direction, coords);
        return entity_group[entity_index].id;
    }
    return 0;
}

// updates position_norm and rotation_quat to be float/quaternion versions of integer coords/direction enum
void setEntityVecsFromInts(Entity* e)
{
    e->position_norm = intCoordsToNorm(e->coords);
    e->rotation_quat = directionToQuaternion(e->direction, true);
}

// FILE I/O

// .level file structure: 
//
// first byte is version. version 0 is a dense representation, like the buffer i have in memory. 
// version 1 encodes buffer index, tile type, direction for every object, in a sparse representation, and so is much smaller, because >99% of a level is air
// then 3 bytes: x,y,z of level dimensions
// next x*y*z * 2 bytes: actual level buffer. this is still dense. takes up 2MB memory for the largest level (overworld, which is 250*250*16 in dimension; 2 bytes, one for tile type, one for direction)

// then chunking starts: 4 bytes for tag, 4 bytes for size (not including tag or size), and then data
// camera: 	 		tag: CMRA, 	size: 24 (6 * 4b), 			data: x, y, z, fov, yaw, pitch (as floats)
// win block: 		tag: WINB, 	size: 76 (3 * 4b + 64b), 	data: x, y, z (as int32), char[64] path
// locked block: 	tag: LOKB, 	size: 76 (3 * 4b + 64b),	data: x, y, z (as int32), char[64] path
// reset block: 	tag: RESB, 	size: 3b + n*(3+1+3)b,		data: x, y, z + n * entity_to_reset(x, y, z, direction) (as int32)

void buildLevelPathFromName(char level_name[64], char (*level_path)[64], bool overwrite_source)
{
    char prefix[64];
    if (overwrite_source) memcpy(prefix, source_start_level_path_buffer, sizeof(prefix));
    else			      memcpy(prefix, relative_start_level_path_buffer, sizeof(prefix));
    snprintf(*level_path, sizeof(*level_path), "%s%s.level", prefix, level_name);
}

bool readChunkHeader(FILE* file, char out_tag[4], int32 *out_size)
{
    if (fread(out_tag, 4, 1, file) != 1) return false; // EOF
    if (fread(out_size, 4, 1, file) != 1) return false; // truncated
    return true;
}

// gets position and count of some chunk tag. cursor placed right before chunk tag
int32 getCountAndPositionOfChunk(FILE* file, char tag[3], int32 positions[64])
{
	char chunk[4] = {0};
    int32 chunk_size = 0;
    int32 tag_pos = 0;
    int32 count = 0;

    // go to start of chunking
    fseek(file, 0, SEEK_SET);
    uint8 version = 0;
    fread(&version, 1, 1, file);
    int32 chunk_start = 0;
    if (version == 0)
    {
        chunk_start = 4 + (level_dim.x*level_dim.y*level_dim.z * 2);
    }
    else if (version == 1)
    {
        fseek(file, 4, SEEK_SET);
        int32 tile_count = 0;
        fread(&tile_count, 4, 1, file);
        chunk_start = 8 + (tile_count * 6);
    }
	fseek(file, chunk_start, SEEK_SET);

    while (true)
    {
        tag_pos = ftell(file);

        if (fread(chunk, 4, 1, file) != 1)
        {
            // eof
            return count;
        }
        if (memcmp(chunk, tag, 4) == 0)
        {
            // found tag
            positions[count] = tag_pos; 
            count++;
        }
        fread(&chunk_size, 4, 1, file);
        fseek(file, chunk_size, SEEK_CUR);
    }
}

// reads version nr directly from file
void loadBufferInfo(FILE* file)
{
    fseek(file, 0, SEEK_SET); // seek to start of file

    // get version
    int32 version = 0;
    fread(&version, 1, 1, file);

    // get level dimensions
    uint8 x, y, z;
    fread(&x, 1, 1, file);
    fread(&y, 1, 1, file);
    fread(&z, 1, 1, file);
    level_dim.x = x;
    level_dim.y = y;
    level_dim.z = z;

    if (version == 0)
    {
        fread(&next_world_state.buffer, 1, level_dim.x*level_dim.y*level_dim.z * 2, file);
    }
    else if (version == 1)
    {
        int32 tile_count = 0;
        fread(&tile_count, 4, 1, file);
        FOR(tile_index, tile_count)
        {
            int32 buffer_index = 0;
            TileType type = NONE;
            Direction direction = NO_DIRECTION;
            fread(&buffer_index, 4, 1, file);
            fread(&type, 1, 1, file);
            fread(&direction, 1, 1, file);

            next_world_state.buffer[buffer_index] = (uint8)type;
            next_world_state.buffer[buffer_index + 1] = (uint8)direction;
        }
    }
}

Camera loadCameraInfo(FILE* file, bool use_alt_camera)
{
    Camera out_camera = {0};

    int32 positions[64] = {0};
    char tag[4] = {0}; 
    if (use_alt_camera) memcpy(&tag, &ALT_CAMERA_CHUNK_TAG, sizeof(tag));
    else		    memcpy(&tag, &MAIN_CAMERA_CHUNK_TAG, sizeof(tag));

    int32 count = getCountAndPositionOfChunk(file, tag, positions);
    if (count != 1) return out_camera;

    fseek(file, positions[0] + 8, SEEK_SET);
    fread(&out_camera.coords.x, 4, 1, file);
    fread(&out_camera.coords.y, 4, 1, file);
    fread(&out_camera.coords.z, 4, 1, file);
    fread(&out_camera.fov, 4, 1, file);
    fread(&out_camera.yaw, 4, 1, file);
    fread(&out_camera.pitch, 4, 1, file);

    return out_camera;
}

void loadWinBlockPaths(FILE* file)
{
    int32 positions[64] = {0};
    int32 count = getCountAndPositionOfChunk(file, WIN_BLOCK_CHUNK_TAG, positions);

    FOR(wb_index_file, count)
    {
        fseek(file, positions[wb_index_file] + 8, SEEK_SET); // skip tag + size

        int32 x, y, z;
        char path[64];
        if (fread(&x, 4, 1, file) != 1) return;
        if (fread(&y, 4, 1, file) != 1) return;
        if (fread(&z, 4, 1, file) != 1) return;
        if (fread(&path, 1, 64, file) != 64) return;
        path[63] = '\0';

        FOR(wb_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* wb = &next_world_state.win_blocks[wb_index];
            if (wb->coords.x == x && wb->coords.y == y && wb->coords.z == z)
            {
                memcpy(wb->next_level, path, sizeof(wb->next_level));
                break;
            }
        }
    }
}

void loadLockedInfoPaths(FILE* file)
{
    int32 positions[64] = {0};
    int32 count = getCountAndPositionOfChunk(file, LOCKED_INFO_CHUNK_TAG, positions);

    FOR(locked_index_file, count)
    {
        fseek(file, positions[locked_index_file] + 8, SEEK_SET); // skip tag + size

        int32 x, y, z;
        char path[64];
        if (fread(&x, 4, 1, file) != 1) return;
        if (fread(&y, 4, 1, file) != 1) return;
        if (fread(&z, 4, 1, file) != 1) return;
        if (fread(&path, 1, 64, file) != 64) return;
        path[63] = '\0';

        Entity* entity_group[6] = {next_world_state.boxes, next_world_state.mirrors, next_world_state.locked_blocks, next_world_state.glass_blocks, next_world_state.sources, next_world_state.win_blocks};
        FOR(group_index, 6)
        {
            FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
            {
                Entity* e = &entity_group[group_index][entity_index];
                if (e->coords.x == x && e->coords.y == y && e->coords.z == z)
                {
                    memcpy(e->unlocked_by, path, sizeof(e->unlocked_by));
                    break;
                }
            }
        }
    }
}

int32 findNextFreeInResetBlock(Entity* rb)
{
    FOR(to_reset_index, MAX_RESET_COUNT) if (rb->reset_info[to_reset_index].id == -1) return to_reset_index;
    return -1;
}


void loadResetBlockInfo(FILE* file)
{
    int32 positions[64] = {0};
    int32 rb_count = getCountAndPositionOfChunk(file, RESET_INFO_CHUNK_TAG, positions);

    FOR(rb_index_file, rb_count)
    {
        fseek(file, positions[rb_index_file] + 4, SEEK_SET);

        int32 size = 0;
        fread(&size, 4, 1, file);
        int32 reset_entity_count = (size/4 - 3) / RESET_INFO_SINGLE_ENTRY_SIZE;

        Int3 rb_coords = {0};
        fread(&rb_coords.x, 4, 1, file);
        fread(&rb_coords.y, 4, 1, file);
        fread(&rb_coords.z, 4, 1, file);

        FOR(rb_index_state, MAX_ENTITY_INSTANCE_COUNT)
        {
            if (int3IsEqual(rb_coords, next_world_state.reset_blocks[rb_index_state].coords))
            {
                Entity* rb = getEntityAtCoords(rb_coords);
                FOR(reset_entity_index, reset_entity_count)
                {
                    int32 stored_id = -1;
                    Int3 reset_start_coords = {0};
                    TileType reset_entity_type = NONE;
                    Direction reset_entity_direction = NO_DIRECTION;
                    fread(&stored_id, 4, 1, file);
                    fread(&reset_start_coords.x, 4, 1, file);
                    fread(&reset_start_coords.y, 4, 1, file);
                    fread(&reset_start_coords.z, 4, 1, file);
                    fread(&reset_entity_type, 4, 1, file);
                    fread(&reset_entity_direction, 4, 1, file);

                    Entity* reset_e = getEntityFromId(stored_id);
                    if (reset_e == 0 && stored_id >= 0)
                    {
                        // entity was removed, recreate it as removed
                        Entity* entity_group = 0;
                        Color color = NO_COLOR;
                        int32 switch_value = (stored_id / 100) * 100;
                        if      (switch_value == ID_OFFSET_BOX)          entity_group = next_world_state.boxes;
                        else if (switch_value == ID_OFFSET_MIRROR)       entity_group = next_world_state.mirrors;
                        else if (switch_value == ID_OFFSET_GLASS)        entity_group = next_world_state.glass_blocks;
                        else if (switch_value >= ID_OFFSET_SOURCE && switch_value < ID_OFFSET_WIN_BLOCK)
                        {
                            entity_group = next_world_state.sources;
                            color = (stored_id - ID_OFFSET_SOURCE) / 100;
                        }
                        else if (switch_value == ID_OFFSET_WIN_BLOCK)    entity_group = next_world_state.win_blocks;
                        else if (switch_value == ID_OFFSET_LOCKED_BLOCK) entity_group = next_world_state.locked_blocks;
                        else if (switch_value == ID_OFFSET_RESET_BLOCK)  entity_group = next_world_state.reset_blocks;

                        if (entity_group)
                        {
                            FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
                            {
                                if (entity_group[entity_index].id != -1) continue;
                                entity_group[entity_index].id = stored_id;
                                entity_group[entity_index].coords = reset_start_coords;
                                entity_group[entity_index].position_norm = intCoordsToNorm(reset_start_coords);
                                entity_group[entity_index].direction = reset_entity_direction;
                                entity_group[entity_index].rotation_quat = directionToQuaternion(reset_entity_direction, true);
                                entity_group[entity_index].color = color;
                                entity_group[entity_index].removed = true;
                                entity_group[entity_index].unlocked_by[0] = '\0';
                                reset_e = &entity_group[entity_index];
                                break;
                            }
                        }
                    }

                    rb->reset_info[reset_entity_index].id = (reset_e != 0) ? reset_e->id : -1;
                    rb->reset_info[reset_entity_index].start_coords = reset_start_coords;
                    rb->reset_info[reset_entity_index].start_type = reset_entity_type;
                    rb->reset_info[reset_entity_index].start_direction = reset_entity_direction;
                }
                break;
            }
        }
    }
}

// keep level_dim in later versions also. can then just write buffer_index rather than the full coords and backsolve on load.
void writeBufferToFile(FILE* file, int32 version)
{
    if (version == 0)
    {
        fwrite(next_world_state.buffer, 1, level_dim.x*level_dim.y*level_dim.z * 2, file);
    }
    else if (version == 1)
    {
        int32 tile_count = 0;

		fseek(file, 8, SEEK_SET); // leave space for tile_count

        for (int32 buffer_index = 0; buffer_index < level_dim.x*level_dim.y*level_dim.z * 2; buffer_index += 2)
        {
            if (next_world_state.buffer[buffer_index] == NONE) continue;
            TileType type = (int8)next_world_state.buffer[buffer_index];
            Direction direction = (int8)next_world_state.buffer[buffer_index + 1];
            fwrite(&buffer_index, 4, 1, file); // write buffer_index, backsolve coords from level dims on decompression
            fwrite(&type, 1, 1, file);
            fwrite(&direction, 1, 1, file);
            tile_count++;
        }

		fseek(file, 4, SEEK_SET); // seek back to after level_dims
        fwrite(&tile_count, 4, 1, file);
        fseek(file, (8 + (tile_count * 6)), SEEK_SET); // set seek to end of tiles
    }
}

void writeCameraToFile(FILE* file, Camera* in_camera, bool write_alt_camera)
{
    char tag[4] = {0};
    if (write_alt_camera) memcpy(&tag, ALT_CAMERA_CHUNK_TAG, sizeof(tag));
    else 				  memcpy(&tag, MAIN_CAMERA_CHUNK_TAG, sizeof(tag));

    fwrite(tag, 4, 1, file);
    fwrite(&CAMERA_CHUNK_SIZE, 4, 1, file);
    fwrite(&in_camera->coords.x, 4, 1, file);
    fwrite(&in_camera->coords.y, 4, 1, file);
    fwrite(&in_camera->coords.z, 4, 1, file);
    fwrite(&in_camera->fov, 4, 1, file);
    fwrite(&in_camera->yaw, 4, 1, file);
    fwrite(&in_camera->pitch, 4, 1, file);
}

void writeWinBlockToFile(FILE* file, Entity* wb)
{
    fwrite(WIN_BLOCK_CHUNK_TAG, 4, 1, file);
    fwrite(&WIN_BLOCK_CHUNK_SIZE, 4, 1, file);
    fwrite(&wb->coords.x, 4, 1, file);
    fwrite(&wb->coords.y, 4, 1, file);
    fwrite(&wb->coords.z, 4, 1, file);
    char next_level[64] = {0};
    memcpy(next_level, wb->next_level, 63);
    fwrite(next_level, 1, 64, file);
}

void writeLockedInfoToFile(FILE* file, Entity* e)
{
    fwrite(LOCKED_INFO_CHUNK_TAG, 4, 1, file);
    fwrite(&LOCKED_INFO_CHUNK_SIZE, 4, 1, file);
    fwrite(&e->coords.x, 4, 1, file);
    fwrite(&e->coords.y, 4, 1, file);
    fwrite(&e->coords.z, 4, 1, file);
    char unlocked_by[64] = {0};
    memcpy(unlocked_by, e->unlocked_by, 63);
    fwrite(unlocked_by, 1, 64, file);
}

// reset blocks currently aren't used in the game. they were a block that could reset individual blocks to their start positions when interacted with. 
// this was used in the overworld, but there's now a very different structure to the overworld which means that this is hardly necessary as a mechanic

// if save_reset_block_state, save current positions of reset block as their new homes. otherwise, save their position to file, but keep their old homes
void writeResetInfoToFile(FILE* file, Entity* rb, bool save_reset_block_state)
{
    int32 count = 0;
    FOR(to_reset_index, MAX_RESET_COUNT)
    {
        if (rb->reset_info[to_reset_index].id == -1) continue;
        else count++;
    }
    if (count == 0) return;

	fwrite(RESET_INFO_CHUNK_TAG, 4, 1, file);

    int32 size = 4 * (3 + (RESET_INFO_SINGLE_ENTRY_SIZE * count));
    fwrite(&size, 4, 1, file);

    fwrite(&rb->coords.x, 4, 1, file);
    fwrite(&rb->coords.y, 4, 1, file);
    fwrite(&rb->coords.z, 4, 1, file);

    FOR(to_reset_index, MAX_RESET_COUNT)
    {
        if (rb->reset_info[to_reset_index].id == -1) continue;

        int32 id_to_write = rb->reset_info[to_reset_index].id;
        fwrite(&id_to_write, 4, 1, file);

        if (save_reset_block_state)
        {
            Entity* e = getEntityFromId(id_to_write);
            if (e)
            {
                TileType type = getTileType(e->coords);
                fwrite(&e->coords.x, 4, 1, file);
                fwrite(&e->coords.y, 4, 1, file);
                fwrite(&e->coords.z, 4, 1, file);
                fwrite(&type, 4, 1, file);
                fwrite(&e->direction, 4, 1, file);
            }
            else
            {
                fwrite(&rb->reset_info[to_reset_index].start_coords.x, 4, 1, file);
                fwrite(&rb->reset_info[to_reset_index].start_coords.y, 4, 1, file);
                fwrite(&rb->reset_info[to_reset_index].start_coords.z, 4, 1, file);
                fwrite(&rb->reset_info[to_reset_index].start_type, 4, 1, file);
                fwrite(&rb->reset_info[to_reset_index].start_direction, 4, 1, file);
            }
        }
        else
        {
            fwrite(&rb->reset_info[to_reset_index].start_coords.x, 4, 1, file);
            fwrite(&rb->reset_info[to_reset_index].start_coords.y, 4, 1, file);
            fwrite(&rb->reset_info[to_reset_index].start_coords.z, 4, 1, file);
            fwrite(&rb->reset_info[to_reset_index].start_type, 4, 1, file);
            fwrite(&rb->reset_info[to_reset_index].start_direction, 4, 1, file);
        }
    }
}

// doesn't change the camera
bool saveLevelRewrite(char* path, bool save_reset_block_state)
{
    FILE* file = fopen(path, "wb");
    if (!file) return false;

    fwrite(&SAVE_WRITE_VERSION, 1, 1, file);
    uint8 x, y, z;
    x = (uint8)level_dim.x;
    y = (uint8)level_dim.y;
    z = (uint8)level_dim.z;
    fwrite(&x, 1, 1, file);
    fwrite(&y, 1, 1, file);
    fwrite(&z, 1, 1, file);

    writeBufferToFile(file, SAVE_WRITE_VERSION);
    writeCameraToFile(file, &saved_main_camera, false);
    if (saved_alt_camera.fov != 0) writeCameraToFile(file, &saved_alt_camera, true);

    FOR(win_block_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        Entity* wb = &next_world_state.win_blocks[win_block_index];
        if (wb->removed) continue;
        if (wb->next_level[0] == '\0') continue;
        writeWinBlockToFile(file, wb);
    }

    Entity* entity_group[6] = {next_world_state.boxes, next_world_state.mirrors, next_world_state.locked_blocks, next_world_state.glass_blocks, next_world_state.sources, next_world_state.win_blocks};
    FOR(group_index, 6)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* e = &entity_group[group_index][entity_index];
            if (e->removed) continue;
            if (e->unlocked_by[0] == '\0') continue;
            writeLockedInfoToFile(file, e);
        }
    }

    FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        Entity* rb = &next_world_state.reset_blocks[entity_index];
        if (rb->removed) continue;
        writeResetInfoToFile(file, rb, save_reset_block_state);
    }

    fclose(file);
    return true;
}

int32 findInSolvedLevels(char level[64])
{
    if (level[0] == '\0') return INT32_MAX; // if NULL string passed, return large number
    FOR(level_index, MAX_LEVEL_COUNT) if (strcmp(next_world_state.solved_levels[level_index], level) == 0) return level_index;
    return -1;
}

int32 nextFreeInSolvedLevels(char (*solved_levels)[64][64])
{
    FOR(solved_level_index, MAX_LEVEL_COUNT) if ((*solved_levels)[solved_level_index][0] == 0) return solved_level_index;
    return -1;
}

void addToSolvedLevels(char level[64])
{
    int32 next_free = nextFreeInSolvedLevels(&next_world_state.solved_levels);
    if (next_free == -1) return; // no free space (should not happen)
    strcpy(next_world_state.solved_levels[next_free], level);
}

void removeFromSolvedLevels(char level[64])
{
    int32 index = findInSolvedLevels(level);
    if (index == -1 || index > MAX_LEVEL_COUNT) return; // not in solved levels, or null string passed
    memset(next_world_state.solved_levels[index], 0, sizeof(next_world_state.solved_levels[0]));
}

void loadSolvedLevelsFromFile()
{
    memset(next_world_state.solved_levels, 0, sizeof(next_world_state.solved_levels));
	FILE* file = fopen(solved_level_path, "rb+");
    FOR(level_index, MAX_LEVEL_COUNT)
    {
        if (fread(next_world_state.solved_levels[level_index], 64, 1, file) != 1) break;
        if (next_world_state.solved_levels[level_index][0] == 0) break;
    }
    fclose(file);
}

void writeSolvedLevelsToFile()
{
	FILE* file = fopen(solved_level_path, "wb");
    if (!file) return;
    FOR(level_index, MAX_LEVEL_COUNT)
    {
		if (next_world_state.solved_levels[level_index][0] == 0) break;
        fwrite(&next_world_state.solved_levels[level_index], 64, 1, file);
    }
    fclose(file);
}

void clearSolvedLevels()
{
	FILE* file = fopen(solved_level_path, "wb");
    fclose(file);
    memset(next_world_state.solved_levels, 0, sizeof(next_world_state.solved_levels));
}

// DRAW ASSET

AssetType assetAtlas(SpriteId id)
{
    return (id < (ASSET_COUNT - SPRITE_2D_FONT_LAST)) ? SPRITE_2D : CUBE_3D;
}

SpriteId getSprite2DId(TileType tile)
{
    switch (tile)
    {
        case NONE:         return NO_ID;
        case VOID:         return SPRITE_2D_VOID;
        case GRID:         return SPRITE_2D_GRID;
        case WALL:         return SPRITE_2D_WALL;
        case BOX:          return SPRITE_2D_BOX;
        case PLAYER:       return SPRITE_2D_PLAYER;
        case MIRROR:       return SPRITE_2D_MIRROR;
        case GLASS:        return SPRITE_2D_GLASS;
        case PACK:    	   return SPRITE_2D_PACK;
        case WATER:        return SPRITE_2D_WATER;
        case WIN_BLOCK:    return SPRITE_2D_WIN_BLOCK;
        case LOCKED_BLOCK: return SPRITE_2D_LOCKED_BLOCK;
        case RESET_BLOCK:  return SPRITE_2D_RESET_BLOCK;
        case LADDER:	   return SPRITE_2D_LADDER;

        case SOURCE_RED:	 return SPRITE_2D_SOURCE_RED;
        case SOURCE_GREEN:	 return SPRITE_2D_SOURCE_GREEN;
        case SOURCE_BLUE:	 return SPRITE_2D_SOURCE_BLUE;
        case SOURCE_MAGENTA: return SPRITE_2D_SOURCE_MAGENTA;
        case SOURCE_YELLOW:	 return SPRITE_2D_SOURCE_YELLOW;
        case SOURCE_CYAN:	 return SPRITE_2D_SOURCE_CYAN;
        case SOURCE_WHITE:	 return SPRITE_2D_SOURCE_WHITE;
        default: return 0;
    }
}

SpriteId getCube3DId(TileType tile)
{
    switch (tile)
    {
        case NONE:         return NO_ID;
        case VOID:         return CUBE_3D_VOID;
        case GRID:         return CUBE_3D_GRID;
        case WALL:         return CUBE_3D_WALL;
        case BOX:          return CUBE_3D_BOX;
        case PLAYER:       return CUBE_3D_PLAYER;
        case MIRROR:       return CUBE_3D_MIRROR;
        case GLASS:        return CUBE_3D_GLASS;
        case PACK:    	   return CUBE_3D_PACK;
        case WATER:        return CUBE_3D_WATER;
        case WIN_BLOCK:    return CUBE_3D_WIN_BLOCK;
        case LOCKED_BLOCK: return CUBE_3D_LOCKED_BLOCK;
        case RESET_BLOCK:  return CUBE_3D_RESET_BLOCK;
        case LADDER: 	   return CUBE_3D_LADDER;
        case WON_BLOCK:    return CUBE_3D_WON_BLOCK;

        case LASER_RED:     return CUBE_3D_LASER_RED;
        case LASER_GREEN:	return CUBE_3D_LASER_GREEN;
        case LASER_BLUE:	return CUBE_3D_LASER_BLUE;
        case LASER_MAGENTA:	return CUBE_3D_LASER_MAGENTA;
        case LASER_YELLOW:	return CUBE_3D_LASER_YELLOW;
        case LASER_CYAN:	return CUBE_3D_LASER_CYAN;
        case LASER_WHITE:	return CUBE_3D_LASER_WHITE;

        case SOURCE_RED:	 return CUBE_3D_SOURCE_RED;
        case SOURCE_GREEN:	 return CUBE_3D_SOURCE_GREEN;
        case SOURCE_BLUE:	 return CUBE_3D_SOURCE_BLUE;
        case SOURCE_MAGENTA: return CUBE_3D_SOURCE_MAGENTA;
        case SOURCE_YELLOW:	 return CUBE_3D_SOURCE_YELLOW;
        case SOURCE_CYAN:	 return CUBE_3D_SOURCE_CYAN;
        case SOURCE_WHITE:	 return CUBE_3D_SOURCE_WHITE;
        default: return 0;
    }
}

SpriteId getModelId(TileType tile)
{
    switch (tile)
    {
        case NONE:         return NO_ID;
        case VOID:         return MODEL_3D_VOID;
        case GRID:         return MODEL_3D_GRID;
        case WALL:         return MODEL_3D_WALL;
        case BOX:          return MODEL_3D_BOX;
        case PLAYER:       return MODEL_3D_PLAYER;
        case MIRROR:       return MODEL_3D_MIRROR;
        case GLASS:        return MODEL_3D_GLASS;
        case PACK:    	   return MODEL_3D_PACK;
        case WATER:        return MODEL_3D_WATER;
        case WIN_BLOCK:    return MODEL_3D_WIN_BLOCK;
        case LOCKED_BLOCK: return MODEL_3D_LOCKED_BLOCK;
        case RESET_BLOCK:  return MODEL_3D_RESET_BLOCK;
        case LADDER: 	   return MODEL_3D_LADDER;
        case WON_BLOCK:    return MODEL_3D_WON_BLOCK;

        case LASER_RED:     return MODEL_3D_LASER_RED;
        case LASER_GREEN:	return MODEL_3D_LASER_GREEN;
        case LASER_BLUE:	return MODEL_3D_LASER_BLUE;
        case LASER_MAGENTA:	return MODEL_3D_LASER_MAGENTA;
        case LASER_YELLOW:	return MODEL_3D_LASER_YELLOW;
        case LASER_CYAN:	return MODEL_3D_LASER_CYAN;
        case LASER_WHITE:	return MODEL_3D_LASER_WHITE;

        case SOURCE_RED:	 return MODEL_3D_SOURCE_RED;
        case SOURCE_GREEN:	 return MODEL_3D_SOURCE_GREEN;
        case SOURCE_BLUE:	 return MODEL_3D_SOURCE_BLUE;
        case SOURCE_MAGENTA: return MODEL_3D_SOURCE_MAGENTA;
        case SOURCE_YELLOW:	 return MODEL_3D_SOURCE_YELLOW;
        case SOURCE_CYAN:	 return MODEL_3D_SOURCE_CYAN;
        case SOURCE_WHITE:	 return MODEL_3D_SOURCE_WHITE;
        default: return 0;
    }
}

void drawAsset(SpriteId id, AssetType type, Vec3 coords, Vec3 scale, Vec4 rotation, Vec3 color)
{
    if (id < 0) return;
    DrawCommand* command = &draw_commands[draw_command_count++];
    command->sprite_id = id;
    command->type = type;
    command->coords = coords;
    command->scale = scale;
    command->rotation = rotation;
    command->color = color;
}

// uses color.x as alpha channel.
void drawText(char* string, Vec2 coords, float scale, float alpha)
{
    float pen_x = coords.x;
    float pen_y = coords.y;
    float aspect = (float)FONT_CELL_WIDTH_PX / (float)FONT_CELL_HEIGHT_PX;
    for (char* pointer = string; *pointer; pointer++)
    {
        unsigned char character = *pointer;
        if (character == '\n')
        {
            pen_x = coords.x;
            pen_y += FONT_CELL_HEIGHT_PX * scale; 
            continue;
        }
        if (character < FONT_FIRST_ASCII || character > FONT_LAST_ASCII) character = '?';

        SpriteId id = (SpriteId)(SPRITE_2D_FONT_SPACE + (character - 32));
        Vec3 draw_coords = { pen_x, pen_y, 0};
        Vec3 draw_scale = { scale * aspect, scale, 1};
        Vec3 color = { alpha, 0.0f, 0.0f };
        drawAsset(id, SPRITE_2D, draw_coords, draw_scale, IDENTITY_QUATERNION, color);
        pen_x += scale * aspect;
    }
}

void createDebugText(char* string)
{
    if (debug_text_count >= MAX_DEBUG_TEXT_COUNT) return;
    memcpy(debug_text_buffer[debug_text_count], string, 256);
    debug_text_count++;
}

void createDebugPopup(char* string, PopupType popup_type)
{
    // check if such a type already exists, and if so just overwrite it with renewed timer. recalculate x coord but not y
    if (popup_type != NO_TYPE)
    {
        FOR(popup_index, MAX_DEBUG_POPUP_COUNT)
        {
            if (debug_popups[popup_index].frames_left == 0) continue;
            if (debug_popups[popup_index].type == popup_type)
            {
                FOR(string_index, 64) if (string[string_index] == '\0') 
                {
                    debug_popups[popup_index].coords.x = debug_popup_start_coords.x - (((float)string_index / 2) * DEFAULT_TEXT_SCALE * ((float)FONT_CELL_WIDTH_PX / (float)FONT_CELL_HEIGHT_PX));
                    break;
                }
                debug_popups[popup_index].frames_left = DEFAULT_POPUP_TIME;
                memcpy(debug_popups[popup_index].text, string, 64 * sizeof(char));
                return;
            }
        }
    }

    // no such type exists, or it has no type, so proceed with looking up into next free
    int32 next_free_in_popups = 0;
    FOR(popup_index, MAX_DEBUG_POPUP_COUNT) if (debug_popups[popup_index].frames_left == 0)
    {
        next_free_in_popups = popup_index;
        break;
    }

    FOR(string_index, 64) if (string[string_index] == '\0') 
    {
        debug_popups[next_free_in_popups].coords.x = debug_popup_start_coords.x - (((float)string_index / 2) * DEFAULT_TEXT_SCALE * ((float)FONT_CELL_WIDTH_PX / (float)FONT_CELL_HEIGHT_PX));
        break;
    }
    debug_popups[next_free_in_popups].coords.y = debug_popup_start_coords.y + (next_free_in_popups * DEBUG_POPUP_STEP_SIZE);
    debug_popups[next_free_in_popups].frames_left = DEFAULT_POPUP_TIME;
    debug_popups[next_free_in_popups].type = popup_type;
    memcpy(debug_popups[next_free_in_popups].text, string, 64 * sizeof(char));
}

void createTutorialPopup()
{

}

// CAMERA STUFF 

Vec4 buildCameraQuaternion(Camera input_camera)
{
    Vec4 quaternion_yaw   = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), input_camera.yaw);
    Vec4 quaternion_pitch = quaternionFromAxisAngle(intCoordsToNorm(AXIS_X), input_camera.pitch);
    return quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));
}

// assumes looking toward plane (otherwise negative t value)
// some of the double conversions here probably aren't required
Vec3 cameraLookingAtPointOnPlane(Camera input_camera, float plane_y)
{
    Vec3 forward = vec3RotateByQuaternion((intCoordsToNorm(int3Negate(AXIS_Z))), buildCameraQuaternion(input_camera));
    double t = ((double)plane_y - (double)input_camera.coords.y) / (double)forward.y;
    return (Vec3){
        (float)((double)input_camera.coords.x + (double)forward.x * t),
        (float)((double)input_camera.coords.y + (double)forward.y * t),
        (float)((double)input_camera.coords.z + (double)forward.z * t)
    };
}

Camera lerpCamera(Camera a, Camera b, float t, float target_plane_y)
{
    Camera result = {0};
    result.coords.x = a.coords.x + (b.coords.x - a.coords.x) * t;
    result.coords.y = a.coords.y + (b.coords.y - a.coords.y) * t;
    result.coords.z = a.coords.z + (b.coords.z - a.coords.z) * t;
    result.fov      = a.fov      + (b.fov      - a.fov)      * t;

    Vec3 target_a = cameraLookingAtPointOnPlane(a, target_plane_y);
    Vec3 target_b = cameraLookingAtPointOnPlane(b, target_plane_y);
    Vec3 target = vec3Add(target_a, vec3ScalarMultiply(vec3Subtract(target_b, target_a), t));

    // build rotation from looked at point
    Vec3 forward = vec3Normalize(vec3Subtract(target, result.coords));
    result.yaw   = (float)atan2((double)-forward.x, (double)-forward.z);
    result.pitch = (float)asin((double)forward.y);
    result.rotation = buildCameraQuaternion(result);

    return result;
}

// RAYCAST ALGORITHM FOR EDITOR

RaycastHit raycastHitCube(Vec3 start, Vec3 direction, float max_distance)
{
    RaycastHit output = {0};
    Int3 current_cube = normCoordsToInt(start);
    start.x += 0.5;
    start.y += 0.5;
    start.z += 0.5;

    // step direction on each axis
    int32 step_x = 0, step_y = 0, step_z = 0;

    if (direction.x > 0.0f)      step_x = 1;
    else if (direction.x < 0.0f) step_x = -1;
    if (direction.y > 0.0f) 	 step_y = 1;
    else if (direction.y < 0.0f) step_y = -1;
    if (direction.z > 0.0f) 	 step_z = 1;
    else if (direction.z < 0.0f) step_z = -1;

    // get coordinates of next grid plane on each axis
    float next_plane_x = 0.0f, next_plane_y = 0.0f, next_plane_z = 0.0f;

    if (step_x > 0)      next_plane_x = (float)current_cube.x + 1.0f;
    else if (step_x < 0) next_plane_x = (float)current_cube.x;       
    if (step_y > 0)      next_plane_y = (float)current_cube.y + 1.0f;
    else if (step_y < 0) next_plane_y = (float)current_cube.y;       
    if (step_z > 0)      next_plane_z = (float)current_cube.z + 1.0f;
    else if (step_z < 0) next_plane_z = (float)current_cube.z;       

    // distance t at which first plane on axis is hit
    float t_max_x = 0, t_max_y = 0, t_max_z = 0;

    if (step_x != 0) t_max_x = (next_plane_x - start.x) / direction.x;
    else			 t_max_x = 1e12f;
    if (step_y != 0) t_max_y = (next_plane_y - start.y) / direction.y;
    else			 t_max_y = 1e12f;
    if (step_z != 0) t_max_z = (next_plane_z - start.z) / direction.z;
    else			 t_max_z = 1e12f;

    // distance t to traverse an entire plane
    float t_delta_x = 0, t_delta_y = 0, t_delta_z = 0;

    if (step_x != 0) t_delta_x = 1.0f / fabsf(direction.x);
    else             t_delta_x = 1e12f;
    if (step_y != 0) t_delta_y = 1.0f / fabsf(direction.y);
    else             t_delta_y = 1e12f;
    if (step_z != 0) t_delta_z = 1.0f / fabsf(direction.z);
    else             t_delta_z = 1e12f;

    Int3 previous_cube = current_cube;
    float t = 0.0f;

    // main loop from paper
    while (t <= max_distance) 
    {
        if (t_max_x <= t_max_y && t_max_x <= t_max_z)
        {
            previous_cube = current_cube;
            current_cube.x += step_x;
            t = t_max_x;
            t_max_x += t_delta_x;
        }
        else if (t_max_y <= t_max_z)
        {
            previous_cube = current_cube;
            current_cube.y += step_y;
            t = t_max_y;
            t_max_y += t_delta_y;
        }
        else
        {
            previous_cube = current_cube;
            current_cube.z += step_z;
            t = t_max_z;
            t_max_z += t_delta_z;
        }

        if (!intCoordsWithinLevelBounds(current_cube)) continue;

        TileType tile = getTileType(current_cube);
        if (tile != NONE)
        {
            output.hit = true;
            output.hit_coords = current_cube;
            output.place_coords = previous_cube;
            return output;
        }
    }
    return output;
}

void editorPlaceOnlyInstanceOfTile(Entity* entity, Int3 coords, TileType tile, int32 id)
{
    for (int buffer_index = 0; buffer_index < 2 * level_dim.x*level_dim.y*level_dim.z; buffer_index += 2)
    {
        if (next_world_state.buffer[buffer_index] != tile) continue;
        next_world_state.buffer[buffer_index] = NONE;
        next_world_state.buffer[buffer_index + 1] = NORTH;
    }
    entity->coords = coords;
    entity->position_norm = intCoordsToNorm(coords);
    entity->id = id;
    setTileType(editor_state.picked_tile, coords);
    setTileDirection(NORTH, coords);
}

// ANIMATION HELPER 

Int3 getNextCoords(Int3 coords, Direction direction)
{
    switch (direction)
    {
        case NORTH: return int3Add(coords, int3Negate(AXIS_Z)); 
        case WEST:  return int3Add(coords, int3Negate(AXIS_X));
        case SOUTH: return int3Add(coords, AXIS_Z);
        case EAST:  return int3Add(coords, AXIS_X);
        case UP:	return int3Add(coords, AXIS_Y);
        case DOWN:	return int3Add(coords, int3Negate(AXIS_Y));

        default: return (Int3){0};
    }
}

// assumes only one difference (will prioritise unpredictably)
Direction getDirectionFromCoordDiff(Int3 to_coords, Int3 from_coords)
{
    Int3 diff = int3Subtract(from_coords, to_coords);
    if 		(diff.x ==  1) return EAST;
    else if (diff.x == -1) return WEST;
    else if (diff.y ==  1) return UP;
    else if (diff.y == -1) return DOWN;
    else if (diff.z ==  1) return SOUTH;
    else if (diff.z == -1) return NORTH;
    return NO_DIRECTION;
}

int32 getPushableStackSize(Int3 first_entity_coords)
{
    Int3 current_stack_coords = first_entity_coords;
    int32 stack_size = 1;
    FOR(find_stack_size_index, MAX_PUSHABLE_STACK_SIZE)
    {
        current_stack_coords = getNextCoords(current_stack_coords, UP);
        TileType next_tile_type = getTileType(current_stack_coords);
        if (!isPushable(next_tile_type)) break;
        stack_size++;
    }
    return stack_size;
}

// TRAILING HITBOXES

int32 findNextFreeInTrailingHitboxes()
{
    FOR(find_hitbox_index, MAX_TRAILING_HITBOX_COUNT)
    {
        if (trailing_hitboxes[find_hitbox_index].frames != 0) continue;
        return find_hitbox_index;
    }
    return 0;
}

void createTrailingHitbox(Int3 coords, Direction moving_direction, Direction hit_direction, int32 frames, TileType type)
{
    int32 hitbox_index = findNextFreeInTrailingHitboxes();
    trailing_hitboxes[hitbox_index].coords = coords;
    trailing_hitboxes[hitbox_index].hit_direction = hit_direction;
    trailing_hitboxes[hitbox_index].moving_direction = moving_direction;
    trailing_hitboxes[hitbox_index].frames = frames;
    trailing_hitboxes[hitbox_index].type = type;
}

bool trailingHitboxAtCoords(Int3 coords, TrailingHitbox* trailing_hitbox)
{
    FOR(trailing_hitbox_index, MAX_TRAILING_HITBOX_COUNT) 
    {
        TrailingHitbox th = trailing_hitboxes[trailing_hitbox_index];
        if (int3IsEqual(coords, th.coords) && th.frames > 0) 
        {
            *trailing_hitbox = th;
            return true;
        }
    }
    return false;
}

// ANIMATIONS

// returns animation_index and queue_time
int32* findNextFreeInAnimations(int32* next_free_array, int32 entity_id) 
{
    int32 animation_index = -1;
    int32 queue_time = 0;
    for (int find_anim_index = 0; find_anim_index < MAX_ANIMATION_COUNT; find_anim_index++)
    {
        if (animation_index == -1 && animations[find_anim_index].frames_left == 0)
        {
            animation_index = find_anim_index;
            animations[animation_index] = (Animation){0};
        }
        if (animations[find_anim_index].id == entity_id && animations[find_anim_index].frames_left != 0) 
        {
            if (queue_time < animations[find_anim_index].frames_left) queue_time = animations[find_anim_index].frames_left;
        }
    }
    next_free_array[0] = animation_index;
    next_free_array[1] = queue_time;
    return next_free_array;
}

int32 presentInAnimations(int32 entity_id)
{
    int32 frames = 0;
    FOR(find_anim_index, MAX_ANIMATION_COUNT) 
    {
        Animation* a = &animations[find_anim_index];
        if (a->id == entity_id && a->frames_left > frames) frames = a->frames_left; 
    }
    return frames;
}

void zeroAnimations(int32 id) 
{
    FOR(animation_index, MAX_ANIMATION_COUNT) if (animations[animation_index].id == id) memset(&animations[animation_index], 0, sizeof(Animation));
}

// automatically queues if given object is already being moved around. assumes object is entity, because requires id - easily fixable if required. assumes max two animations on any given object (max one queued)
void createInterpolationAnimation(Vec3 position_a, Vec3 position_b, Vec3* position_to_change, Vec4 rotation_a, Vec4 rotation_b, Vec4* rotation_to_change, int32 entity_id, int32 animation_frames)
{
    int32 next_free_array[2] = {0};
    int32* next_free_output = findNextFreeInAnimations(next_free_array, entity_id);
    int32 animation_index = next_free_output[0];
    int32 queue_time = next_free_output[1];

    animations[animation_index].id = entity_id;
    animations[animation_index].frames_left = animation_frames + queue_time; 

    Vec3 translation_per_frame = vec3ScalarMultiply(vec3Subtract(position_b, position_a), (float)(1.0f/animation_frames));

    if (!vec3IsZero(translation_per_frame))
    {
        animations[animation_index].position_to_change = position_to_change;
        for (int frame_index = 0; frame_index < animation_frames; frame_index++)
        {
            animations[animation_index].position[animation_frames-(1+frame_index)] 
            = vec3Add(position_a, vec3ScalarMultiply(translation_per_frame, (float)(1+frame_index)));
        }
    }
    if (!quaternionIsZero(quaternionSubtract(rotation_b, rotation_a)))
    {
        animations[animation_index].rotation_to_change = rotation_to_change;
        if (quaternionDot(rotation_a, rotation_b) < 0.0f) rotation_b = quaternionScalarMultiply(rotation_b, -1.0f);
        for (int frame_index = 0; frame_index < animation_frames; frame_index++)
        {
            float param = (float)(frame_index + 1) / animation_frames;
            animations[animation_index].rotation[animation_frames-(1+frame_index)] 
            = quaternionNormalize(quaternionAdd(quaternionScalarMultiply(rotation_a, 1.0f - param), quaternionScalarMultiply(rotation_b, param)));
        }
    }
}

void createPackRotationAnimation(Vec3 player_position, Vec3 pack_position, Direction pack_direction, bool clockwise, Vec3* position_to_change, Vec4* rotation_to_change, int32 entity_id, int32 animation_frames)
{
    int32 next_free_array[2] = {0};
    int32* next_free_output = findNextFreeInAnimations(next_free_array, entity_id);
    int32 animation_index = next_free_output[0];
    int32 queue_time = next_free_output[1];

    animations[animation_index].id = entity_id;
    animations[animation_index].frames_left = animation_frames + queue_time; 
    animations[animation_index].rotation_to_change = rotation_to_change;
    animations[animation_index].position_to_change = position_to_change;

    Vec3 pivot_point = player_position;
    Vec3 pivot_to_pack_start = vec3Subtract(pack_position, player_position);
    float d_theta_per_frame = (TAU*0.25f)/(float)animation_frames;
    float angle_sign = clockwise ? 1.0f : -1.0f;
    Direction previous_pack_direction = NO_DIRECTION;

    if (clockwise) 
    {
        previous_pack_direction = pack_direction - 1;
        if (previous_pack_direction == NO_DIRECTION) previous_pack_direction = EAST;
    }
    else 
    {
        previous_pack_direction = pack_direction + 1;
        if (previous_pack_direction == UP) previous_pack_direction = NORTH;
    }

    for (int frame_index = 0; frame_index < animation_frames; frame_index++)
    {
        // rotation
        Vec4 quat_prev = directionToQuaternion(oppositeDirection(previous_pack_direction), true);
        Vec4 quat_next = directionToQuaternion(oppositeDirection(pack_direction), true);
        if (quaternionDot(quat_prev, quat_next) < 0.0f) quat_next = quaternionNegate(quat_next); // resolve quat sign issue 
        float param = (float)(frame_index + 1) / (float)(animation_frames);
        animations[animation_index].rotation[animation_frames-(1+frame_index)] 
            = quaternionNormalize(quaternionAdd(quaternionScalarMultiply(quat_prev, 1.0f - param), quaternionScalarMultiply(quat_next, param)));

        // translation
        float theta = angle_sign * (frame_index+1) * d_theta_per_frame;
        Vec4 roll = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), theta);
        Vec3 relative_rotation = vec3RotateByQuaternion(pivot_to_pack_start, roll);
        animations[animation_index].position[animation_frames-(1+frame_index)] = vec3Add(pivot_point, relative_rotation);
    }
}

void createFailedWalkAnimation(Vec3 start_position, Vec3 next_position, Vec3* position_to_change, int32 entity_id)
{
    int32 next_free_array[2] = {0};
    int32* next_free_output = findNextFreeInAnimations(next_free_array, entity_id);
    int32 animation_index = next_free_output[0];
    int32 queue_time = next_free_output[1];

    animations[animation_index].id = entity_id;
    animations[animation_index].frames_left = FAILED_ANIMATION_TIME + queue_time; 
    animations[animation_index].position_to_change = position_to_change;

    Vec3 translation_per_frame = vec3ScalarMultiply(vec3Subtract(next_position, start_position), (float)(1.0f/FAILED_ANIMATION_TIME) / 2);

    for (int frame_index = 0; frame_index < FAILED_ANIMATION_TIME / 2; frame_index++)
    {
        Vec3 position = vec3Add(start_position, vec3ScalarMultiply(translation_per_frame, (float)(1+frame_index)));
        animations[animation_index].position[FAILED_ANIMATION_TIME-(1+frame_index)] = position;
        animations[animation_index].position[1+frame_index] = position;
    }
    animations[animation_index].position[0] = start_position;
}

void doFailedWalkAnimations(Direction direction)
{
    int32 stack_size = getPushableStackSize(next_world_state.player.coords); // counts player as member of stack
    Int3 current_coords = next_world_state.player.coords;
    FOR(stack_index, stack_size) 
    {
        createFailedWalkAnimation(intCoordsToNorm(current_coords), intCoordsToNorm(getNextCoords(current_coords, direction)), &getEntityAtCoords(current_coords)->position_norm, getEntityAtCoords(current_coords)->id);
        current_coords = getNextCoords(current_coords, UP);
    }
    if (!pack_detached) createFailedWalkAnimation(intCoordsToNorm(next_world_state.pack.coords), intCoordsToNorm(getNextCoords(next_world_state.pack.coords, direction)), &next_world_state.pack.position_norm, PACK_ID);

    next_world_state.player.moving_direction = NO_DIRECTION;
}

void createFailedStaticRotationAnimation(Vec4 start_rotation, Vec4 input_direction_as_quat, Vec4* rotation_to_change, int32 entity_id)
{
    int32 next_free_array[2] = {0};
    int32* next_free_output = findNextFreeInAnimations(next_free_array, entity_id);
    int32 animation_index = next_free_output[0];
    int32 queue_time = next_free_output[1];

    animations[animation_index].id = entity_id;
    animations[animation_index].frames_left = FAILED_ANIMATION_TIME + queue_time; 
    animations[animation_index].rotation_to_change = rotation_to_change;

    if (quaternionDot(start_rotation, input_direction_as_quat) < 0.0f) input_direction_as_quat = quaternionScalarMultiply(input_direction_as_quat, -1.0f);
    for (int frame_index = 0; frame_index < FAILED_ANIMATION_TIME / 2; frame_index++)
    {
        // similar interpolation for loop, but fill both the end and start of the array, so it bounces back
        float param = ((float)(frame_index + 1) / FAILED_ANIMATION_TIME) / 2;
        Vec4 rotation = quaternionNormalize(quaternionAdd(quaternionScalarMultiply(start_rotation, 1.0f - param), quaternionScalarMultiply(input_direction_as_quat, param)));
        animations[animation_index].rotation[FAILED_ANIMATION_TIME - (1+frame_index)] = rotation;
        animations[animation_index].rotation[1+frame_index] = rotation;
    }
    animations[animation_index].rotation[0] = start_rotation;
}

void createFailedPackRotationAnimation(Vec3 player_position, Vec3 pack_position, Direction pack_direction, bool clockwise, Vec3* position_to_change, Vec4* rotation_to_change, int32 entity_id)
{
    int32 next_free_array[2] = {0};
    int32* next_free_output = findNextFreeInAnimations(next_free_array, entity_id);
    int32 animation_index = next_free_output[0];
    int32 queue_time = next_free_output[1];

    animations[animation_index].id = entity_id;
    animations[animation_index].frames_left = FAILED_ANIMATION_TIME + queue_time; 
    animations[animation_index].rotation_to_change = rotation_to_change;
    animations[animation_index].position_to_change = position_to_change;

    Vec3 pivot_point = player_position;
    Vec3 pivot_to_pack_start = vec3Subtract(pack_position, player_position);
    float d_theta_per_frame = (TAU*0.25f)/(float)TURN_ANIMATION_TIME;
    float angle_sign = clockwise ? 1.0f : -1.0f;
    Direction previous_pack_direction = NORTH;
    if (clockwise) 
    {
        previous_pack_direction = pack_direction - 1;
        if (previous_pack_direction == -1) previous_pack_direction = EAST;
    }
    else 
    {
        previous_pack_direction = pack_direction + 1;
        if (previous_pack_direction == 4) previous_pack_direction = NORTH;
    }

    for (int frame_index = 0; frame_index < FAILED_ANIMATION_TIME / 2; frame_index++)
    {
        // rotation
        Vec4 quat_prev = directionToQuaternion(oppositeDirection(previous_pack_direction), true);
        Vec4 quat_next = directionToQuaternion(oppositeDirection(pack_direction), true);
        if (quaternionDot(quat_prev, quat_next) < 0.0f) quat_next = quaternionNegate(quat_next); // resolve quat sign issue 
        float param = ((float)(frame_index + 1) / FAILED_ANIMATION_TIME) / 2;
        Vec4 rotation = quaternionNormalize(quaternionAdd(quaternionScalarMultiply(quat_prev, 1.0f - param), quaternionScalarMultiply(quat_next, param)));
        animations[animation_index].rotation[FAILED_ANIMATION_TIME-(1+frame_index)] = rotation;
        animations[animation_index].rotation[1+frame_index] = rotation;

        // translation
        float theta = (angle_sign * (frame_index+1) * d_theta_per_frame) / 2;
        Vec4 roll = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), theta);
        Vec3 relative_rotation = vec3RotateByQuaternion(pivot_to_pack_start, roll);
        animations[animation_index].position[FAILED_ANIMATION_TIME-(1+frame_index)] = vec3Add(pivot_point, relative_rotation);
        animations[animation_index].position[1+frame_index] = vec3Add(pivot_point, relative_rotation);
    }
    animations[animation_index].rotation[0] = directionToQuaternion(oppositeDirection(previous_pack_direction), true);
    animations[animation_index].position[0] = pack_position;
}

void doFailedTurnAnimations(Direction input_direction, bool clockwise)
{
    int32 stack_size = getPushableStackSize(next_world_state.player.coords);
    Int3 current_coords = next_world_state.player.coords;
    FOR(stack_index, stack_size)
    {
        Entity* current_entity = getEntityAtCoords(current_coords);
        Direction next_direction = getNextRotationalDirection(current_entity->direction, clockwise);
        createFailedStaticRotationAnimation(directionToQuaternion(current_entity->direction, true), directionToQuaternion(next_direction, true), &current_entity->rotation_quat, current_entity->id);
        current_coords = getNextCoords(current_coords, UP);
    }
    createFailedPackRotationAnimation(intCoordsToNorm(next_world_state.player.coords), 
            intCoordsToNorm(next_world_state.pack.coords), 
            oppositeDirection(input_direction), clockwise, 
            &next_world_state.pack.position_norm, &next_world_state.pack.rotation_quat, PACK_ID);
}

// hard codes first fall = 12 frames total (8 acceleration, 4 at terminal velocity of 1/8 b/f)
void createFirstFallAnimation(Vec3 start_position, Vec3* position_to_change, int32 entity_id)
{
    int32 next_free_array[2] = {0};
    int32* next_free_output = findNextFreeInAnimations(next_free_array, entity_id);
    int32 animation_index = next_free_output[0];
    int32 queue_time = next_free_output[1];

    animations[animation_index].id = entity_id;
    animations[animation_index].frames_left = 12 + queue_time; 
    animations[animation_index].position_to_change = position_to_change;

    FOR(first_8_frames_index, 8)
    {
        Vec3 delta_y = { 0.0f, -(float)(first_8_frames_index * first_8_frames_index) / 128, 0.0f };
        animations[animation_index].position[12-first_8_frames_index] = vec3Add(start_position, delta_y);
    }
    FOR(next_4_frames_index, 4)
    {
        Vec3 delta_y = { 0.0f, -(float)(next_4_frames_index + 4) / 8, 0.0f };
        animations[animation_index].position[12-(next_4_frames_index+8)] = vec3Add(start_position, delta_y);
    }
    animations[animation_index].position[0] = vec3Add(start_position, vec3Negate(intCoordsToNorm(AXIS_Y)));
}

// PUSH ENTITES

void changeMoving(Entity* e)
{
    if (presentInAnimations(e->id) && e->in_motion == 0 && !(e->moving_direction == NO_DIRECTION)) e->in_motion = presentInAnimations(e->id);
    else if (e->in_motion > 0) e->in_motion--;
    else e->moving_direction = NO_DIRECTION;
}

void resetPlayerAndPackMotion()
{
    next_world_state.player.in_motion = 0;
    next_world_state.player.moving_direction = NO_DIRECTION;
    if (!pack_detached)
    {
        next_world_state.pack.in_motion = 0;
        next_world_state.pack.moving_direction = NO_DIRECTION;
    }
}

void resetFirstFall(Entity* e)
{
    if ((!e->in_motion && getTileType(getNextCoords(e->coords, DOWN)) != NONE) || next_world_state.player.hit_by_blue) e->first_fall_already_done = false;
}

PushResult canPush(Int3 coords, Direction direction)
{
    Int3 current_coords = coords;
    TileType current_tile = getTileType(current_coords);
    for (int push_index = 0; push_index < MAX_ENTITY_PUSH_COUNT; push_index++) 
    {
        Entity* entity = getEntityAtCoords(current_coords);
        if (isEntity(current_tile) && entity->locked) return FAILED_PUSH;

        if (entity->in_motion > 0) return PAUSE_PUSH;
        if (isPushable(getTileType(current_coords)) && getTileType(getNextCoords(current_coords, DOWN)) == NONE && !next_world_state.player.hit_by_blue) return PAUSE_PUSH;

        current_coords = getNextCoords(current_coords, direction);
        current_tile = getTileType(current_coords);

        Int3 coords_ahead = getNextCoords(entity->coords, direction);
        if (isPushable(getTileType(coords_ahead)) && getEntityAtCoords(coords_ahead)->in_motion) return PAUSE_PUSH;
        Int3 coords_below = getNextCoords(entity->coords, DOWN);
        if (isPushable(getTileType(coords_below)) && getEntityAtCoords(coords_below)->in_motion) return PAUSE_PUSH;
        Int3 coords_below_and_ahead = getNextCoords(getNextCoords(entity->coords, DOWN), direction);
        if (isPushable(getTileType(coords_below_and_ahead)) && getEntityAtCoords(coords_below_and_ahead)->in_motion) return PAUSE_PUSH;

        if (!intCoordsWithinLevelBounds(current_coords)) return FAILED_PUSH;

        if (current_tile == NONE) return CAN_PUSH;
        if (current_tile == GRID || current_tile == WALL || current_tile == LADDER ) return FAILED_PUSH;
    }
    return FAILED_PUSH; // only here if hit the max entity push count
}

PushResult canPushStack(Int3 coords, Direction direction)
{
    int32 stack_size = getPushableStackSize(coords);
    Int3 current_coords = coords;
    FOR(stack_index, stack_size)
    {
        PushResult push_result = canPush(current_coords, direction);
        if (push_result == PAUSE_PUSH) return PAUSE_PUSH;
        if(stack_index == 0) if (push_result == FAILED_PUSH) return FAILED_PUSH;
        current_coords = getNextCoords(current_coords, UP);
    }
    return CAN_PUSH;
}

PushResult canPushUp(Int3 coords)
{
	int32 stack_size = getPushableStackSize(coords);
    Int3 check_coords = coords;
    FOR(_, stack_size) check_coords = getNextCoords(check_coords, UP);
    if (getTileType(check_coords) == NONE) return CAN_PUSH;
    else return FAILED_PUSH;
}

Push pushOnceWithoutAnimation(Int3 coords, Direction direction, int32 time)
{
    Push entity_to_push = {0};

    Entity* entity = getEntityAtCoords(coords);
    entity_to_push.type = getTileType(coords);
    entity_to_push.previous_coords = coords;
    entity_to_push.entity = entity; 
    entity_to_push.new_coords = getNextCoords(coords, direction);

    entity->in_motion = time;
    entity->moving_direction = direction;

    setTileType(NONE, entity_to_push.previous_coords);
    setTileDirection(NORTH, entity_to_push.previous_coords);

    setTileType(entity_to_push.type, entity_to_push.new_coords);
    setTileDirection(entity->direction, entity_to_push.new_coords);
    entity->coords = entity_to_push.new_coords;

    return entity_to_push;
}

void pushOnce(Int3 coords, Direction direction, int32 animation_time)
{
    Push entity_to_push = pushOnceWithoutAnimation(coords, direction, animation_time);

    int32 id = getEntityId(entity_to_push.new_coords);
    TileType trailing_hitbox_type = getTileType(entity_to_push.new_coords);
    createInterpolationAnimation(intCoordsToNorm(entity_to_push.previous_coords),
                                 intCoordsToNorm(entity_to_push.new_coords),
                                 &entity_to_push.entity->position_norm,
                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                 id, animation_time); 
    int32 trailing_hitbox_time = (animation_time / 2) + 1;
    createTrailingHitbox(coords, direction, NO_DIRECTION, trailing_hitbox_time, trailing_hitbox_type);
}

// assumes at least the lowest layer of the stack is able to be pushed. checks if next is NONE, if so stops. 
void pushAll(Int3 coords, Direction direction, int32 animation_time, bool animations_on, bool limit_stack_size_to_one)
{
    Int3 current_coords = coords;
    int32 push_size = 0;
    FOR(push_index, MAX_ENTITY_PUSH_COUNT)
    {
        if (getTileType(current_coords) == NONE) break;
        current_coords = getNextCoords(current_coords, direction);
        push_size++;
    }
    current_coords = getNextCoords(current_coords, oppositeDirection(direction));
    for (int32 inverse_push_index = push_size; inverse_push_index != 0; inverse_push_index--)
    {
        int32 stack_size = getPushableStackSize(current_coords);
        Int3 current_stack_coords = current_coords;
        FOR(stack_index, stack_size)
        {
            if (getTileType(getNextCoords(current_stack_coords, direction)) != NONE) break;
            if (animations_on) pushOnce(current_stack_coords, direction, animation_time);
            else pushOnceWithoutAnimation(current_stack_coords, direction, animation_time);
            current_stack_coords = getNextCoords(current_stack_coords, UP);
            if (limit_stack_size_to_one && stack_index == 0) break;
        }
        current_coords = getNextCoords(current_coords, oppositeDirection(direction));
    }
}

void pushUp(Int3 coords, int32 animation_time)
{
    int32 stack_size = getPushableStackSize(coords);
    Int3 current_coords = coords;
    FOR(_, stack_size - 1) current_coords = getNextCoords(current_coords, UP);
    for (int32 inverse_stack_index = stack_size; inverse_stack_index != 0; inverse_stack_index--)
    {
        TileType tile = getTileType(current_coords);
        Direction dir = getTileDirection(current_coords);
        Entity* e = getEntityAtCoords(current_coords);
        Int3 coords_above = getNextCoords(current_coords, UP);
        moveEntityInBufferAndState(e, coords_above, dir);
        createInterpolationAnimation(intCoordsToNorm(current_coords), intCoordsToNorm(coords_above), &e->position_norm,
                					 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                     e->id, animation_time);
    	createTrailingHitbox(current_coords, UP, NO_DIRECTION, (animation_time / 2) + 1, tile);
        current_coords = getNextCoords(current_coords, DOWN);
    }
}

// assumes 6-dim dir
Direction getNextMirrorState(Direction start_direction, Direction push_direction)
{
    switch (start_direction)
    {
        case NORTH: switch (push_direction)
        {
            case NORTH: return SOUTH;
            case SOUTH: return SOUTH;
            case WEST:  return UP;
            case EAST:  return DOWN;
            default:    return 0;
        }
        case SOUTH: switch (push_direction)
        {
            case NORTH: return NORTH;
            case SOUTH: return NORTH;
            case WEST:  return DOWN;
            case EAST:  return UP;
            default: 	return 0;
        }
        case WEST: switch (push_direction)
        {
            case NORTH: return UP;
            case SOUTH: return DOWN;
            case WEST:  return EAST;
            case EAST:  return EAST;
            default: 	return 0;
        }
        case EAST: switch (push_direction)
        {
            case NORTH: return DOWN;
            case SOUTH: return UP;
            case WEST:  return WEST;
            case EAST:  return WEST;
            default: 	return 0;
        }
        case UP: switch (push_direction)
     	{
        	case NORTH: return EAST;
         	case SOUTH: return WEST;
         	case WEST:  return SOUTH;
         	case EAST:  return NORTH;
         	default: 	return 0;
     	}
        case DOWN: switch (push_direction)
       	{
           	case NORTH: return WEST;
           	case SOUTH: return EAST;
           	case WEST:  return NORTH;
           	case EAST:  return SOUTH;
           	default: 	return 0;
       	}
        default: return 0;
    }
}

// LASERS

Direction getNextLaserDirectionMirror(Direction laser_direction, Direction mirror_direction)
{
    if (mirror_direction <= 4 && laser_direction <= 6) 
    {
        if (laser_direction == mirror_direction) 					return DOWN;
        if (laser_direction == oppositeDirection(mirror_direction)) return UP;
        if (laser_direction == UP) 									return oppositeDirection(mirror_direction);
        if (laser_direction == DOWN) 								return mirror_direction;
    }
    switch (mirror_direction)
    {
        case UP: switch (laser_direction)
     	{
         	case NORTH: return EAST;
         	case SOUTH: return WEST;
         	case WEST:  return SOUTH;
         	case EAST:  return NORTH;
            default: 	return NO_DIRECTION;
     	}
		case DOWN: switch (laser_direction)
       	{
            case NORTH: return WEST;
            case SOUTH: return EAST;
            case WEST:  return NORTH;
            case EAST:  return SOUTH;
            default: 	return NO_DIRECTION;
        }
		default: return NO_DIRECTION;
    }
}

LaserColor colorToLaserColor(Color color)
{
    switch (color)
    {
        case RED:     return (LaserColor){ .red = true,  .green = false, .blue = false };
        case GREEN:   return (LaserColor){ .red = false, .green = true,  .blue = false };
        case BLUE:    return (LaserColor){ .red = false, .green = false, .blue = true  };
        case MAGENTA: return (LaserColor){ .red = true,  .green = false, .blue = true  };
        case YELLOW:  return (LaserColor){ .red = true,  .green = true,  .blue = false };
        case CYAN:    return (LaserColor){ .red = false, .green = true,  .blue = true  };
        case WHITE:   return (LaserColor){ .red = true,  .green = true,  .blue = true  };
        default: return (LaserColor){0};
    }
}

Vec3 colorToRGB(Color color)
{
    switch (color)
    {
        case RED:     return (Vec3){ 1.0f, 0.0f, 0.0f };
        case GREEN:   return (Vec3){ 0.0f, 1.0f, 0.0f };
        case BLUE:    return (Vec3){ 0.0f, 0.0f, 1.0f };
        case MAGENTA: return (Vec3){ 1.0f, 0.0f, 1.0f };
        case YELLOW:  return (Vec3){ 1.0f, 1.0f, 0.0f };
        case CYAN:    return (Vec3){ 0.0f, 1.0f, 1.0f };
        case WHITE:   return (Vec3){ 1.0f, 1.0f, 1.0f };
        default: return VEC3_0;
    }
}

TileType laserColorToType(Color color)
{
    switch (color)
    {
        case RED:     return LASER_RED;
        case GREEN:   return LASER_GREEN;
        case BLUE:    return LASER_BLUE;
        case MAGENTA: return LASER_MAGENTA;
        case YELLOW:  return LASER_YELLOW;
        case CYAN:    return LASER_CYAN;
        case WHITE:   return LASER_WHITE;
        default: return NONE;
    }
}

int32 findNextFreeInLaserBuffer()
{
    FOR(laser_buffer_index, MAX_PSEUDO_SOURCE_COUNT) if (laser_buffer[laser_buffer_index].color == NO_COLOR) return laser_buffer_index;
    return -1;
}

// handles where lasers go. the complicated part of this function handles when mirrors are moving around, and offsets the lasers by visually by a bit.
//
// more detail about the mirror offset calculation:
//
// when a mirror is mid-animation between grid cells, the laser wants to hit at the actual visual position of the mirror, not the interger coords.
// this means that the function needs to adjust the laser segment's end coords (and the next segment's start coords) based on the mirror's displacement.
//
// the displacement of the laser-turning-point can change depending along which axis the mirror is moving relative to the direction of the laser - 
// but the displacement always travels along the 1D line of direction of travel of the laser.
// the mirror can be moving along the incoming laser axis, or the reflected laser axis, or neither. if neither, no displacement is required - otherwise some displacement is required.
//
// the sign of the shift along this 1D line depends on the relative orientations of the incoming laser, reflected laser, and the direction the mirror is facing. so the function compares direction signs:
// guess an offset, check if the resulting geometry is consistent (i.e., incoming and reflected rays point the right way relative to each other), and flip the bit if not.
// this bit is the only information actually required, because the magnitude of the offset can be gotten just from mirror.in_motion and the total amount of time for the movement
// (i.e., how far along the interpolation between one axis and another is the mirror). planning to just use position_norm here instead, which would be better and simpler.
// 

// TODO: - need guard on offset_magnitude in the mirrors: if too close to edge, don't want to allow reflection
// 		 - look if need to round from position_norm instead of checking if player is turning for some calculations; does this handle first falls vs. other falls correctly..?
// 		 - figure out moving sources and their lasers
// 		 - two mirrors moving at once isnt handled.
// 		 - looks weird during some undo interpolation edge cases, because i don't write moving_direction correctly sometimes.

void updateLaserBuffer(void)
{
    Entity* player = &next_world_state.player;

    memset(laser_buffer, 0, sizeof(laser_buffer));

    player->hit_by_red   = false;
    player->hit_by_blue  = false;
    player->green_hit = (GreenHit){0};

    // if a source is a non-primary color, create primary sources of the constituent colors
    Entity sources_as_primary[256] = {0};
    int32 primary_index = 0;
    FOR(source_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        Entity* s = &next_world_state.sources[source_index];
        if (s->removed || s->locked) continue;
		if (s->color < MAGENTA)
        {
            sources_as_primary[primary_index++] = *s;
            continue;
        }
		else if (s->color == MAGENTA)
        {
			sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = RED;
			sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = BLUE;
        }
		else if (s->color == YELLOW)
        {
			sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = RED;
			sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = GREEN;
        }
		else if (s->color == CYAN)
        {
			sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = GREEN;
			sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = BLUE;
        }
		else if (s->color == WHITE)
        {
			sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = RED;
			sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = GREEN;
			sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = BLUE;
        }
    }

    FOR(source_index, MAX_PSEUDO_SOURCE_COUNT)
    {
        Entity* source = &sources_as_primary[source_index];

        Direction current_direction = source->direction;
        Int3 current_tile_coords = source->coords;
        if (source->in_motion)
        {
            // TODO: moving sources should emit their laser based on position_norm, not on integer coords. but i haven't designed these levels yet anyway.
        }

        int32 skip_mirror_id = 0; // because lasers will sometimes be 'between' integer coordinates, it can sometimes hit mirrors twice. so just store the id of the mirror that was hit, and don't let the laser hit it (immediately) again
        int32 skip_next_mirror = 0;
        int32 laser_buffer_start_index = findNextFreeInLaserBuffer();

        FOR(laser_turn_index, MAX_LASER_TURNS_ALLOWED)
        {
            bool no_more_turns = true; 

            LaserBuffer* lb = &laser_buffer[laser_buffer_start_index + laser_turn_index];
            LaserBuffer prev_lb = {0}; 
            if (laser_turn_index != 0)
            {
                prev_lb = laser_buffer[laser_buffer_start_index + laser_turn_index - 1];
                lb->start_coords = prev_lb.end_coords;
            }
            else
            {
                lb->start_coords = intCoordsToNorm(current_tile_coords);
            }
            lb->direction = current_direction;
            lb->color = source->color;

            current_tile_coords = getNextCoords(current_tile_coords, current_direction);

            FOR(laser_tile_index, MAX_LASER_TRAVEL_DISTANCE)
            {
                if (skip_next_mirror > 0) skip_next_mirror--;
                no_more_turns = true;

                if (!intCoordsWithinLevelBounds(current_tile_coords))
                {
                    lb->end_coords = intCoordsToNorm(current_tile_coords);
                    Vec3 dir_basis = directionToVector(current_direction);
                    if (dir_basis.x == 0) lb->end_coords.x = lb->start_coords.x;
                    if (dir_basis.y == 0) lb->end_coords.y = lb->start_coords.y;
                    if (dir_basis.z == 0) lb->end_coords.z = lb->start_coords.z;
                    break;
                }

                TileType real_hit_type = getTileType(current_tile_coords);
                if (real_hit_type == GLASS) real_hit_type = NONE;
                TrailingHitbox th = {0};
                bool th_hit = false;
                if (trailingHitboxAtCoords(current_tile_coords, &th) && th.frames > 0)
                {
                    if (th.hit_direction == NO_DIRECTION || th.hit_direction == current_direction) th_hit = true;
                    if (th.type == GLASS) th_hit = false;
                }
                else memset(&th, 0, sizeof(TrailingHitbox));
                if (th_hit) real_hit_type = th.type;

                if (real_hit_type == PLAYER)
                {
                    bool do_passthrough = false;
                    bool direction_on_axis = false;
                    if (!th_hit && player->in_motion > STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH) do_passthrough = true;
                    if (player->moving_direction == current_direction || player->moving_direction == oppositeDirection(current_direction)) direction_on_axis = true;
                    if (do_passthrough && !direction_on_axis)
                    {
                        current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                        continue;
                    }
                    if (direction_on_axis) lb->end_coords = player->position_norm;
                    else lb->end_coords = intCoordsToNorm(current_tile_coords);
                    LaserColor laser_color = colorToLaserColor(lb->color);
                    if (laser_color.red) player->hit_by_red = true;
                    if (laser_color.green) 
                    {
                        switch (current_direction)
                        {
                            case NORTH: player->green_hit.north = true; break;
                            case WEST:  player->green_hit.west  = true; break;
                            case SOUTH: player->green_hit.south = true; break;
                            case EAST:  player->green_hit.east  = true; break;
                            case UP:    player->green_hit.up    = true; break;
                            case DOWN:  player->green_hit.down  = true; break;
                            default: break;
                        }
                    }
                    if (laser_color.blue)  player->hit_by_blue  = true;
                    break;
                }

                if (real_hit_type == MIRROR)
                {
                    Entity* mirror = {0};
                    if (th_hit) mirror = getEntityAtCoords(getNextCoords(current_tile_coords, th.moving_direction));
                    else mirror = getEntityAtCoords(current_tile_coords);

                    if (skip_next_mirror == 0 || skip_mirror_id != mirror->id)
                    {
                        if (mirror->in_motion)
                        {
                            int32 passthrough_comparison = 0;
                            bool player_turning = pack_turn_state.pack_intermediate_states_timer > 0;
                            if (player_turning) passthrough_comparison = PUSH_FROM_TURN_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH;
                            else passthrough_comparison = STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH; 

                            bool direction_on_axis = false;
                            if (mirror->moving_direction == current_direction || mirror->moving_direction == oppositeDirection(current_direction)) direction_on_axis = true;
                            if (direction_on_axis)
                            {
                                Vec3 offset_test = vec3Subtract(prev_lb.end_coords, intCoordsToNorm(current_tile_coords));
                                if (laser_turn_index == 0 || (offset_test.x == 0 && offset_test.y == 0) || (offset_test.x == 0 && offset_test.z == 0) || (offset_test.y == 0 && offset_test.z == 0))
                                {
                                    Vec3 offset = vec3Subtract(mirror->position_norm, intCoordsToNorm(mirror->coords));

                                    if (mirror->in_motion > passthrough_comparison)
                                    {
                                        current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                                        offset = vec3Add(offset, directionToVector(oppositeDirection(current_direction)));
                                    }

                                    lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                                }
                            }
                            else
                            {
                                // moving sideways
                                if (!th_hit && mirror->in_motion > passthrough_comparison)
                                {
                                    // passthrough
                                    current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                                    continue;
                                }
                            }
                        }
                        if (vec3IsZero(lb->end_coords)) // if end_coords has not already been updated in above logic
                        {
                            Vec3 current_dir_basis = directionToVector(current_direction);
                            Vec3 next_dir_basis = directionToVector(getNextLaserDirectionMirror(current_direction, mirror->direction));

                            lb->end_coords = lb->start_coords;
                            if (current_dir_basis.x != 0) lb->end_coords.x = intCoordsToNorm(current_tile_coords).x;
                            if (current_dir_basis.y != 0) lb->end_coords.y = intCoordsToNorm(current_tile_coords).y;
                            if (current_dir_basis.z != 0) lb->end_coords.z = intCoordsToNorm(current_tile_coords).z;

                            Vec3 comparison_coords = intCoordsToNorm(mirror->coords);
                            if (th_hit) comparison_coords = intCoordsToNorm(getNextCoords(mirror->coords, oppositeDirection(th.moving_direction)));

                            Vec3 mirror_shift = vec3Subtract(mirror->position_norm, comparison_coords);
                            Vec3 laser_shift = vec3Subtract(intCoordsToNorm(current_tile_coords), lb->end_coords);
                            Vec3 total_shift = vec3Subtract(mirror_shift, laser_shift);
                            bool reverse_offset = false; 
                            if (vec3IsZero(vec3Subtract(lb->end_coords, intCoordsToNorm(current_tile_coords)))) reverse_offset = true; // TODO: this case should be handled above

                            // this part is just 'which axis did the dot product of the two laser directions land on', hence 6 cases
							// TODO: this entire section could probably be smarter, and not have to code 6 cases like this. 
                            if (!vec3IsZero(vec3Hadamard(total_shift, next_dir_basis)))
                            {
                                float offset_magnitude = 0;
                                if (next_dir_basis.x != 0)
                                {
                                    offset_magnitude = total_shift.x;
                                    Vec3 guess_end_coords = vec3Add(lb->end_coords, vec3ScalarMultiply(current_dir_basis, offset_magnitude));
                                    Vec3 guess_comparison = vec3Subtract(guess_end_coords, lb->end_coords);
                                    bool curr_dir_bit;
                                    if (current_dir_basis.y != 0) curr_dir_bit = (current_dir_basis.y < 0) != (guess_comparison.y < 0);
                                    else 					   	  curr_dir_bit = (current_dir_basis.z < 0) != (guess_comparison.z < 0);
                                    bool next_dir_bit = (next_dir_basis.x < 0) != (offset_magnitude < 0);
                                    if (curr_dir_bit != next_dir_bit) offset_magnitude = -offset_magnitude;
                                }
                                if (next_dir_basis.y != 0)
                                {
                                    offset_magnitude = total_shift.y;
                                    Vec3 guess_end_coords = vec3Add(lb->end_coords, vec3ScalarMultiply(current_dir_basis, offset_magnitude));
                                    Vec3 guess_comparison = vec3Subtract(guess_end_coords, lb->end_coords);
                                    bool curr_dir_bit;
                                    if (current_dir_basis.x != 0) curr_dir_bit = (current_dir_basis.x < 0) != (guess_comparison.x < 0);
                                    else 					   	  curr_dir_bit = (current_dir_basis.z < 0) != (guess_comparison.z < 0);
                                    bool next_dir_bit = (next_dir_basis.y < 0) != (offset_magnitude < 0);
                                    if (curr_dir_bit != next_dir_bit) offset_magnitude = -offset_magnitude;
                                }
                                if (next_dir_basis.z != 0)
                                {
                                    offset_magnitude = total_shift.z;
                                    Vec3 guess_end_coords = vec3Add(lb->end_coords, vec3ScalarMultiply(current_dir_basis, offset_magnitude));
                                    Vec3 guess_comparison = vec3Subtract(guess_end_coords, lb->end_coords);
                                    bool curr_dir_bit;
                                    if (current_dir_basis.x != 0) curr_dir_bit = (current_dir_basis.x < 0) != (guess_comparison.x < 0);
                                    else 					   	  curr_dir_bit = (current_dir_basis.y < 0) != (guess_comparison.y < 0);
                                    bool next_dir_bit = (next_dir_basis.z < 0) != (offset_magnitude < 0);
                                    if (curr_dir_bit != next_dir_bit) offset_magnitude = -offset_magnitude;
                                }

                                if (reverse_offset) offset_magnitude = -offset_magnitude;
                                lb->end_coords = vec3Add(lb->end_coords, vec3ScalarMultiply(current_dir_basis, offset_magnitude));
                            }
                            else if (!vec3IsZero(vec3Hadamard(total_shift, directionToVector(current_direction))))
                            {
                                float offset_magnitude = 0;
                                if (current_dir_basis.x != 0)
                                {
                                    offset_magnitude = total_shift.x;
                                    Vec3 guess_end_coords = vec3Add(lb->end_coords, vec3ScalarMultiply(current_dir_basis, offset_magnitude));
                                    Vec3 guess_comparison = vec3Subtract(guess_end_coords, lb->end_coords);
                                    bool curr_dir_bit = (current_dir_basis.x < 0) != (offset_magnitude < 0);
                                    bool next_dir_bit;
                                    if (next_dir_basis.y != 0) next_dir_bit = (next_dir_basis.y < 0) != (guess_comparison.y < 0);
                                    else 					   next_dir_bit = (next_dir_basis.z < 0) != (guess_comparison.z < 0);
                                    if (curr_dir_bit != next_dir_bit) offset_magnitude = -offset_magnitude;
                                }
                                if (current_dir_basis.y != 0)
                                {
                                    offset_magnitude = total_shift.y;
                                    Vec3 guess_end_coords = vec3Add(lb->end_coords, vec3ScalarMultiply(current_dir_basis, offset_magnitude));
                                    Vec3 guess_comparison = vec3Subtract(guess_end_coords, lb->end_coords);
                                    bool curr_dir_bit = (current_dir_basis.y < 0) != (offset_magnitude < 0);
                                    bool next_dir_bit;
                                    if (next_dir_basis.x != 0) next_dir_bit = (next_dir_basis.x < 0) != (guess_comparison.x < 0);
                                    else 					   next_dir_bit = (next_dir_basis.z < 0) != (guess_comparison.z < 0);
                                    if (curr_dir_bit != next_dir_bit) offset_magnitude = -offset_magnitude;
                                }
                                if (current_dir_basis.z != 0)
                                {
                                    offset_magnitude = total_shift.z;
                                    Vec3 guess_end_coords = vec3Add(lb->end_coords, vec3ScalarMultiply(current_dir_basis, offset_magnitude));
                                    Vec3 guess_comparison = vec3Subtract(guess_end_coords, lb->end_coords);
                                    bool curr_dir_bit = (current_dir_basis.z < 0) != (offset_magnitude < 0);
                                    bool next_dir_bit;
                                    if (next_dir_basis.x != 0) next_dir_bit = (next_dir_basis.x < 0) != (guess_comparison.x < 0);
                                    else 					   next_dir_bit = (next_dir_basis.y < 0) != (guess_comparison.y < 0);
                                    if (curr_dir_bit != next_dir_bit) offset_magnitude = -offset_magnitude;
                                }

                                if (reverse_offset) offset_magnitude = -offset_magnitude;
                                lb->end_coords = vec3Add(lb->end_coords, vec3ScalarMultiply(current_dir_basis, offset_magnitude));
                            }
                            else
                            {
                                lb->end_coords = intCoordsToNorm(current_tile_coords);
                            }
                        }

                        current_direction = getNextLaserDirectionMirror(current_direction, mirror->direction);
                        if (current_direction != NO_DIRECTION)
                        {
                            no_more_turns = false;
                            skip_next_mirror = 2;
                            skip_mirror_id = mirror->id;
                        }
                        else
                        {
                            lb->end_coords = intCoordsToNorm(current_tile_coords);
                        }
                        break;
                    }
                    else
                    {
                        current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                        continue;
                    }
                }

                if (real_hit_type != NONE)
                {
                    if (isEntity(real_hit_type))
                    {
                        Entity* e = getEntityAtCoords(current_tile_coords);
                        if (e)
                        {
                            bool do_passthrough = false;
                            bool direction_on_axis = false;
                            if (!th_hit && e->in_motion > STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH) do_passthrough = true;
                            if (e->moving_direction == current_direction || e->moving_direction == oppositeDirection(current_direction)) direction_on_axis = true;
                            if (do_passthrough && !direction_on_axis)
                            {
                                current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                                continue;
                            }
                            if (direction_on_axis) lb->end_coords = e->position_norm; // TODO: causes issues with some 2-step undo interpolations. but solveable by just assigning moving_direction correctly on undo.
                        }
                    }
                    if (vec3IsEqual(lb->end_coords, VEC3_0)) lb->end_coords = intCoordsToNorm(current_tile_coords); // if not already updated above
                    Vec3 dir_basis = directionToVector(current_direction);
                    if (dir_basis.x == 0) lb->end_coords.x = lb->start_coords.x;
                    if (dir_basis.y == 0) lb->end_coords.y = lb->start_coords.y;
                    if (dir_basis.z == 0) lb->end_coords.z = lb->start_coords.z;
                    break;
                }

                current_tile_coords = getNextCoords(current_tile_coords, current_direction);
            }

            if (no_more_turns) break;
        }
    }
}

// FALLING LOGIC

// returns true iff object is able to fall as usual, but object collides with something instead.
bool doFallingEntity(Entity* entity, bool do_animation)
{
    if (entity->removed) return false;
    Int3 next_coords = getNextCoords(entity->coords, DOWN);
    if (!intCoordsWithinLevelBounds(next_coords)) return false;
    if (!(isPushable(getTileType(next_coords)) && getEntityAtCoords(next_coords)->removed) && getTileType(next_coords) != NONE) return true;
    TrailingHitbox _;
    if (trailingHitboxAtCoords(next_coords, &_) && entity->id != PLAYER_ID) return true;

    int32 stack_size = getPushableStackSize(entity->coords);
    Int3 current_start_coords = entity->coords;
    Int3 current_end_coords = next_coords; 
    FOR(stack_fall_index, stack_size)
    {
        Entity* entity_in_stack = getEntityAtCoords(current_start_coords);
        if (entity_in_stack->removed) return false; // should never happen, shouldn't have removed entity in the middle of a stack somewhere
        if (entity_in_stack->in_motion) return false; 
        if (entity_in_stack == &next_world_state.pack && !pack_detached && stack_fall_index != 0) return false;
        if (entity_in_stack == &next_world_state.player && !next_world_state.player.hit_by_red) time_until_allow_game_input = FALL_ANIMATION_TIME;

        // check if this is going to be first fall
        if (!entity_in_stack->first_fall_already_done)
        {
            if (do_animation) 
            {
                createFirstFallAnimation(intCoordsToNorm(current_start_coords), &entity_in_stack->position_norm, entity_in_stack->id);
                createTrailingHitbox(current_start_coords, DOWN, NO_DIRECTION, TRAILING_HITBOX_TIME + 4, getTileType(entity_in_stack->coords)); // it takes 4 extra frames to get to the point where it's cutting off the below laser (and thus not cutting off above, i guess)
            }
            entity_in_stack->first_fall_already_done = true;
            entity_in_stack->in_motion = STANDARD_IN_MOTION_TIME + 5;
            entity_in_stack->moving_direction = DOWN; 
        }
        else
        {
            if (do_animation)
            {
                createInterpolationAnimation(intCoordsToNorm(current_start_coords),
                                             intCoordsToNorm(current_end_coords),
                                             &entity_in_stack->position_norm,
                                             IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                             entity_in_stack->id, FALL_ANIMATION_TIME);
                createTrailingHitbox(current_start_coords, DOWN, NO_DIRECTION, TRAILING_HITBOX_TIME, getTileType(entity_in_stack->coords));
            }
            entity_in_stack->first_fall_already_done = true;
            entity_in_stack->in_motion = STANDARD_IN_MOTION_TIME + 1;
            entity_in_stack->moving_direction = DOWN; 
        }

		moveEntityInBufferAndState(entity_in_stack, current_end_coords, entity_in_stack->direction);

        current_end_coords = current_start_coords;
        current_start_coords = getNextCoords(current_start_coords, UP);
    }
    return false;
}

void doFallingObjects(bool do_animation)
{
    Entity* object_group_to_fall[6] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.glass_blocks, next_world_state.sources, next_world_state.win_blocks, next_world_state.reset_blocks };
    FOR(to_fall_index, 6)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* entity = &object_group_to_fall[to_fall_index][entity_index];

            if (entity->locked || entity->removed) continue;
            doFallingEntity(entity, do_animation);

            if (getTileType(getNextCoords(entity->coords, DOWN)) == VOID && !entity->in_motion)
            {
                setTileType(NONE, entity->coords);
                entity->removed = true;
            }
        }
    }
}

// GHOSTS

// returns true if player is able to try to tp (i.e. player is facing tw green beam). doesn't consider any obstructed tps.
bool calculateGhosts()
{
    Entity* player = &next_world_state.player;

    // render and calculate ghosts
    bool facing_green = false;
    switch (player->direction)
    {
        case NORTH: if (player->green_hit.south) facing_green = true; break;
        case WEST:  if (player->green_hit.east)  facing_green = true; break;
        case SOUTH: if (player->green_hit.north) facing_green = true; break;
        case EAST:  if (player->green_hit.west)  facing_green = true; break;
        case UP:    if (player->green_hit.down)  facing_green = true; break;
        case DOWN:  if (player->green_hit.up)    facing_green = true; break;
        default: break;
    }
    if (!facing_green) return false;

    Int3 current_coords = player->coords; 
    Direction current_direction = player->direction;
    FOR(seek_index, MAX_LASER_TRAVEL_DISTANCE)
    {
        current_coords = getNextCoords(current_coords, current_direction);
        TileType current_tile = getTileType(current_coords);
        if (current_tile == MIRROR)
        {
            current_direction = getNextLaserDirectionMirror(current_direction, getTileDirection(current_coords));
            continue;
        }
        if (current_tile != NONE) break;
    }
    player_ghost_coords = getNextCoords(current_coords, oppositeDirection(current_direction));
    player_ghost_direction = current_direction;
    if (!pack_detached) 
    {
        pack_ghost_coords = getNextCoords(player_ghost_coords, oppositeDirection(current_direction));
        pack_ghost_direction = current_direction;
    }
    return true;
}

// TEXT HELPERS FOR EDIT_BUFFER

void editAppendChar(char character)
{
    EditBuffer* buffer = &editor_state.edit_buffer; 
    if (buffer->length >= 256) return;
    buffer->string[buffer->length++] = character;
}

void editBackspace()
{
    EditBuffer* buffer = &editor_state.edit_buffer;
    if (buffer->length == 0) return;
    buffer->length--;
    buffer->string[buffer->length] = 0;
}

void updateTextInput(TickInput *input)
{
    for (int32 chars_typed_index = 0; chars_typed_index < input->text.count; chars_typed_index++)
    {
        uint32 codepoint = input->text.codepoints[chars_typed_index];
        char character = (char)codepoint;
        editAppendChar(character);
    }
    if (input->backspace_pressed_this_frame)
    {
        editBackspace();
    }
}

int32 glyphSprite(char character)
{
    unsigned char uc = (unsigned char)character;
    if (uc < FONT_FIRST_ASCII || uc > FONT_LAST_ASCII) uc = '?';
    return (SpriteId)(uc - FONT_FIRST_ASCII);
}

void initUndoBuffer()
{
    memset(&undo_buffer, 0, sizeof(UndoBuffer));
    memset(undo_buffer.level_change_indices, 0xFF, sizeof(undo_buffer.level_change_indices));
}

// TODO:
// i want to write undo buffer to a file on every movement, because if player closes the game, and undo buffer is not up to date, then bad things happen.
// but the write function takes >10ms, for some reason? maybe fwrite is just really slow, or i should thread it? 
// either way, i'm not writing undo buffer to a file at all right now, because it causes noticeable lag.
/*
void writeUndoBufferToFile()
{
    FILE* file = fopen(undo_meta_path, "wb");
    if (!file) return;
    fwrite(&undo_buffer, sizeof(UndoBuffer), 1, file);
    fclose(file);
}
void loadUndoBufferFromFile()
{
    FILE* file = fopen(undo_meta_path, "rb");
    if (!file)
    {
        initUndoBuffer();
        return;
    }
    if (fread(&undo_buffer, sizeof(UndoBuffer), 1, file) != 1)
    {
        initUndoBuffer();
    }
    fclose(file);
}
*/

// GAME INIT

void gameInitializeState(char* level_name)
{
    if (level_name == 0) strcpy(next_world_state.level_name, debug_level_name);
    else strcpy(next_world_state.level_name, level_name);

    Entity* player = &next_world_state.player;
    Entity* pack = &next_world_state.pack;

    // memset worldstate to 0 (with persistant level_name, and solved levels)
    char persist_level_name[256] = {0};
    char persist_solved_levels[64][64] = {0};
    strcpy(persist_level_name, next_world_state.level_name);
    memcpy(persist_solved_levels, next_world_state.solved_levels, sizeof(persist_solved_levels));

    memset(&next_world_state, 0, sizeof(WorldState));
    strcpy(next_world_state.level_name, persist_level_name);
    memcpy(next_world_state.solved_levels, persist_solved_levels, sizeof(persist_solved_levels));

    if (strcmp(next_world_state.level_name, "overworld") == 0) in_overworld = true;
    else in_overworld = false;

    // build level_path from level_name
    char level_path[64] = {0};
    buildLevelPathFromName(next_world_state.level_name, &level_path, false);
    FILE* file = fopen(level_path, "rb+");
    loadBufferInfo(file);
    fclose(file);

    memset(next_world_state.boxes,    	   0, sizeof(next_world_state.boxes)); 
    memset(next_world_state.mirrors,  	   0, sizeof(next_world_state.mirrors));
    memset(next_world_state.glass_blocks,  0, sizeof(next_world_state.glass_blocks));
    memset(next_world_state.sources,  	   0, sizeof(next_world_state.sources));
    memset(next_world_state.win_blocks,    0, sizeof(next_world_state.win_blocks));
    memset(next_world_state.locked_blocks, 0, sizeof(next_world_state.locked_blocks));
    memset(next_world_state.reset_blocks,  0, sizeof(next_world_state.reset_blocks));
    FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        next_world_state.boxes[entity_index].id 		= -1;
        next_world_state.mirrors[entity_index].id 		= -1;
        next_world_state.glass_blocks[entity_index].id	= -1;
        next_world_state.sources[entity_index].id		= -1;
        next_world_state.win_blocks[entity_index].id	= -1;
        next_world_state.locked_blocks[entity_index].id	= -1;
        next_world_state.reset_blocks[entity_index].id	= -1;
        FOR(to_reset_index, MAX_RESET_COUNT) next_world_state.reset_blocks[entity_index].reset_info[to_reset_index].id = -1;
    }

    Entity *entity_group = 0;
    for (int buffer_index = 0; buffer_index < 2 * level_dim.x*level_dim.y*level_dim.z; buffer_index += 2)
    {
        TileType buffer_contents = next_world_state.buffer[buffer_index];
        if 	    (buffer_contents == BOX)     	  entity_group = next_world_state.boxes;
        else if (buffer_contents == MIRROR)  	  entity_group = next_world_state.mirrors;
        else if (buffer_contents == GLASS)	 	  entity_group = next_world_state.glass_blocks;
        else if (buffer_contents == WIN_BLOCK)    entity_group = next_world_state.win_blocks;
        else if (buffer_contents == LOCKED_BLOCK) entity_group = next_world_state.locked_blocks;
        else if (buffer_contents == RESET_BLOCK)  entity_group = next_world_state.reset_blocks;
        else if (isSource(buffer_contents))  	  entity_group = next_world_state.sources;
        if (entity_group != 0)
        {
            int32 count = getEntityCount(entity_group);
            entity_group[count].coords = bufferIndexToCoords(buffer_index);
            entity_group[count].position_norm = intCoordsToNorm(entity_group[count].coords);
            entity_group[count].direction = next_world_state.buffer[buffer_index + 1]; 
            entity_group[count].rotation_quat = directionToQuaternion(entity_group[count].direction, true);
            entity_group[count].color = getEntityColor(entity_group[count].coords);
            entity_group[count].id = getEntityCount(entity_group) + entityIdOffset(entity_group, entity_group[count].color);
        	entity_group[count].removed = false;
            entity_group = 0;
        }
        else if (next_world_state.buffer[buffer_index] == PLAYER)
        {
            player->coords = bufferIndexToCoords(buffer_index);
            player->position_norm = intCoordsToNorm(player->coords);
            player->direction = next_world_state.buffer[buffer_index + 1];
            player->rotation_quat = directionToQuaternion(player->direction, true);
            player->id = PLAYER_ID;
        }
        else if (next_world_state.buffer[buffer_index] == PACK)
        {
            pack->coords = bufferIndexToCoords(buffer_index);
            pack->position_norm = intCoordsToNorm(pack->coords);
            pack->direction = next_world_state.buffer[buffer_index + 1];
            pack->rotation_quat = directionToQuaternion(pack->direction, true);
            pack->id = PACK_ID;
        }
    }

    file = fopen(level_path, "rb+");
    saved_main_camera = loadCameraInfo(file, false);
    saved_alt_camera = loadCameraInfo(file, true);
    if (in_overworld)
    {
        if (saved_overworld_camera.fov > 0) camera = saved_overworld_camera; // on first startup, just use the camera that's saved as main camera in the overworld
        else camera = saved_main_camera;
        camera_mode = saved_overworld_camera_mode;
        if (camera_mode == ALT_WAITING) camera_lerp_t = 1.0f;
        else camera_lerp_t = 0.0f;
    }
    else
    {
        camera = saved_main_camera;
        camera_mode = MAIN_WAITING;
        camera_lerp_t = 0.0f;
    }
    loadWinBlockPaths(file);
    loadLockedInfoPaths(file);
    loadResetBlockInfo(file);
    fclose(file);

    loadSolvedLevelsFromFile();

    camera_screen_offset.x = (int32)(camera.coords.x / OVERWORLD_SCREEN_SIZE_X);
    camera_screen_offset.z = (int32)(camera.coords.z / OVERWORLD_SCREEN_SIZE_Z);
    camera.rotation = buildCameraQuaternion(camera);
    camera_target_plane = player->coords.y;

    world_state = next_world_state;
}

void recalculateDebugStartCoords()
{
    debug_text_start_coords = (Vec2){ 50.0f, game_display.client_height - 50.0f };
    debug_popup_start_coords = (Vec2){ game_display.client_width / 2.0f, 80.0f };
}

void gameInitialize(char* level_name, DisplayInfo display_from_platform)
{	
    game_display = display_from_platform;
	recalculateDebugStartCoords();

    initUndoBuffer();

    // read overworld-zero's world state from file on startup, so it's kept in memory. this is used on restart in the overworld.
    gameInitializeState("overworld-zero");
    memcpy(&overworld_zero, &world_state, sizeof(WorldState));

    gameInitializeState(level_name);
}

void gameRedraw(DisplayInfo display_from_platform)
{
    if (draw_command_count == 0) return;
    game_display = display_from_platform;
	recalculateDebugStartCoords();
    vulkanSubmitFrame(draw_commands, draw_command_count, (float)global_time, camera_with_ow_offset, game_shader_mode); // render models is temp passthrough here. toggles some shaders that apply to everything,
                                                                          						                       // but that i only want on when working on rendering
    vulkanDraw();
}

// UNDO / RESTART

// the undo system uses three circular buffers:
// 1. deltas: records id, coords, direction for individual entities
// 2. headers: groups deltas
// 3. level_changes: stores extra data which is required when an action changes the current level.
//
// every action taken in the game that wants to be able to be undone records the old state of every entity before the action happened. 
// note, this is pretty lazy; could be smarter about exactly what enities need a delta, and only store those, and that would be supported in this system, but it's sometimes pretty 
// difficult to know what entities will be affected by an action and thus need a delta without just simulating forward. this is a solveable problem, but for now i'm just storing deltas for every entity.
//
// on undo, restore the old states and create interpolation animations from the current position_norm of the entities. the longer port of the performUndo function is dealing with
// edge cases based on the start / end coords. for example, if a box has travelled down and right in one action, it must have been pushed and then fallen - it cannot have fallen and been moved on one
// action. so i split the interpolation animation into two parts, and always do the up movement first, then the left movement, since the box will have gone right->down in every such case.
// 
// actions that shouldn't interpolate on undo (teleports and resets) don't get interpolated.

// writes one delta into the circular buffer
void recordEntityDelta(Entity* e)
{
    uint32 pos = undo_buffer.delta_write_pos;
    undo_buffer.deltas[pos].id = e->id;
    undo_buffer.deltas[pos].old_coords = e->coords;
    undo_buffer.deltas[pos].old_direction = e->direction;
    undo_buffer.deltas[pos].was_removed = e->removed;
    undo_buffer.delta_write_pos = (pos + 1) % MAX_UNDO_DELTAS;
    undo_buffer.delta_count++;
}

void evictOldestUndoAction()
{
    UndoActionHeader* oldest = &undo_buffer.headers[undo_buffer.oldest_action_index];
    undo_buffer.delta_count -= oldest->entity_count;
    if (oldest->level_changed)
    {
        uint8 level_change_index = undo_buffer.level_change_indices[undo_buffer.oldest_action_index];
        if (level_change_index != 0xFF)
        {
            undo_buffer.level_change_count--;
        }
    }
    undo_buffer.level_change_indices[undo_buffer.oldest_action_index] = 0xFF;
    undo_buffer.oldest_action_index = (undo_buffer.oldest_action_index + 1) % MAX_UNDO_ACTIONS;
    undo_buffer.header_count--;
}

// called after a noraml (non-level-change) action
// diffs world_state vs. next_world_state and stores deltas for every entity that changed
// was_reset and was_teleport currently do the same thing: cancel interpolation animations for the player (and pack if attached) for that action.
void recordActionForUndo(WorldState* old_state, bool action_was_reset, bool action_was_teleport, bool action_was_climb)
{
    if (undo_buffer.header_count >= MAX_UNDO_ACTIONS) evictOldestUndoAction();

    uint32 header_index = undo_buffer.header_write_pos;
    uint32 delta_start = undo_buffer.delta_write_pos;
    uint32 entity_count = 0;

    recordEntityDelta(&old_state->player);
    recordEntityDelta(&old_state->pack);
    entity_count += 2;

	// other entities
    Entity* groups[7] = { old_state->boxes, old_state->mirrors, old_state->glass_blocks, old_state->sources, old_state->win_blocks, old_state->locked_blocks, old_state->reset_blocks };
    FOR(group_index, 7)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* e = &groups[group_index][entity_index];
            if (e->id == -1) continue;

            recordEntityDelta(e);
            entity_count++;
        }
    }

    undo_buffer.headers[header_index].entity_count = (uint8)entity_count;
    undo_buffer.headers[header_index].delta_start_pos = delta_start;
    undo_buffer.headers[header_index].level_changed = false;
    undo_buffer.headers[header_index].was_teleport = action_was_teleport;
    undo_buffer.headers[header_index].was_reset = action_was_reset;
    undo_buffer.headers[header_index].was_climb = action_was_climb;
    undo_buffer.level_change_indices[header_index] = 0xFF;
    undo_buffer.header_write_pos = (header_index + 1) % MAX_UNDO_ACTIONS;
    undo_buffer.header_count++;

    restart_last_turn = false;

    //writeUndoBufferToFile();
}

// call before transitioning to a new level. stores a delta for every entity in the current level, plus the level change metadata
void recordLevelChangeForUndo(char* current_level_name, bool level_was_just_solved)
{
    if (undo_buffer.header_count >= MAX_UNDO_ACTIONS) evictOldestUndoAction();

    // evict oldest level change if level_changes array is full
    if (undo_buffer.level_change_count >= MAX_LEVEL_CHANGES)
    {
        uint32 scan = undo_buffer.oldest_action_index;
        for (uint32 header_index = 0; header_index < undo_buffer.header_count; header_index++)
        {
            uint32 index = (scan + header_index) % MAX_UNDO_ACTIONS;
            if (undo_buffer.headers[index].level_changed)
            {
                for(uint32 up_to_header = 0; up_to_header < header_index; up_to_header++)
                {
                    uint32 evict_index = (scan + up_to_header) % MAX_UNDO_ACTIONS;
                    undo_buffer.delta_count -= undo_buffer.headers[evict_index].entity_count;
                    if (undo_buffer.headers[evict_index].level_changed)
                    {
                        undo_buffer.level_change_count--;
                    }
                    undo_buffer.level_change_indices[evict_index] = 0xFF;
                    undo_buffer.header_count--;
                }
                undo_buffer.oldest_action_index = (scan + header_index + 1) % MAX_UNDO_ACTIONS;
                break;
            }
        }
    }

    uint32 header_index = undo_buffer.header_write_pos;
    uint32 delta_start = undo_buffer.delta_write_pos;
    uint32 entity_count = 0;

    // store all entities
    recordEntityDelta(&next_world_state.player);
    recordEntityDelta(&next_world_state.pack);
    entity_count += 2;

    Entity* groups[7] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.glass_blocks, next_world_state.sources, next_world_state.win_blocks, next_world_state.locked_blocks, next_world_state.reset_blocks };
    FOR(group_index, 7)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* e = &groups[group_index][entity_index];
            if (e->id == -1) continue;
            recordEntityDelta(e);
            entity_count++;
        }
    }

    // write level change info
    uint32 level_change_index = undo_buffer.level_change_write_pos;
    memset(undo_buffer.level_changes[level_change_index].from_level, 0, 64);
    strcpy(undo_buffer.level_changes[level_change_index].from_level, current_level_name);
    undo_buffer.level_changes[level_change_index].remove_from_solved = level_was_just_solved;
    undo_buffer.level_change_write_pos = (level_change_index + 1) % MAX_LEVEL_CHANGES;
    undo_buffer.level_change_count++;

    // write header
    undo_buffer.headers[header_index].entity_count = (uint8)entity_count;
    undo_buffer.headers[header_index].delta_start_pos = delta_start;
    undo_buffer.headers[header_index].level_changed = true;
    undo_buffer.level_change_indices[header_index] = (uint8)level_change_index;
    undo_buffer.header_write_pos = (header_index + 1) % MAX_UNDO_ACTIONS;
    undo_buffer.header_count++;

    restart_last_turn = false;

    //writeUndoBufferToFile();
}

bool performUndo(int32 undo_animation_time)
{
    if (undo_buffer.header_count == 0) return false;

    // clear animations + trailing hitboxes
    memset(animations, 0, sizeof(animations));
    memset(trailing_hitboxes, 0, sizeof(trailing_hitboxes));

	// get most recent action header
    uint32 header_index = (undo_buffer.header_write_pos + MAX_UNDO_ACTIONS - 1) % MAX_UNDO_ACTIONS;
    UndoActionHeader* header = &undo_buffer.headers[header_index];

    if (header->level_changed)
    {
        uint8 level_change_index = undo_buffer.level_change_indices[header_index];
        UndoLevelChange* level_change = &undo_buffer.level_changes[level_change_index];

        // reinitialize previous
        gameInitializeState(level_change->from_level);

        // remove from solved levels if the level was just completed
        if (level_change->remove_from_solved)
        {
            removeFromSolvedLevels(level_change->from_level);
            writeSolvedLevelsToFile();
        }
    }

    // pass 1: clear all current tiles
    uint32 delta_pos = header->delta_start_pos;
    FOR(entity_index, header->entity_count)
    {
        UndoEntityDelta* delta = &undo_buffer.deltas[delta_pos];
        Entity* e = getEntityFromId(delta->id);
        if (e && !e->removed)
        {
            setTileType(NONE, e->coords);
            setTileDirection(NORTH, e->coords);
        }
        delta_pos = (delta_pos + 1) % MAX_UNDO_DELTAS;
    }

    // pass 2: restore all entities
    delta_pos = header->delta_start_pos;
    FOR(entity_index, header->entity_count)
    {
        UndoEntityDelta* delta = &undo_buffer.deltas[delta_pos];

        Entity* e = getEntityFromId(delta->id);
        if (e)
        {
            Vec3 old_position = e->position_norm;
            Vec4 old_rotation = e->rotation_quat;
            bool was_at_different_coords = !int3IsEqual(e->coords, delta->old_coords);
            bool was_at_different_direction = (e->direction != delta->old_direction);

            e->coords = delta->old_coords;
            e->position_norm = intCoordsToNorm(e->coords); // TODO: this gets overwritten later, but is used as endpoint for queued animations. a bit messy, maybe fix
            e->direction = delta->old_direction;
            e->rotation_quat = directionToQuaternion(e->direction, true);
            e->removed = delta->was_removed;

            if (!delta->was_removed)
            {
            	TileType type = getTileTypeFromId(delta->id);
                setTileType(type, delta->old_coords);
                setTileDirection(delta->old_direction, delta->old_coords);

                if (!header->level_changed && !header->was_teleport && !header->was_reset && (was_at_different_coords || was_at_different_direction))
                {
                    int32 dx = (int32)roundf(e->position_norm.x - old_position.x);
                    int32 dy = (int32)roundf(e->position_norm.y - old_position.y);
                    int32 dz = (int32)roundf(e->position_norm.z - old_position.z);

                    if (dx != 0 || dy != 0 || dz != 0 || was_at_different_direction) // only do any sort of interpolation if the object moved / changed direction 
                    {
                        if (dx != 0 && dy != 0 && dz != 0) e->in_motion = undo_animation_time;
                        // moving direction is updated later. TODO: note that this doesn't work with the 2-step animations (it just assumes whatever diff coord direction function returns as direction) - but should work for all 'normal' animations

                        // a lot of edge case handling for how to interpolate undos
                        if (e->id == PLAYER_ID)
                        {
                            if (header->was_climb)
                            {
                                // player climb
                                Vec3 mid_position = { e->position_norm.x, old_position.y, e->position_norm.z };
                                int32 first_animation_time = undo_animation_time / 2;
                                int32 second_animation_time = undo_animation_time - first_animation_time;
                                createInterpolationAnimation(old_position, mid_position, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, PLAYER_ID, first_animation_time);
                                createInterpolationAnimation(mid_position, e->position_norm, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, PLAYER_ID, second_animation_time);
                                e->moving_direction = NO_DIRECTION;
                            }
                            else if (dy != 0 && was_at_different_direction)
                            {
                                // player turn and fall
                                Vec3 mid_position = { old_position.x, e->position_norm.y, old_position.z };
                                int32 first_animation_time = undo_animation_time / 2;
                                int32 second_animation_time = undo_animation_time - first_animation_time;
                                createInterpolationAnimation(old_position, mid_position, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, PLAYER_ID, first_animation_time);
                                createInterpolationAnimation(VEC3_0, VEC3_0, 0, old_rotation, e->rotation_quat, &e->rotation_quat, PLAYER_ID, second_animation_time);
                                e->moving_direction = getDirectionFromCoordDiff(e->coords, normCoordsToInt(old_position));
                            }
                            else if (dy != 0 && (dx != 0 || dz != 0))
                            {
                                // player move and fall
                                Vec3 mid_position = { old_position.x, e->position_norm.y, old_position.z };
                                int32 first_animation_time = undo_animation_time / 2;
                                int32 second_animation_time = undo_animation_time - first_animation_time;
                                createInterpolationAnimation(old_position, mid_position, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, PLAYER_ID, first_animation_time);
                                createInterpolationAnimation(mid_position, e->position_norm, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, PLAYER_ID, second_animation_time);
                                e->moving_direction = getDirectionFromCoordDiff(e->coords, normCoordsToInt(old_position));
                            }
                            else
                            {
                                createInterpolationAnimation(old_position, e->position_norm, &e->position_norm, old_rotation, e->rotation_quat, &e->rotation_quat, PLAYER_ID, undo_animation_time);
                                e->moving_direction = getDirectionFromCoordDiff(e->coords, normCoordsToInt(old_position));
                            }
                        }
                        else if (e->id == PACK_ID)
                        {
                            Int3 old_player_coords = {0};
                            uint32 scan_pos = header->delta_start_pos;
                            FOR(scan_index, header->entity_count)
                            {
                                UndoEntityDelta* d = &undo_buffer.deltas[scan_pos];
                                if (d->id == PLAYER_ID)
                                {
                                    old_player_coords = d->old_coords;
                                    break;
                                }
                                scan_pos = (scan_pos + 1) % MAX_UNDO_DELTAS;
                            }

                            Direction player_to_old_pack_dir = getDirectionFromCoordDiff(delta->old_coords, old_player_coords);
                            Direction player_to_new_pack_dir = getDirectionFromCoordDiff(normCoordsToInt(old_position), old_player_coords);

                            int32 clockwise_calculation = player_to_new_pack_dir - player_to_old_pack_dir;
                            bool clockwise = (clockwise_calculation == -1 || clockwise_calculation == 3);

                            if (dy == 0 && dx != 0 && dz != 0) // if both dx and dz != 0 this must be a turn
                            {
                                createPackRotationAnimation(intCoordsToNorm(old_player_coords), old_position, oppositeDirection(delta->old_direction), clockwise, &e->position_norm, &e->rotation_quat, PACK_ID, undo_animation_time);
                                e->moving_direction = getDirectionFromCoordDiff(e->coords, normCoordsToInt(old_position));
                            }
                            else if (header->was_climb)
                            {
                                Vec3 mid_position = { e->position_norm.x, old_position.y, e->position_norm.z };
                                int32 first_animation_time = undo_animation_time / 2;
                                int32 second_animation_time = undo_animation_time - first_animation_time;
                                createInterpolationAnimation(old_position, mid_position, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, PACK_ID, first_animation_time);
                                createInterpolationAnimation(mid_position, e->position_norm, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, PACK_ID, second_animation_time);
                                e->moving_direction = NO_DIRECTION;
                            }
                            else if (dy != 0 && dx != 0 && dz != 0) // if both dx and dz != 0 this must be a turn
                            {
                                Vec3 mid_position = { old_position.x, e->position_norm.y, old_position.z };
                                int32 first_animation_time = undo_animation_time / 2;
                                int32 second_animation_time = undo_animation_time - first_animation_time;
                                createInterpolationAnimation(old_position, mid_position, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, e->id, first_animation_time);
                                createPackRotationAnimation(intCoordsToNorm(old_player_coords), mid_position, oppositeDirection(delta->old_direction), clockwise, &e->position_norm, &e->rotation_quat, PACK_ID, second_animation_time);
                                e->moving_direction = getDirectionFromCoordDiff(e->coords, normCoordsToInt(old_position));
                            }
                            else if (dy != 0 && (dx != 0 || dz != 0))
                            {
                                // pack move and fall
                                Vec3 mid_position = { old_position.x, e->position_norm.y, old_position.z };
                                int32 first_animation_time = undo_animation_time / 2;
                                int32 second_animation_time = undo_animation_time - first_animation_time;
                                createInterpolationAnimation(old_position, mid_position, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, e->id, first_animation_time);
                                createInterpolationAnimation(mid_position, e->position_norm, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, e->id, second_animation_time);
                                e->moving_direction = getDirectionFromCoordDiff(e->coords, normCoordsToInt(old_position));
                            }
                            else // pack, but just moving normally, or being pushed / falling normally, so interpolate normally
                            {
                                createInterpolationAnimation(old_position, e->position_norm, &e->position_norm, old_rotation, e->rotation_quat, &e->rotation_quat, PACK_ID, undo_animation_time);
                                e->moving_direction = getDirectionFromCoordDiff(e->coords, normCoordsToInt(old_position));
                            }
                        }
                        else if (dy != 0 && (dx != 0 || dz != 0))
                        {
                            // object move and fall
                            Vec3 mid_position = { old_position.x, e->position_norm.y, old_position.z };
                            int32 first_animation_time = undo_animation_time / 2;
                            int32 second_animation_time = undo_animation_time - first_animation_time;
                            createInterpolationAnimation(old_position, mid_position, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, e->id, first_animation_time);
                            createInterpolationAnimation(mid_position, e->position_norm, &e->position_norm, IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0, e->id, second_animation_time);
                            e->moving_direction = getDirectionFromCoordDiff(e->coords, normCoordsToInt(old_position));
                        }
                        else
                        {
                            createInterpolationAnimation(old_position, e->position_norm, &e->position_norm, old_rotation, e->rotation_quat, &e->rotation_quat, e->id, undo_animation_time);
                            e->moving_direction = getDirectionFromCoordDiff(e->coords, normCoordsToInt(old_position));
                        }
                    }
                }
            }
        }
        delta_pos = (delta_pos + 1) % MAX_UNDO_DELTAS;
    }

    // rewind buffer positions
    undo_buffer.delta_write_pos = header->delta_start_pos;
    undo_buffer.delta_count -= header->entity_count;
    undo_buffer.level_change_indices[header_index] = 0xFF;
    undo_buffer.header_write_pos = header_index;
    undo_buffer.header_count--;

    restart_last_turn = false;

	// sync worldstate
    world_state = next_world_state;

    //writeUndoBufferToFile();

    return true;
}

void levelChangePrep(char next_level[64])
{
    bool level_was_just_solved = false;
    if (!in_overworld && findInSolvedLevels(next_world_state.level_name) == -1)
    {
        addToSolvedLevels(next_world_state.level_name);
        writeSolvedLevelsToFile();
        level_was_just_solved = true;
    }
    
    recordLevelChangeForUndo(next_world_state.level_name, level_was_just_solved);

    if (strcmp(next_level, "overworld") == 0) in_overworld = true;
    else in_overworld = false;

    memset(animations, 0, sizeof(animations));
}

// HEAD ROTATION / MOVEMENT

// handles situation when an object is on top of your head, in which case it moves and rotates with you.
void doHeadRotation(bool clockwise)
{
    int32 stack_size = getPushableStackSize(getNextCoords(next_world_state.player.coords, UP));

    Int3 current_tile_coords = getNextCoords(next_world_state.player.coords, UP);
    FOR(stack_rotate_index, stack_size)
    {
        Entity* entity = getEntityAtCoords(current_tile_coords);
        bool up_or_down = false;
        Direction current_direction = getTileDirection(current_tile_coords);
        Direction next_direction = NO_DIRECTION;
        switch (current_direction)
        {
            case NORTH:
            case WEST:
            case SOUTH:
            case EAST:
            {
                if (clockwise) 
                {
                    next_direction = current_direction + 1;
                    if (next_direction == UP) next_direction = NORTH;
                }
                else 
                {
                    next_direction = current_direction - 1;
                    if (next_direction == -1) next_direction = EAST;
                }
                break;
            }
            case UP:
            {
                next_direction = DOWN;
                up_or_down = true;
                break;
            }
            case DOWN:
            {
                next_direction = UP;
                up_or_down = true;
                break;
            }
            default: break;
        }

        int32 id = getEntityId(current_tile_coords);

        // for mirror
        if (!up_or_down) createInterpolationAnimation(VEC3_0, VEC3_0, 0, 
                                                     directionToQuaternion(current_direction, true), directionToQuaternion(next_direction, true), &entity->rotation_quat,
                                                     id, TURN_ANIMATION_TIME);
        else 
        {
            Vec4 start = entity->rotation_quat;
            float sign = clockwise ? 1.0f : -1.0f;
            Vec4 delta = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), sign * 0.25f * TAU);
            Vec4 end = quaternionNormalize(quaternionMultiply(delta, start));
            createInterpolationAnimation(VEC3_0, VEC3_0, 0,
                                         start, end, &entity->rotation_quat,
                                         id, TURN_ANIMATION_TIME);
        }

        setTileDirection(next_direction, current_tile_coords);
        entity->direction = next_direction;
        current_tile_coords = getNextCoords(current_tile_coords, UP);
    }
}

void doHeadMovement(Direction direction, bool animations_on, int32 animation_time)
{
    Int3 coords_above_player = getNextCoords(next_world_state.player.coords, UP);
    if (!isPushable(getTileType(coords_above_player))) return;
    if (canPushStack(coords_above_player, direction) == CAN_PUSH) pushAll(coords_above_player, direction, animation_time, animations_on, false);
}

void doStandardMovement(Direction input_direction, Int3 next_player_coords, int32 animation_time, bool record_for_undo)
{
    Entity* player = &next_world_state.player;
    Entity* pack = &next_world_state.pack;

    if (!player->hit_by_blue) doHeadMovement(input_direction, true, animation_time);

    createInterpolationAnimation(intCoordsToNorm(player->coords), intCoordsToNorm(next_player_coords), &player->position_norm,
                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                 PLAYER_ID, animation_time);
    createTrailingHitbox(player->coords, input_direction, NO_DIRECTION, TRAILING_HITBOX_TIME, PLAYER);
    moveEntityInBufferAndState(player, next_player_coords, player->direction);
    player->moving_direction = input_direction;
    changeMoving(player);

    // move pack also if pack is attached
    if (!pack_detached)
    {
        Int3 next_pack_coords = getNextCoords(pack->coords, input_direction);
        createInterpolationAnimation(intCoordsToNorm(pack->coords), intCoordsToNorm(next_pack_coords), &pack->position_norm,
                                     IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                     PACK_ID, animation_time);
        createTrailingHitbox(pack->coords, input_direction, NO_DIRECTION, TRAILING_HITBOX_TIME, PACK);
        moveEntityInBufferAndState(pack, next_pack_coords, pack->direction);
        pack->moving_direction = input_direction;
        changeMoving(pack);
    }

    if (record_for_undo) recordActionForUndo(&world_state, false, false, false);
}

void updatePackDetached()
{
    Entity* player = &next_world_state.player;
    Entity* pack = &next_world_state.pack;
    TileType tile_behind_player = getTileType(getNextCoords(next_world_state.player.coords, oppositeDirection(next_world_state.player.direction)));
    if (tile_behind_player == PACK) 
    {
        pack_detached = false;
        setTileDirection(player->direction, pack->coords);
        pack->direction = player->direction;
    }
    else pack_detached = true;
}

// EDITOR

/*
	- T press: draw camera boundary lines
    - Y press: get rid of debug text in top left
    - backspace press: reset camera to saved location

	- F, G, H are used as mouse buttons

	0: normal mode:
    - WASD movement TODO: add arrow keys
    - Z undo
    - R restart
    - Q interact TODO: think about removing this

	1: place/break mode
    - WASD, SPACE, SHIFT: camera movement
    - E: toggle rendering of models TODO: remove this when have 3d models for everything
    - LMB: break block
    - RMB: place selected on normal totarget block
    - MMB: select targeted block as picked block
    - L: increment picked block by 1
    - R: rotate target block
    - I: save world state
    - C: save camera
    - V: save alt camera
    - X: remove alt camera
    - J: change wide camera
    - B: fov up
    - N: fov down
    - M: clear solved levels
	- P: face camera north

    2: select mode
    - LMB / RMB to select block, to set win blocks / locked blocks / reset blocks
*/

void editorMode(TickInput *tick_input)
{
    Entity* player = &next_world_state.player;
    Entity* pack = &next_world_state.pack;

    if (editor_state.editor_mode == PLACE_BREAK)
    {
        if (tick_input->left_mouse_press || tick_input->right_mouse_press || tick_input->middle_mouse_press || tick_input->r_press || tick_input->f_press || tick_input->h_press || tick_input->g_press)
        {
            Vec3 neg_z_basis = {0, 0, -1};
            RaycastHit raycast_output = raycastHitCube(camera_with_ow_offset.coords, vec3RotateByQuaternion(neg_z_basis, camera_with_ow_offset.rotation), MAX_RAYCAST_SEEK_LENGTH);

            if ((tick_input->left_mouse_press || tick_input->f_press) && raycast_output.hit) 
            {
                Entity *entity= getEntityAtCoords(raycast_output.hit_coords);
                if (entity != 0)
                {
                    entity->coords = (Int3){0};
                    entity->position_norm = (Vec3){0};
                    entity->removed = true;

                    // TODO: if deleting entity, go through reset blocks and remove from reset block
                }
                setTileType(NONE, raycast_output.hit_coords);
                setTileDirection(NORTH, raycast_output.hit_coords);
            }
            else if ((tick_input->right_mouse_press || tick_input->h_press) && raycast_output.hit) 
            {
                if (!intCoordsWithinLevelBounds(raycast_output.place_coords)) return;
                if (isSource(editor_state.picked_tile)) 
                {
                    setTileType(editor_state.picked_tile, raycast_output.place_coords); 
                    setEntityInstanceInGroup(next_world_state.sources, raycast_output.place_coords, NORTH, getEntityColor(raycast_output.place_coords)); 
                    setTileDirection(editor_state.picked_direction, raycast_output.place_coords);
                }
                else if (editor_state.picked_tile == PLAYER) editorPlaceOnlyInstanceOfTile(player, raycast_output.place_coords, PLAYER, PLAYER_ID);
                else if (editor_state.picked_tile == PACK) editorPlaceOnlyInstanceOfTile(pack, raycast_output.place_coords, PACK, PACK_ID);
                else
                {
                    Entity* entity_group = 0;
                    switch (editor_state.picked_tile)
                    {
                        case BOX:     	   entity_group = next_world_state.boxes;    	  break;
                        case MIRROR:  	   entity_group = next_world_state.mirrors;  	  break;
                        case GLASS: 	   entity_group = next_world_state.glass_blocks;  break;
                        case WIN_BLOCK:    entity_group = next_world_state.win_blocks;    break;
                        case LOCKED_BLOCK: entity_group = next_world_state.locked_blocks; break;
                        case RESET_BLOCK:  entity_group = next_world_state.reset_blocks;  break;
                        default: entity_group = 0;
                    }
                    if (entity_group != 0) setEntityInstanceInGroup(entity_group, raycast_output.place_coords, NORTH, NO_COLOR);
                    setTileType(editor_state.picked_tile, raycast_output.place_coords);

                    if (editor_state.picked_tile != VOID && editor_state.picked_tile != WATER && editor_state.picked_tile != GRID) 
                    {
                        setTileDirection(editor_state.picked_direction, raycast_output.place_coords);
                    }
                    else 
                    {
                        setTileDirection(NORTH, raycast_output.place_coords);
                    }
                }
            }
            else if (tick_input->r_press && raycast_output.hit)
            {   
                TileType tile = getTileType(raycast_output.hit_coords);
                if (isEntity(tile) && tile != VOID && tile != WATER)
                {
                    Direction direction = getTileDirection(raycast_output.hit_coords);
                    if (direction == DOWN) direction = NORTH;
                    else direction++;
                    setTileDirection(direction, raycast_output.hit_coords);
                    Entity *entity = getEntityAtCoords(raycast_output.hit_coords);
                    if (entity != 0)
                    {
                        entity->direction = direction;
                        entity->rotation_quat = directionToQuaternion(direction, true);
                    }
                }
                else if (tile == LADDER)
                {
                    Direction direction = getTileDirection(raycast_output.hit_coords);
                    if (direction == EAST) direction = NORTH;
                    else direction++;
                    setTileDirection(direction, raycast_output.hit_coords);
                }
            }
            else if ((tick_input->middle_mouse_press || tick_input->g_press) && raycast_output.hit) editor_state.picked_tile = getTileType(raycast_output.hit_coords);

            time_until_allow_meta_input = PLACE_BREAK_TIME_UNTIL_ALLOW_INPUT;
        }
        if (tick_input->l_press)
        {
            editor_state.picked_tile++;
            if (editor_state.picked_tile == LADDER + 1) editor_state.picked_tile = VOID;
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }
        if (tick_input->m_press)
        {
            clearSolvedLevels();
            createDebugPopup("solved levels cleared", NO_TYPE);
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }
    }

    if (editor_state.editor_mode == SELECT)
    {
        if (tick_input->left_mouse_press)
        {
            Vec3 neg_z_basis = {0, 0, -1};
            RaycastHit raycast_output = raycastHitCube(camera_with_ow_offset.coords, vec3RotateByQuaternion(neg_z_basis, camera_with_ow_offset.rotation), MAX_RAYCAST_SEEK_LENGTH);

            if (isEntity(getTileType(raycast_output.hit_coords)))
            {
                editor_state.selected_id = getEntityId(raycast_output.hit_coords);
                editor_state.selected_coords = raycast_output.hit_coords;
            }
            else
            {
                editor_state.selected_id = -1;
            }
        }

        else if (tick_input->right_mouse_press)
        {
            Vec3 neg_z_basis = {0, 0, -1};
            RaycastHit raycast_output = raycastHitCube(camera_with_ow_offset.coords, vec3RotateByQuaternion(neg_z_basis, camera_with_ow_offset.rotation), MAX_RAYCAST_SEEK_LENGTH);
            Entity* rb = 0;
            if (editor_state.selected_id > 0) rb = getEntityFromId(editor_state.selected_id);
            if (rb != 0 && getTileType(rb->coords) == RESET_BLOCK)
            {
                Entity* new_e = getEntityAtCoords(raycast_output.hit_coords);
                if (new_e != 0)
                {
                    int32 present_in_rb = -1;
                    FOR(present_check_index, MAX_RESET_COUNT) if (rb->reset_info[present_check_index].id == new_e->id) 
                    {
                        present_in_rb = present_check_index;
                        break;
                    }
                    if (present_in_rb == -1)
                    {
                        int32 next_free = findNextFreeInResetBlock(rb);
                        if (next_free != -1) 
                        {
                            rb->reset_info[next_free].id = new_e->id;
                            rb->reset_info[next_free].start_coords = new_e->coords;
                            rb->reset_info[next_free].start_type = editor_state.picked_tile;
                            rb->reset_info[next_free].start_direction = new_e->direction;
                        }
                    }
                    else
                    {
                        rb->reset_info[present_in_rb].id = -1;
                    }
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                }
                // did not click on entity
            }
        }

        else if (editor_state.selected_id > 0)
        {
            if (tick_input->l_press) 
            {
                editor_state.editor_mode = SELECT_WRITE;
                editor_state.writing_field = WRITING_FIELD_NEXT_LEVEL;
            }
            else if (tick_input->b_press)
            {
                editor_state.editor_mode = SELECT_WRITE;
                editor_state.writing_field = WRITING_FIELD_UNLOCKED_BY;
            }
            else if (tick_input->q_press && editor_state.selected_id / ID_OFFSET_WIN_BLOCK * ID_OFFSET_WIN_BLOCK == ID_OFFSET_WIN_BLOCK)
            {
				Entity* wb = getEntityFromId(editor_state.selected_id);
                if (wb->next_level[0] != 0)
                {
                    levelChangePrep(wb->next_level);
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                    gameInitializeState(wb->next_level);
                    writeSolvedLevelsToFile();
                }
            }
        }
    }
}

// GAME LOGIC

void gameFrame(double delta_time, TickInput tick_input)
{	
    if (delta_time > 0.1) delta_time = 0.1;
    physics_accumulator += delta_time;

    draw_command_count = 0;

    Entity* player = &next_world_state.player;
    Entity* pack = &next_world_state.pack;

    // camera input (always at 60hz)
    if (editor_state.editor_mode != NO_MODE)
    {
        camera.yaw += tick_input.mouse_dx * CAMERA_SENSITIVITY;
        if (camera.yaw >  0.5f * TAU) camera.yaw -= TAU; 
        if (camera.yaw < -0.5f * TAU) camera.yaw += TAU; 
        camera.pitch += tick_input.mouse_dy * CAMERA_SENSITIVITY;
        float pitch_limit = 0.25f * TAU;
        if (camera.pitch >  pitch_limit) camera.pitch =  pitch_limit; 
        if (camera.pitch < -pitch_limit) camera.pitch = -pitch_limit; 
        camera.rotation = buildCameraQuaternion(camera);
    }
    if (editor_state.editor_mode != NO_MODE && editor_state.editor_mode != SELECT_WRITE)
    {
        Vec3 right_camera_basis, forward_camera_basis;
        cameraBasisFromYaw(camera.yaw, &right_camera_basis, &forward_camera_basis);

        if (editor_state.editor_mode == PLACE_BREAK || editor_state.editor_mode == SELECT)
        {
            if (tick_input.w_press) 
            {
                camera.coords.x += forward_camera_basis.x * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
                camera.coords.z += forward_camera_basis.z * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            }
            if (tick_input.a_press) 
            {
                camera.coords.x -= right_camera_basis.x * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
                camera.coords.z -= right_camera_basis.z * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            }
            if (tick_input.s_press) 
            {
                camera.coords.x -= forward_camera_basis.x * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
                camera.coords.z -= forward_camera_basis.z * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            }
            if (tick_input.d_press) 
            {
                camera.coords.x += right_camera_basis.x * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
                camera.coords.z += right_camera_basis.z * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            }
            if (tick_input.space_press) camera.coords.y += CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            if (tick_input.shift_press) camera.coords.y -= CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
        }
    }

    // handle writing once per present frame
    if (editor_state.editor_mode == SELECT_WRITE)
    {
        char (*writing_to_field)[64] = 0;
        Entity* e = getEntityFromId(editor_state.selected_id);
        if 		(editor_state.writing_field == WRITING_FIELD_NEXT_LEVEL)  writing_to_field = &e->next_level;
        else if (editor_state.writing_field == WRITING_FIELD_UNLOCKED_BY) writing_to_field = &e->unlocked_by;

        if (tick_input.enter_pressed_this_frame)
        {
            memset(*writing_to_field, 0, sizeof(*writing_to_field));
            memcpy(*writing_to_field, editor_state.edit_buffer.string, sizeof(*writing_to_field) - 1);

            world_state = next_world_state; // this is a bit messy...

            editor_state.editor_mode = SELECT;
            editor_state.selected_id = 0;
            editor_state.writing_field = NO_WRITING_FIELD;
        }
        else if (tick_input.escape_press)
        {
            editor_state.editor_mode = SELECT;
            editor_state.selected_id = 0;
            editor_state.writing_field = NO_WRITING_FIELD;
        }

        updateTextInput(&tick_input);
    }
    else
    {
        memset(&editor_state.edit_buffer, 0, sizeof(editor_state.edit_buffer));
    }

    // mode toggle
    if (editor_state.editor_mode != SELECT_WRITE)
    {
        if (tick_input.zero_press) 
        {
            editor_state.editor_mode = NO_MODE;
            createDebugPopup("game mode", GAMEPLAY_MODE_CHANGE);
        }
        if (tick_input.one_press) 
        {
            editor_state.editor_mode = PLACE_BREAK;
            createDebugPopup("place / break mode", GAMEPLAY_MODE_CHANGE);
        }
        if (tick_input.two_press) 
        {
            editor_state.editor_mode = SELECT;
            createDebugPopup("select mode", GAMEPLAY_MODE_CHANGE);
        }
    }

    // speed up / slow down physics tick
    if (tick_input.dot_press && time_until_allow_meta_input == 0 && editor_state.editor_mode != SELECT_WRITE)
    {
        char timestep_text[256] = {0};
        if (physics_timestep > DEFAULT_PHYSICS_TIMESTEP)
        {
            physics_timestep /= 2;
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            snprintf(timestep_text, sizeof(timestep_text), "physics timestep increased (%f)", physics_timestep);
            createDebugPopup(timestep_text, PHYSICS_TIMESTEP_CHANGE);
        }
        else
        {
            createDebugPopup("physics timestep already at minimum!", PHYSICS_TIMESTEP_CHANGE);
        }
    }
    else if (tick_input.comma_press && time_until_allow_meta_input == 0 && editor_state.editor_mode != SELECT_WRITE)
    {
        physics_timestep *= 2;
        time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        char timestep_text[256] = {0};
        snprintf(timestep_text, sizeof(timestep_text), "physics timestep decreased (%f)", physics_timestep);
        createDebugPopup(timestep_text, PHYSICS_TIMESTEP_CHANGE);
    }

    if (tick_input.backspace_press && time_until_allow_meta_input == 0 && editor_state.editor_mode != SELECT_WRITE)
    {
        camera = saved_main_camera;
        camera.rotation = buildCameraQuaternion(camera);
        camera_mode = MAIN_WAITING;
        camera_lerp_t = 0.0f;
        createDebugPopup("returned camera to saved position", NO_TYPE);
        time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
    }

    while (physics_accumulator >= physics_timestep)
   	{
		next_world_state = world_state;

        debug_text_count = 0; // TODO: does this need to be global? or can it just be in this / one larger scope
        bool silence_unlocks_due_to_restart_or_undo = false;

        if (editor_state.editor_mode == NO_MODE)
        {
            // assuming game undo and restart (still no undo in editor)
            if (time_until_allow_game_input == 0 && tick_input.z_press)
            {
                int32 undo_animation_time = 0;
                if (undos_performed == 0) undo_animation_time = 10;
                else if (undos_performed <= 2) undo_animation_time = 8;
                else if (undos_performed <= 4) undo_animation_time = 7;
                else if (undos_performed <= 8) undo_animation_time = 6;
                else if (undos_performed <= 12) undo_animation_time = 5;
                else undo_animation_time = 5;
                int32 real_undo_animation_time = undo_animation_time > 8 ? 5 : undo_animation_time; // clamp at 7 frames to perform undo, and allow stand still before undo next; want slower at start, but too slow undo looks awkward.
                if (performUndo(real_undo_animation_time))
                {
                    updatePackDetached();
                    undos_performed++;
                }
                silence_unlocks_due_to_restart_or_undo = true;
                time_until_allow_game_input = undo_animation_time;
            }
            if (time_until_allow_game_input == 0 && tick_input.r_press)
            {
                // restart
                if (!restart_last_turn) 
                {
                    recordActionForUndo(&world_state, true, false, false);
                }
                createDebugPopup("level restarted", NO_TYPE);
                memset(animations, 0, sizeof(animations));
                Camera save_camera = camera;

                gameInitializeState(next_world_state.level_name);

                if (in_overworld)
                {
                    // copy world state from overworld_zero, but save the solved levels and overwrite the level name
					char persist_solved_levels[64][64];
                    memcpy(&persist_solved_levels, &next_world_state.solved_levels, sizeof(char) * 64 * 64);
                    memcpy(&next_world_state, &overworld_zero, sizeof(WorldState));
                    memcpy(&next_world_state.solved_levels, &persist_solved_levels, sizeof(char) * 64 * 64);
                    memcpy(&next_world_state.level_name, "overworld", sizeof(char) * 64);

                    // set player and pack position based on game progress. assumes pack is always attached and player always faces north after a restart in overworld

                    Int3 player_restart_coords = {0};
                    switch (game_progress)
                	{
                        case WORLD_0: player_restart_coords = (Int3){ 58, 2, 232 }; break;
                        case WORLD_1: player_restart_coords = (Int3){ 58, 2, 197 }; break;
                        case GATE_1:  player_restart_coords = (Int3){ 58, 2, 189 }; break;
                    }
                    moveEntityInBufferAndState(player, player_restart_coords, NORTH);
                    setEntityVecsFromInts(player);
                    moveEntityInBufferAndState(pack, getNextCoords(player->coords, SOUTH), NORTH);
                    setEntityVecsFromInts(pack);
                }
              	camera = save_camera; 
                restart_last_turn = true;
                silence_unlocks_due_to_restart_or_undo = true;
                time_until_allow_game_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
			if (time_until_allow_game_input == 0 && tick_input.escape_press && !in_overworld)
            {
                // leave current level if not in overworld. TODO: why is saving solved levels required here?
                char save_solved_levels[64][64] = {0};
                memcpy(save_solved_levels, next_world_state.solved_levels, sizeof(save_solved_levels));
                levelChangePrep("overworld");
                time_until_allow_game_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                gameInitializeState("overworld");
                memcpy(next_world_state.solved_levels, save_solved_levels, sizeof(save_solved_levels));
                writeSolvedLevelsToFile();
            }

            if (time_until_allow_game_input == 0 && (tick_input.w_press || tick_input.a_press || tick_input.s_press || tick_input.d_press) && player->in_motion == 0 && player->in_motion == 0 && !player->removed)
            {
				// MOVEMENT 
                Direction input_direction = 0;
                Int3 next_player_coords = {0};
                if 		(tick_input.w_press) input_direction = NORTH; 
                else if (tick_input.a_press) input_direction = WEST; 
                else if (tick_input.s_press) input_direction = SOUTH; 
                else if (tick_input.d_press) input_direction = EAST; 

                if (input_direction == player->direction)
                {
                    if (calculateGhosts())
                    {
                        // seek towards start of laser to get endpoint, and then go to the endpoint
                        // check if endpoint is valid before teleport (i.e, if pack can go there. if over air, teleport anyway)

                        bool allow_tp = false;
                        TileType player_ghost_tile = getTileType(player_ghost_coords);
                        TileType pack_ghost_tile = getTileType(pack_ghost_coords);
                        if ((player_ghost_tile == NONE || player_ghost_tile == PLAYER) && (pack_ghost_tile == NONE || pack_ghost_tile == PLAYER || pack_ghost_tile == PACK)) allow_tp = true;

                        if (allow_tp)
                        {
                            if (!int3IsEqual(player_ghost_coords, player->coords))
                            {
                                recordActionForUndo(&world_state, false, true, false);

                                moveEntityInBufferAndState(player, player_ghost_coords, player_ghost_direction);
                                setEntityVecsFromInts(player);
                                zeroAnimations(PLAYER_ID);
                                if (!pack_detached)
                                {
                                    moveEntityInBufferAndState(pack, pack_ghost_coords, pack_ghost_direction);
                                    setEntityVecsFromInts(pack);
                                    zeroAnimations(PACK_ID);
                                }
                            }
                            // tp sends player ontop of themselves - should count as a successful tp, but no point changing state, and don't samve to undo buffer.
                            time_until_allow_game_input = SUCCESSFUL_TP_TIME;
                        }
                        else
                        {
                            // tp obstructed
                            time_until_allow_game_input = FAILED_TP_TIME;
                        }

                        updateLaserBuffer();
                    }
                    else
                    {
                        // no ghosts, but still need to check if player is green at all 
                        bool do_push = false;
                        bool move_player = false;
                        bool climb = false;
                        bool do_failed_animations = false;
                        int32 animation_time = 0;
                        if 		(tick_input.w_press) next_player_coords = int3Add(player->coords, int3Negate(AXIS_Z));
                        else if (tick_input.a_press) next_player_coords = int3Add(player->coords, int3Negate(AXIS_X));
                        else if (tick_input.s_press) next_player_coords = int3Add(player->coords, AXIS_Z);
                        else if (tick_input.d_press) next_player_coords = int3Add(player->coords, AXIS_X);
                        TileType next_tile = getTileType(next_player_coords);
                        if (!player_will_fall_next_turn) switch (next_tile)
                        {
                            case NONE:
                            {
                                Int3 coords_ahead = next_player_coords;
                                Int3 coords_below_and_ahead = getNextCoords(next_player_coords, DOWN);
                                if (isPushable(getTileType(coords_ahead)) && getEntityAtCoords(coords_ahead)->in_motion) move_player = false;
                                else if (isPushable(getTileType(coords_below_and_ahead)) && getEntityAtCoords(coords_below_and_ahead)->moving_direction != NO_DIRECTION) move_player = false;
                                else
                                {
                                    move_player = true;
                                    animation_time = MOVE_OR_PUSH_ANIMATION_TIME;
                                }
                                break;
                            }
                            case BOX:
                            case GLASS:
                            case PACK:
                            case MIRROR:
                            case SOURCE_RED:
                            case SOURCE_GREEN:
                            case SOURCE_BLUE:
                            case SOURCE_MAGENTA:
                            case SOURCE_YELLOW:
                            case SOURCE_CYAN:
                            case SOURCE_WHITE:
                            {
                                // figure out if push, pause, or fail here.
                            	PushResult push_check = canPushStack(next_player_coords, input_direction);
                                if (push_check == CAN_PUSH) 
                                {
                                    do_push = true;
                                    move_player = true;
                                    animation_time = MOVE_OR_PUSH_ANIMATION_TIME;
                                }
                                else if (push_check == FAILED_PUSH) do_failed_animations = true;
                                break;
                            }
                            case LADDER:
                            {
                                if (getTileDirection(next_player_coords) == oppositeDirection(player->direction)) climb = true;
                                else do_failed_animations = true;
                                break;
                            }
                            default:
                            {
                                do_failed_animations = true;
                                break;
                            }
                        }
						if (move_player)
                        {
                            // don't allow walking off edge
                            Int3 coords_below_next_coords = getNextCoords(next_player_coords, DOWN);
                            TileType tile_below_next_coords = getTileType(coords_below_next_coords);

                            bool allow_movement = true;
                            if (tile_below_next_coords == NONE && !player->hit_by_red) allow_movement = false;
                            if (isEntity(tile_below_next_coords) && getEntityAtCoords(coords_below_next_coords)->moving_direction != NO_DIRECTION) allow_movement = false;

                            if (allow_movement || cheating)
                            {
                                if (do_push) pushAll(next_player_coords, input_direction, animation_time, true, false);
                                doStandardMovement(input_direction, next_player_coords, animation_time, true);
                                time_until_allow_game_input = animation_time;
                            }
                            else
                            {
                                // leap of faith logic
                                memcpy(&leap_of_faith_snapshot, &world_state, sizeof(WorldState));

                                if (do_push) pushAll(next_player_coords, input_direction, 0, false, false);

                                if (!player->hit_by_blue)
                                {
                                    bool animations_on = false;
                                    doFallingObjects(animations_on);
                                    if (pack_detached) doFallingEntity(pack, animations_on);
                                    doHeadMovement(input_direction, animations_on, 1);
                                }

                                moveEntityInBufferAndState(player, next_player_coords, player->direction);

                                if (!pack_detached)
                                {
                                    Int3 next_pack_coords = getNextCoords(next_player_coords, oppositeDirection(input_direction));
                                    moveEntityInBufferAndState(pack, next_pack_coords, pack->direction);
                                }

                                updateLaserBuffer();

                                bool leap_of_faith_worked = false;
                                if (player->hit_by_red) leap_of_faith_worked = true;
                                //next_world_state = world_state_savestate;
                                memcpy(&next_world_state, &leap_of_faith_snapshot, sizeof(WorldState));
                                if (leap_of_faith_worked)
                                {
                                    if (do_push) pushAll(next_player_coords, input_direction, animation_time, true, false);
                                    doStandardMovement(input_direction, next_player_coords, animation_time, true);
                                    bypass_player_fall = true; 
                                    time_until_allow_game_input = MOVE_OR_PUSH_ANIMATION_TIME;
                                }
                                else 
                                {
                                    resetPlayerAndPackMotion();
                                    doFailedWalkAnimations(player->direction);
                                    time_until_allow_game_input = FAILED_ANIMATION_TIME;
                                    updateLaserBuffer();
                                }
                            }
                        }
						else if (climb)
                        {
                            bool can_climb = false;
                            Int3 coords_above = getNextCoords(player->coords, UP);

                            if (getTileType(coords_above) == NONE)
                            {
                                can_climb = true;
                            }
                            else if (isPushable(getTileType(coords_above)))
                            {
                                if (canPushUp(coords_above) == CAN_PUSH) 
                                {
                                    pushUp(coords_above, CLIMB_ANIMATION_TIME);
                                    can_climb = true;
                                }
                            }

                            if (can_climb)
                            {
                                moveEntityInBufferAndState(player, coords_above, player->direction);
			
                                createInterpolationAnimation(intCoordsToNorm(getNextCoords(player->coords, DOWN)),
                                                             intCoordsToNorm(player->coords),
                                                             &player->position_norm,
                                                             IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                             PLAYER_ID, CLIMB_ANIMATION_TIME);

                                player->in_motion = CLIMB_ANIMATION_TIME;
                                player->moving_direction = UP;

                                if (!pack_detached)
                                {
                                    moveEntityInBufferAndState(pack, getNextCoords(pack->coords, UP), pack->direction);

                                    createInterpolationAnimation(intCoordsToNorm(getNextCoords(pack->coords, DOWN)),
                                                                 intCoordsToNorm(pack->coords),
                                                                 &pack->position_norm,
                                                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                                 PACK_ID, CLIMB_ANIMATION_TIME);

                                    pack->in_motion = CLIMB_ANIMATION_TIME;
                                    pack->moving_direction = UP;
                                }

                                recordActionForUndo(&world_state, false, false, true);

                                time_until_allow_game_input = CLIMB_ANIMATION_TIME + MOVE_OR_PUSH_ANIMATION_TIME;
                            }
                            else
                            {
                                //doFailedClimbUpAnimation();
                                //time_until_allow_game_input = FAILED_CLIMB_TIME;
                            }
                        }
						else if (do_failed_animations) 
                        {
                            resetPlayerAndPackMotion();
                            doFailedWalkAnimations(player->direction);
                            time_until_allow_game_input = FAILED_ANIMATION_TIME;
                        }
                    }
                }
                else if (input_direction != oppositeDirection(player->direction)) // check if turning (as opposed to trying to reverse)
                {
                    // player is turning

                    bool allow_turn = true;
                    if (getTileType(getNextCoords(player->coords, DOWN)) == NONE && !player->hit_by_red) allow_turn = false;
                    if (allow_turn || cheating)
                    {
                        Direction polarity_direction = NORTH;
                        int32 clockwise = false;
                        int32 clockwise_calculation = player->direction - input_direction;
                        if (clockwise_calculation == -1 || clockwise_calculation == 3) clockwise = true;

                        if (pack_detached)
                        {
                            // if pack detached, always allow turn
                            if (isPushable(getTileType(getNextCoords(player->coords, UP)))) 
                            {
                                if (!player->hit_by_blue) doHeadRotation(clockwise);
                            }

                            createInterpolationAnimation(VEC3_0, VEC3_0, 0, 
                                                         directionToQuaternion(player->direction, true), 
                                                         directionToQuaternion(input_direction, true), 
                                                         &player->rotation_quat,
                                                         1, TURN_ANIMATION_TIME); 

                            player->direction = input_direction;
                            setTileDirection(player->direction, player->coords);
                            player->moving_direction = NO_DIRECTION;

                            recordActionForUndo(&world_state, false, false, false);
                        }
                        else
                        {
                            if (clockwise) polarity_direction = (input_direction + 1) % 4;
                            else 		   polarity_direction = (input_direction + 3) % 4;

                            Int3 orthogonal_coords = getNextCoords(player->coords, oppositeDirection(input_direction));			
                            Int3 diagonal_coords = getNextCoords(orthogonal_coords, polarity_direction);
                            Direction diagonal_push_direction = oppositeDirection(input_direction);	
                            Direction orthogonal_push_direction = oppositeDirection(polarity_direction); 
                            
                            bool pause_turn = false;
                            TrailingHitbox _;
                            if (trailingHitboxAtCoords(diagonal_coords, &_)) pause_turn = true;
                            else if (isEntity(getTileType(orthogonal_coords)) && getEntityAtCoords(orthogonal_coords)->in_motion) pause_turn = true;
                            else if (isEntity(getTileType(diagonal_coords))   && getEntityAtCoords(diagonal_coords)->in_motion)   pause_turn = true;

                            if (!pause_turn)
                            {
                                TileType diagonal_tile_type = getTileType(diagonal_coords); 
                                TileType orthogonal_tile_type = getTileType(orthogonal_coords);

                                bool allow_turn_diagonal = false;
                                bool allow_turn_orthogonal = false;
                                bool push_diagonal = false;
                                bool push_orthogonal = false;

                                switch (diagonal_tile_type)
                                {
                                    case NONE:
                                    {
                                        allow_turn_diagonal = true;
                                        break;
                                    }
                                    case BOX:
                                    case GLASS:
                                    case MIRROR:

                                    case SOURCE_RED:
                                    case SOURCE_GREEN:
                                    case SOURCE_BLUE:
                                    case SOURCE_MAGENTA:
                                    case SOURCE_YELLOW:
                                    case SOURCE_CYAN:
                                    case SOURCE_WHITE:
                                    {
                                        PushResult push_result = canPushStack(diagonal_coords, diagonal_push_direction);
                                        if (push_result == CAN_PUSH)
                                        {
                                            push_diagonal = true;
                                            allow_turn_diagonal = true;
                                        }
                                        break;
                                    }
                                    default: break;
                                }

                                if (allow_turn_diagonal == true) switch (orthogonal_tile_type)
                                {
                                    case NONE:
                                    {
                                        allow_turn_orthogonal = true;
                                        break;
                                    }
                                    case BOX:
                                    case GLASS:
                                    case MIRROR:

                                    case SOURCE_RED:
                                    case SOURCE_GREEN:
                                    case SOURCE_BLUE:
                                    case SOURCE_MAGENTA:
                                    case SOURCE_YELLOW:
                                    case SOURCE_CYAN:
                                    case SOURCE_WHITE:
                                    {
                                        PushResult push_result = canPushStack(orthogonal_coords, orthogonal_push_direction);
                                        if (push_result == CAN_PUSH)
                                        {
                                            push_orthogonal = true;
                                            allow_turn_orthogonal = true;
                                        }
                                        break;
                                    }
                                    default: break;
                                }

                                if (allow_turn_diagonal || allow_turn_orthogonal)
                                {
                                    if (!allow_turn_orthogonal)
                                    {
                                        if (push_diagonal) 
                                        {
                                            recordActionForUndo(&world_state, false, false, false);
                                            pushAll(diagonal_coords, oppositeDirection(input_direction), PUSH_FROM_TURN_ANIMATION_TIME, true, false);
                                        }
                                        doFailedTurnAnimations(input_direction, clockwise);
                                    }
                                    else
                                    {
                                        recordActionForUndo(&world_state, false, false, false);

                                        createTrailingHitbox(pack->coords, input_direction, NO_DIRECTION, FIRST_TRAILING_PACK_TURN_HITBOX_TIME, PACK);

                                        if (isPushable(getTileType(getNextCoords(player->coords, UP)))) 
                                        {
                                            if (!player->hit_by_blue) doHeadRotation(clockwise);
                                        }

                                        createInterpolationAnimation(VEC3_0, VEC3_0, 0, 
                                                                     directionToQuaternion(player->direction, true), 
                                                                     directionToQuaternion(input_direction, true), 
                                                                     &player->rotation_quat,
                                                                     PLAYER_ID, TURN_ANIMATION_TIME); 

                                        player->direction = input_direction;
                                        setTileDirection(player->direction, player->coords);
                                        player->moving_direction = NO_DIRECTION;

                                        createPackRotationAnimation(intCoordsToNorm(player->coords), 
                                                                    intCoordsToNorm(pack->coords), 
                                                                    oppositeDirection(input_direction), clockwise, 
                                                                    &pack->position_norm, &pack->rotation_quat, PACK_ID, TURN_ANIMATION_TIME);

                                        if (push_diagonal)   pack_turn_state.do_diagonal_push_on_turn = true;
                                        if (push_orthogonal) pack_turn_state.do_orthogonal_push_on_turn = true;

                                        pack_turn_state.pack_intermediate_states_timer = TIME_BEFORE_ORTHOGONAL_PUSH_STARTS_IN_TURN + PACK_TIME_IN_INTERMEDIATE_STATE + 1;
                                        pack_turn_state.pack_intermediate_coords = diagonal_coords;
                                        pack_turn_state.pack_orthogonal_push_direction = orthogonal_push_direction;
                                        pack_turn_state.pack_hitbox_turning_to_coords = orthogonal_coords;
                                    }
                                }
                                else
                                {
                                    // failed turn animation
                                    doFailedTurnAnimations(input_direction, clockwise);
                                }
                            }
                        }
                    }
                    time_until_allow_game_input = TURN_ANIMATION_TIME;
        		}
                else if (input_direction == oppositeDirection(player->direction))
                {
                    if (!player_will_fall_next_turn)
                    {
                        // backwards movement: allow only when climbing down a ladder. right now just move, and let player fall (functionally the same, but animation is goofy)
                        Direction backwards_direction = oppositeDirection(player->direction);
                        Int3 coords_below = getNextCoords(player->coords, DOWN);
                        if (player->moving_direction == NO_DIRECTION && getTileType(coords_below) == LADDER && getTileDirection(coords_below) == input_direction && (pack_detached || (!pack_detached && getTileType(getNextCoords(pack->coords, DOWN)) == NONE)))
                        {
                            bool can_move = false;
                            bool do_push = false;
                            Int3 coords_behind_pack = getNextCoords(pack->coords, backwards_direction);
                            TileType tile_behind = getTileType(coords_behind_pack);
                            if (tile_behind == NONE)
                            {
                                can_move = true;
                            }
                            else if (isPushable(tile_behind))
                            {
                                can_move = true;
                                do_push = true;
                            }

                            if (can_move)
                            {
                                if (do_push)
                                {
                                    if (canPushStack(coords_behind_pack, backwards_direction) == CAN_PUSH) pushAll(coords_behind_pack, backwards_direction, MOVE_OR_PUSH_ANIMATION_TIME, true, false);
                                }
                                if (!player->hit_by_blue) doHeadMovement(backwards_direction, true, MOVE_OR_PUSH_ANIMATION_TIME);
            
                                // since moving backwards, move pack first so no accidental overlap during movement
                                if (!pack_detached)
                                {
                                    Int3 next_pack_coords = getNextCoords(pack->coords, backwards_direction);
                                    moveEntityInBufferAndState(pack, next_pack_coords, pack->direction);

                                    createInterpolationAnimation(intCoordsToNorm(getNextCoords(pack->coords, player->direction)),
                                                                 intCoordsToNorm(pack->coords),
                                                                 &pack->position_norm,
                                                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                                 PACK_ID, MOVE_OR_PUSH_ANIMATION_TIME);

                                    pack->in_motion = MOVE_OR_PUSH_ANIMATION_TIME;
                                    pack->moving_direction = backwards_direction;
                                    pack->first_fall_already_done = true;
                                }

                                next_player_coords = getNextCoords(player->coords, backwards_direction);
                                moveEntityInBufferAndState(player, next_player_coords, player->direction);

                                createInterpolationAnimation(intCoordsToNorm(getNextCoords(player->coords, player->direction)),
                                                             intCoordsToNorm(player->coords),
                                                             &player->position_norm,
                                                             IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                             PLAYER_ID, MOVE_OR_PUSH_ANIMATION_TIME);

                                player->in_motion = MOVE_OR_PUSH_ANIMATION_TIME;
                                player->moving_direction = backwards_direction;
                                player->first_fall_already_done = true;

                                recordActionForUndo(&world_state, false, false, false);

                                time_until_allow_game_input = MOVE_OR_PUSH_ANIMATION_TIME;
                            }
                            else
                            {
                                resetPlayerAndPackMotion();
                                doFailedWalkAnimations(oppositeDirection(player->direction));
                                time_until_allow_game_input = FAILED_ANIMATION_TIME;
                            }
                        }
                        else
                        {
                            resetPlayerAndPackMotion();
                            doFailedWalkAnimations(oppositeDirection(player->direction));
                            time_until_allow_game_input = FAILED_ANIMATION_TIME;
                        }
                    }
                }
            }
        }
        else
        {
            // if not in editor mode NO_MODE, go into editor mode fn
            if (time_until_allow_meta_input == 0) editorMode(&tick_input);
        }

        // pack turn sequence
        // the pack_intermediate_states_timer numbers control when during a turn does the backpack push things, and what tile(s) does the backpack occupy for the purposes of laser passthrough

        // numbers are magic and based on how long it takes for the pack to turn. this is because it's kind of hard to make a good looking generalization, e.g. just using 
        // fractions of TURN_ANIMATION_TIME because it looks awkward for small values of the animation (anything less than 20) so i just hard code these numbers.

        if (pack_turn_state.pack_intermediate_states_timer > 0)
        {
            if (pack_turn_state.pack_intermediate_states_timer == 7)
            {
                createTrailingHitbox(pack->coords, pack_turn_state.pack_orthogonal_push_direction, NO_DIRECTION, 4, PACK);
				if (pack_turn_state.do_diagonal_push_on_turn) pushAll(pack_turn_state.pack_intermediate_coords, oppositeDirection(player->direction), PUSH_FROM_TURN_ANIMATION_TIME, true, false);
            }
            else if (pack_turn_state.pack_intermediate_states_timer == 5)
            {
                moveEntityInBufferAndState(pack, pack_turn_state.pack_intermediate_coords, player->direction);
            }
            else if (pack_turn_state.pack_intermediate_states_timer == 4)
            {
                if (pack_turn_state.do_orthogonal_push_on_turn) pushAll(pack_turn_state.pack_hitbox_turning_to_coords, pack_turn_state.pack_orthogonal_push_direction, PUSH_FROM_TURN_ANIMATION_TIME, true, false);
            }
            else if (pack_turn_state.pack_intermediate_states_timer == 3)
            {
                moveEntityInBufferAndState(pack, pack_turn_state.pack_hitbox_turning_to_coords, pack->direction);
                createTrailingHitbox(pack_turn_state.pack_intermediate_coords, pack->direction, NO_DIRECTION, 3, PACK);
            }
            pack_turn_state.pack_intermediate_states_timer--;
        }

		updateLaserBuffer();

        // falling logic
        bool do_falling_logic = true;
        if (undos_performed != 0) do_falling_logic = false; // only do gravity if not currently holding the undo button.
        if (cheating) do_falling_logic = false;

        if (do_falling_logic)
        {
            if (!player->hit_by_blue) doFallingObjects(true);

            if (pack_turn_state.pack_intermediate_states_timer == 0)
            {
                if (!player->hit_by_red)
                {
                    if (!pack_detached)
                    {
                        if (getTileType(getNextCoords(player->coords, DOWN)) == NONE) player_will_fall_next_turn = true; 
                        else player_will_fall_next_turn = false;

                        // not red and pack attached: player always falls, if no bypass. pack only falls if player falls
                        if (!bypass_player_fall && !doFallingEntity(player, true))
                        {
                            doFallingEntity(pack, true);
                        }
                    }
                    else
                    {
                        if (getTileType(getNextCoords(player->coords, DOWN)) == NONE) player_will_fall_next_turn = true;
                        else player_will_fall_next_turn = false;
                        // not red and pack not attached, so pack and player both always fall
                        if (!bypass_player_fall) doFallingEntity(player, true);
                        doFallingEntity(pack, true);
                    }
                }
                else
                {
                    player_will_fall_next_turn = false;
                    // red, so pack only falls if is detached from player
                    if (pack_detached)
                    {
                        doFallingEntity(pack, true);
                    }
                }
            }
        }

        // climb logic
        if (player->moving_direction == UP && player->in_motion == 1)
        {
            bool do_push = false;
            bool can_move = false;
            bool try_climb_more = false;
            Int3 next_coords = getNextCoords(player->coords, player->direction);
            TileType tile_ahead = getTileType(next_coords);
            if (tile_ahead == NONE)
            {
				can_move = true;
            }
            else if (isPushable(tile_ahead)) 
            {
                if (canPushStack(next_coords, player->direction) == CAN_PUSH)
                {
                    can_move = true;
                    do_push = true;
                }
            }
            else if (tile_ahead == LADDER)
            {
                try_climb_more = true;
            }

            if (can_move)
            {
                int32 animation_time = MOVE_OR_PUSH_ANIMATION_TIME;
                if (do_push) pushAll(next_coords, player->direction, animation_time, true, false);
                doStandardMovement(player->direction, next_coords, animation_time, false);
                time_until_allow_game_input = animation_time;
            }
            else if (try_climb_more)
            {
				// assume works for now (breaks if solid block above)
                Int3 coords_above = getNextCoords(player->coords, UP);
                if (getTileType(coords_above) == NONE)
                {
                    can_move = true;
                    do_push = false; // not actually required
                }
                else if (isPushable(getTileType(coords_above)))
                {
                    if (canPushUp(coords_above) == CAN_PUSH)
                    {
                        can_move = true;
                        do_push = true;
                    }
                }
                if (can_move)
                {
                    if (do_push)
                    {
						pushUp(coords_above, CLIMB_ANIMATION_TIME);
                    }
                    moveEntityInBufferAndState(player, coords_above, player->direction);

                    createInterpolationAnimation(intCoordsToNorm(getNextCoords(player->coords, DOWN)),
                                                 intCoordsToNorm(player->coords),
                                                 &player->position_norm,
                                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                 PLAYER_ID, CLIMB_ANIMATION_TIME);

                    player->in_motion = CLIMB_ANIMATION_TIME;
                    player->moving_direction = UP;

                    if (!pack_detached)
                    {
                        Int3 coords_above_pack = getNextCoords(pack->coords, UP);
                        moveEntityInBufferAndState(pack, coords_above_pack, pack->direction);

                        createInterpolationAnimation(intCoordsToNorm(getNextCoords(pack->coords, DOWN)),
                                                     intCoordsToNorm(pack->coords),
                                                     &pack->position_norm,
                                                     IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                     PACK_ID, CLIMB_ANIMATION_TIME);

                        pack->in_motion = CLIMB_ANIMATION_TIME;
                        pack->moving_direction = UP;
                    }
                    time_until_allow_game_input = CLIMB_ANIMATION_TIME + MOVE_OR_PUSH_ANIMATION_TIME;
                }
            }
            else
            {
                // can't move or climb more
                resetPlayerAndPackMotion();
                doFailedWalkAnimations(player->direction);
                time_until_allow_game_input = FAILED_ANIMATION_TIME;
            }
        }

        // pack detach logic
        TileType tile_behind_player = getTileType(getNextCoords(player->coords, oppositeDirection(player->direction)));
        if (!pack_detached && pack_turn_state.pack_intermediate_states_timer == 0 && tile_behind_player != PACK) pack_detached = true;
        else if (pack_detached && tile_behind_player == PACK) pack_detached = false;

        // do animations
        // the animation system is just an array, where the frames_left decrement each frame. 
        // animations[index].position[frames left of animation] is the position that writes to animations[index].position_to_change, which is a pointer to the position_norm field of an entity.
        // same goes for rotation. this whole system will be changed when i have more sophisticated animations
		for (int animation_index = 0; animation_index < MAX_ANIMATION_COUNT; animation_index++)
        {
            Animation* a = &animations[animation_index];
			if (a->frames_left == 0) continue;
			if (a->position_to_change != 0) 
            {
                Vec3 next_pos = a->position[a->frames_left-1];
                if (!vec3IsZero(next_pos)) *a->position_to_change = next_pos; 
                //*a->position_to_change = a->position[a->frames_left-1];
            }
			if (a->rotation_to_change != 0) 
            {
				*a->rotation_to_change = a->rotation[a->frames_left-1];
            }
            animations[animation_index].frames_left--;
        }

        // decrement in_motion / moving_direction and reset first_fall_already_done for pushable entities, if they should be reset
        Entity* falling_entity_groups[6] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.glass_blocks, next_world_state.sources, next_world_state.win_blocks, next_world_state.reset_blocks };
        FOR(falling_object_index, 6)
        {
            Entity* entity_group = falling_entity_groups[falling_object_index];
            FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT) 
            {
                changeMoving(&entity_group[entity_index]);
                resetFirstFall(&entity_group[entity_index]);
            }
        }
        changeMoving(player);
        resetFirstFall(player);
		changeMoving(pack);
        resetFirstFall(pack);
        if (pack_turn_state.pack_intermediate_states_timer > 0)
        {
            pack->in_motion = 0;
            pack->moving_direction = NO_DIRECTION;
        }
        // if player red, reset first fall timer (and pack, if it is attached)
        if (player->hit_by_red)
		{
            player->first_fall_already_done = false;
            if (!pack_detached) pack->first_fall_already_done = false;
        }

        // delete player / pack if above void
        if (!player->hit_by_red)
        {
            if ((getTileType(getNextCoords(player->coords, DOWN)) == VOID || getTileType(getNextCoords(player->coords, DOWN)) == WATER) && !presentInAnimations(PLAYER_ID)) 
            {
                setTileType(NONE, player->coords);
                setTileDirection(NORTH, player->coords);
                player->removed = true;
            }
        }
        if (pack_detached)
        {
            if (!player->hit_by_blue)
            {
                if ((getTileType(getNextCoords(pack->coords, DOWN)) == VOID || getTileType(getNextCoords(pack->coords, DOWN)) == WATER) && !presentInAnimations(PACK_ID)) 
                {
                    setTileType(NONE, pack->coords);
                    setTileDirection(NORTH, pack->coords);
                    pack->removed = true;
                }
            }
        }
        else
        {
            if (!player->hit_by_red)
            {
                if ((getTileType(getNextCoords(pack->coords, DOWN)) == VOID || getTileType(getNextCoords(pack->coords, DOWN)) == WATER) && !presentInAnimations(PACK_ID))
                {
                    setTileType(NONE, pack->coords);
                    setTileDirection(NORTH, pack->coords);
                    pack->removed = true;
                }
            }
        }

        // win block logic
        if (getTileType(getNextCoords(player->coords, DOWN)) == WIN_BLOCK)
        {
            if (tick_input.q_press && time_until_allow_game_input == 0)
            {
                // go to win_block.next_level if conditions are met
                Entity* wb = getEntityAtCoords(getNextCoords(player->coords, DOWN));
                bool do_win_block_usage = true;
                if (editor_state.editor_mode != NO_MODE) do_win_block_usage = false;
                if (pack_detached) do_win_block_usage = false;
                if (wb->locked) do_win_block_usage = false;
                if (wb->next_level[0] == 0) do_win_block_usage = false; // don't go through if there is no next level here yet

                if (do_win_block_usage)
                {
                    if (in_overworld) 
                    {
                        char level_path[64] = {0};
                        buildLevelPathFromName(next_world_state.level_name, &level_path, false);
                        saveLevelRewrite(level_path, false);
                        if (camera_mode == ALT_WAITING) 
                        {
                            saved_overworld_camera = saved_alt_camera;
                            saved_overworld_camera_mode = ALT_WAITING;
                        }
                        else 
                        {
                            saved_overworld_camera = saved_main_camera;
                            saved_overworld_camera_mode = MAIN_WAITING;
                        }
                    }
					zeroAnimations(PLAYER_ID);
                    zeroAnimations(PACK_ID);
                    levelChangePrep(wb->next_level);
                    time_until_allow_game_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                    gameInitializeState(wb->next_level);
                }
            }
            else if (tick_input.f_press && time_until_allow_game_input == 0)
            {
                // add win_block.next_level to solved_levels. this is a debug bind
                bool solve_level = true;
                if (editor_state.editor_mode != NO_MODE) solve_level = false;

                if (solve_level)
                {
                    Entity* wb = getEntityAtCoords(getNextCoords(player->coords, DOWN));
                    if (findInSolvedLevels(wb->next_level) == -1)
                    {
                        int32 next_free = nextFreeInSolvedLevels(&next_world_state.solved_levels);
                        strcpy(next_world_state.solved_levels[next_free], wb->next_level);
                    }
                    writeSolvedLevelsToFile();
                    createDebugPopup("level solved!", NO_TYPE);
                    time_until_allow_game_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                }
            }
        }
        
		// locked block logic
        // TODO: currently iterating this every frame on every entity, which is pretty wasteful. should instead just change this if some action that could impact locked-ness happened that frame.
        Entity* entity_group[4] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.win_blocks, next_world_state.sources };
        FOR(group_index, 4)
        {
            FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
            {
                Entity* e = &entity_group[group_index][entity_index];
                if (e->unlocked_by[0] == '\0') e->locked = false;
                if (findInSolvedLevels(e->unlocked_by) == -1) e->locked = true; 
                else e->locked = false;
            }
        }
        FOR(locked_block_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* lb = &next_world_state.locked_blocks[locked_block_index];
            if (lb->id == -1) continue;
            int32 find_result = findInSolvedLevels(lb->unlocked_by);
            if (find_result == INT32_MAX) continue;
            if (find_result != -1 && !lb->removed)
            {
                // locked block to be unlocked
                lb->removed = true;
                if (getTileType(lb->coords) == LOCKED_BLOCK)
                {
                    setTileType(NONE, lb->coords);
                    setTileDirection(NORTH, lb->coords);
                }
                if (!silence_unlocks_due_to_restart_or_undo) createDebugPopup("something was unlocked!", NO_TYPE);
            }
            else if (find_result == -1 && lb->removed)
            {
                lb->removed = false;
                setTileType(LOCKED_BLOCK, lb->coords);
                setTileDirection(NORTH, lb->coords);
            }
        }

        // MISC STUFF

        // decrement trailing hitboxes 
        FOR(i, MAX_TRAILING_HITBOX_COUNT) if (trailing_hitboxes[i].frames > 0) trailing_hitboxes[i].frames--;
        if (bypass_player_fall) bypass_player_fall = false;

        // reset undos performed if no longer holding z undos
        if (undos_performed > 0 && !tick_input.z_press) undos_performed = 0;

        // decide which ghosts to render, if ghosts should be rendered
        do_player_ghost = false;
        do_pack_ghost = false;
		if (calculateGhosts())
        {
            do_player_ghost = true;
            if (!pack_detached) do_pack_ghost = true;
        }

        // update gameProgress based on which levels are solved, and current coords of the player
        if (findInSolvedLevels("red-last") != -1)
        {
			if (player->coords.z <= 189) game_progress = GATE_1;
			else game_progress = WORLD_1;
        }
        else game_progress = WORLD_0;

        // final redo of laser buffer, after all logic is complete, for drawing // TODO: which of these calls throughout the code are now needed?
		updateLaserBuffer();

        // handle debug hotkeys which don't only work when in editor mode
        if (time_until_allow_meta_input == 0 && editor_state.editor_mode != SELECT_WRITE)
        {
            // change camera fov for editor
            if (tick_input.n_press && editor_state.editor_mode != NO_MODE)
            {
                camera.fov--;
                time_until_allow_meta_input = 4;
            }
            else if (tick_input.b_press && editor_state.editor_mode != NO_MODE)
            {
                camera.fov++;
                time_until_allow_meta_input = 4;
            }

            // snap camera yaw to nearest axis
            if (tick_input.p_press)
            {
                float camera_snap_yaw = 0;
                if 		(camera.yaw >= TAU * -0.375f && camera.yaw < TAU * -0.125f) camera_snap_yaw = TAU * -0.25f;
                else if (camera.yaw >= TAU * -0.125f && camera.yaw < TAU *  0.125f) camera_snap_yaw = 0;
                else if (camera.yaw >= TAU *  0.125f && camera.yaw < TAU *  0.375f) camera_snap_yaw = TAU * 0.25f;
                else if (camera.yaw >= TAU *  0.375f || camera.yaw < TAU * -0.375f) camera_snap_yaw = TAU * 0.5f;
                camera.yaw = camera_snap_yaw;
                camera.rotation = buildCameraQuaternion(camera);
                char yaw_text[256] = {0};
                snprintf(yaw_text, sizeof(yaw_text), "camera yaw snapped to: %.3f", camera_snap_yaw);
                createDebugPopup(yaw_text, NO_TYPE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // set camera fov to wide for editor
            if (tick_input.j_press)
            {
                editor_state.do_wide_camera = !editor_state.do_wide_camera;
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                if (editor_state.do_wide_camera)
                {
                    if (editor_state.editor_mode == NO_MODE)
                    {
                        editor_state.do_wide_camera = false;
                        if (camera_mode == MAIN_WAITING) camera.fov = saved_main_camera.fov;
                        else if (camera_mode == ALT_WAITING) camera.fov = saved_alt_camera.fov;
                        else camera.fov = 15.0f;
                    }
                    else
                    {
                        camera.fov = 60.0f;
                    }
                }
                else
                {
                    if (saved_main_camera.fov == camera.fov) camera.fov = 15.0f; // if working on a new level, and have saved camera as 60fov, then default to 15
                    else camera.fov = saved_main_camera.fov;
                }
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // change model states
            if (tick_input.nine_press)
            {
               	game_shader_mode = OLD; 
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                createDebugPopup("shader mode: old", SHADER_MODE_CHANGE);
            }
            if (tick_input.eight_press)
            {
                game_shader_mode = OUTLINE;
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                createDebugPopup("shader mode: outlines", SHADER_MODE_CHANGE);
            }
            if (tick_input.seven_press)
            {
                game_shader_mode = OUTLINE_TEST;
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                createDebugPopup("shader mode: testing outlines", SHADER_MODE_CHANGE);
            }

            // get rid of debug text on press
            if (tick_input.y_press)
            {
                do_debug_text = !do_debug_text;
                if (do_debug_text) createDebugPopup("debug state visibility on", DEBUG_STATE_VISIBILITY_CHANGE);
                else			   createDebugPopup("debug state visibility off", DEBUG_STATE_VISIBILITY_CHANGE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // toggle cheating
            if (tick_input.three_press)
            {
                cheating = !cheating;
                if (cheating) createDebugPopup("cheating", CHEAT_MODE_TOGGLE);
                else createDebugPopup("not cheating", CHEAT_MODE_TOGGLE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
        }

        // alternative camera: switch modes on tab. defined as meta input, so can move player at same time as tab camera change.
        if (tick_input.tab_press && time_until_allow_meta_input == 0 && editor_state.editor_mode == NO_MODE) 
        {
            if (saved_alt_camera.fov != 0)
            {
                if (camera_mode == MAIN_WAITING || camera_mode == ALT_TO_MAIN) camera_mode = MAIN_TO_ALT;
                else camera_mode = ALT_TO_MAIN;
            }
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }

        // perform alt <-> main camera interpolation
        if (camera_mode == MAIN_TO_ALT)
        {
            camera_lerp_t += CAMERA_T_TIMESTEP;
            if (camera_lerp_t >= 1) 
            {
                camera_lerp_t = 1.0f;
                camera = saved_alt_camera;
                camera.rotation = buildCameraQuaternion(camera);
                camera_mode = ALT_WAITING;
            }
        }
        if (camera_mode == ALT_TO_MAIN)
        {
            camera_lerp_t -= CAMERA_T_TIMESTEP;
            if (camera_lerp_t <= 0)
            {
                camera_lerp_t = 0.0f;
                camera = saved_main_camera;
                camera.rotation = buildCameraQuaternion(camera);
                camera_mode = MAIN_WAITING;
            }
        }
        if (camera_lerp_t != 0 && camera_lerp_t != 1)
        {
            camera = lerpCamera(saved_main_camera, saved_alt_camera, camera_lerp_t, (float)camera_target_plane);
        }

        camera_with_ow_offset = camera;
        
		// adjust overworld camera based on position
		if (in_overworld)
        {
            Int3 player_delta = int3Subtract(player->coords, OVERWORLD_CAMERA_CENTER_START);
            int32 screen_offset_x = 0;
        	int32 screen_offset_z = 0;
			if (player_delta.x > 0) screen_offset_x = (player_delta.x + (OVERWORLD_SCREEN_SIZE_X / 2)) / OVERWORLD_SCREEN_SIZE_X;
			else					screen_offset_x = (player_delta.x - (OVERWORLD_SCREEN_SIZE_X / 2)) / OVERWORLD_SCREEN_SIZE_X; 
			if (player_delta.z > 0) screen_offset_z = (player_delta.z + (OVERWORLD_SCREEN_SIZE_Z / 2)) / OVERWORLD_SCREEN_SIZE_Z;
			else 					screen_offset_z = (player_delta.z - (OVERWORLD_SCREEN_SIZE_Z / 2)) / OVERWORLD_SCREEN_SIZE_Z;

            camera_with_ow_offset.coords.x = camera.coords.x + (screen_offset_x * OVERWORLD_SCREEN_SIZE_X);
            camera_with_ow_offset.coords.z = camera.coords.z + (screen_offset_z * OVERWORLD_SCREEN_SIZE_Z);

            // camera delta info
            char delta_text[256];
            snprintf(delta_text, sizeof(delta_text), "player delta from origin: %.1d, %.1d --- %.1d, %.1d", player_delta.x, player_delta.z, screen_offset_x, screen_offset_z);
            if (do_debug_text) createDebugText(delta_text);
        }

        // SAVING STUFF
        {
            // paths for saving data both to source and to inside build
            char level_path[64];
            buildLevelPathFromName(world_state.level_name, &level_path, true);
            char relative_level_path[64];
            buildLevelPathFromName(world_state.level_name, &relative_level_path, false);

            // only used if saving in overworld
            char overworld_zero_path[64];
            buildLevelPathFromName(overworld_zero_name, &overworld_zero_path, true);
            char overworld_zero_relative_path[64];
            buildLevelPathFromName(overworld_zero_name, &overworld_zero_relative_path, false);

            // write camera to file on c press, alternative camera on v press
            if (time_until_allow_meta_input == 0 && (editor_state.editor_mode == PLACE_BREAK || editor_state.editor_mode == SELECT) && (tick_input.c_press || tick_input.v_press))
            {
                char tag[4] = {0};
                bool write_alt_camera = false;
                if (tick_input.c_press) 
                {
                    memcpy(&tag, &MAIN_CAMERA_CHUNK_TAG, sizeof(tag));
                    createDebugPopup("main camera saved", MAIN_CAMERA_SAVE);
                }
                else 					
                {
                    memcpy(&tag, &ALT_CAMERA_CHUNK_TAG, sizeof(tag));
                    createDebugPopup("alt camera saved", ALT_CAMERA_SAVE);
                    write_alt_camera = true;
                }

                {
                    FILE* file = fopen(level_path, "rb+");
                    int32 positions[64] = {0};
                    int32 count = getCountAndPositionOfChunk(file, tag, positions);

                    if (count > 0)
                    {
                        fseek(file, positions[0], SEEK_SET);
                        writeCameraToFile(file, &camera, write_alt_camera);
                    }
                    else
                    {
                        fseek(file, 0, SEEK_END);
                        writeCameraToFile(file, &camera, write_alt_camera);
                    }
                    fclose(file);
                }
                {
                    FILE* file = fopen(relative_level_path, "rb+");
                    int32 positions[64] = {0};
                    int32 count = getCountAndPositionOfChunk(file, tag, positions);

                    if (count > 0)
                    {
                        fseek(file, positions[0], SEEK_SET);
                        writeCameraToFile(file, &camera, write_alt_camera);
                    }
                    else
                    {
                        fseek(file, 0, SEEK_END);
                        writeCameraToFile(file, &camera, write_alt_camera);
                    }
                    fclose(file);
                }

                if (tick_input.c_press) saved_main_camera = camera;
                else saved_alt_camera = camera;
            }

            if (time_until_allow_meta_input == 0 && editor_state.editor_mode != SELECT_WRITE && tick_input.x_press) 
            {
                memset(&saved_alt_camera, 0, sizeof(Camera));
                camera = saved_main_camera;
                camera.rotation = buildCameraQuaternion(camera);

                Camera empty_camera = {0};
                {
                    FILE* file = fopen(relative_level_path, "rb+");
                    int32 positions[64] = {0};
                    getCountAndPositionOfChunk(file, ALT_CAMERA_CHUNK_TAG, positions);
                    fseek(file, positions[0], SEEK_SET);
                    writeCameraToFile(file, &empty_camera, true);
                    fclose(file);
                }
                {
                    FILE* file = fopen(level_path, "rb+");
                    int32 positions[64] = {0};
                    getCountAndPositionOfChunk(file, ALT_CAMERA_CHUNK_TAG, positions);
                    fseek(file, positions[0], SEEK_SET);
                    writeCameraToFile(file, &empty_camera, true);
                    fclose(file);
                }
            }

            // write level to file on i press
            if (time_until_allow_meta_input == 0 && (editor_state.editor_mode == PLACE_BREAK || editor_state.editor_mode == SELECT) && tick_input.i_press) 
            {
                saveLevelRewrite(level_path, true);
                saveLevelRewrite(relative_level_path, true);
                if (in_overworld)
                {
                    saveLevelRewrite(overworld_zero_path, true);
                    saveLevelRewrite(overworld_zero_relative_path, true);
                }
                createDebugPopup("level saved", LEVEL_SAVE);
                writeSolvedLevelsToFile();
            }
        }

        // create debug texts
        if (do_debug_text)
        {
            // display level name
            createDebugText(next_world_state.level_name);

            // game progress info
            char game_text[256] = {0};
            snprintf(game_text, sizeof(game_text), "game progress: %d", game_progress);
            createDebugText(game_text);

            // entity info
            char player_text[256] = {0};
            snprintf(player_text, sizeof(player_text), "player info: coords: %d, %d, %d, moving time: %d, moving direction: %d", player->coords.x, player->coords.y, player->coords.z, player->in_motion, player->moving_direction);
            createDebugText(player_text);

            char pack_text[256] = {0};
            snprintf(pack_text, sizeof(pack_text), "pack info: coords: %d, %d, %d, moving_time: %d, moving_direction: %d", pack->coords.x, pack->coords.y, pack->coords.z, pack->in_motion, pack->moving_direction);
            createDebugText(pack_text);

            // camera pos info
            char camera_text[256] = {0};
            snprintf(camera_text, sizeof(camera_text), "current camera info:    %.1f, %.1f, %.1f, fov: %.1f", camera.coords.x, camera.coords.y, camera.coords.z, camera.fov);
            createDebugText(camera_text);

            // saved camera info
            char saved_camera_text[256] = {0};
            snprintf(saved_camera_text, sizeof(saved_camera_text), "main saved camera info: %.1f, %.1f, %.1f, fov: %.1f", saved_main_camera.coords.x, saved_main_camera.coords.y, saved_main_camera.coords.z, saved_main_camera.fov);
            createDebugText(saved_camera_text);

            // saved alt camera info
            char alt_camera_text[256] = {0};
            snprintf(alt_camera_text, sizeof(alt_camera_text), "alt saved camera info:  %.1f, %.1f, %.1f, fov: %.1f", saved_alt_camera.coords.x, saved_alt_camera.coords.y, saved_alt_camera.coords.z, saved_alt_camera.fov);
            createDebugText(alt_camera_text);

            // camera_t info
            char t_text[256] = {0};
            snprintf(t_text, sizeof(t_text), "t value: %.2f", camera_lerp_t);
            createDebugText(t_text);

            /*
            // show undos performed
            char undo_text[256] = {0};
            snprintf(undo_text, sizeof(undo_text), "undos performed: %d", undos_performed);
            createDebugText(undo_text);
            */

            // show current selected id + coords
            char edit_text[256] = {0};
            snprintf(edit_text, sizeof(edit_text), "selected id: %d; coords: %d, %d, %d", editor_state.selected_id, editor_state.selected_coords.x, editor_state.selected_coords.y, editor_state.selected_coords.z);
            createDebugText(edit_text);

			/*
            // show undo deltas in buffer
            char undo_buffer_text[256] = {0};
            snprintf(undo_buffer_text, sizeof(undo_buffer_text), "undo deltas in buffer: %d", undo_buffer.delta_count);
            createDebugText(undo_buffer_text);
            */

            // draw selected id info
            if (editor_state.editor_mode == SELECT || editor_state.editor_mode == SELECT_WRITE)
            {
                if (editor_state.selected_id > 0)
                {
                    Entity* e = getEntityFromId(editor_state.selected_id);
                    if (e) // TODO: this guard is somewhat bad solution; i still persist selected_id even if that id doesn't exist anymore. this prevents crash, but later: on entity delete check if matches against id, and if so remove from editor_state.
                    {
                        char selected_id_text[256] = {0};
                        snprintf(selected_id_text, sizeof(selected_id_text), "selected id: %d", editor_state.selected_id);

                        char writing_field_text[256] = {0};
                        char writing_field_state[256] = {0};
                        switch (editor_state.writing_field)
                        {
                            case NO_WRITING_FIELD:    		memcpy(writing_field_state, "none", 		sizeof(writing_field_state)); break;
                            case WRITING_FIELD_NEXT_LEVEL:  memcpy(writing_field_state, "next level", 	sizeof(writing_field_state)); break;
                            case WRITING_FIELD_UNLOCKED_BY: memcpy(writing_field_state, "unlocked by", 	sizeof(writing_field_state)); break;
                        }
                        snprintf(writing_field_text, sizeof(writing_field_text), "writing_field: %s", writing_field_state); 
                        createDebugText(writing_field_text);

                        char next_level_text[256] = {0};
                        snprintf(next_level_text, sizeof(next_level_text), "next_level: %s", e->next_level);
                        createDebugText(next_level_text);

                        char unlocked_by_text[256] = {0};
                        snprintf(unlocked_by_text, sizeof(unlocked_by_text), "unlocked_by: %s", e->unlocked_by);
                        createDebugText(unlocked_by_text);

                        if (getTileType(e->coords) == RESET_BLOCK)
                        {
                            FOR(reset_index, MAX_RESET_COUNT)
                            {
                                if (e->reset_info[reset_index].id == -1) continue;
                                char reset_id_text[256] = {0};
                                snprintf(reset_id_text, sizeof(reset_id_text), "id of nr. %d reset: %d", reset_index, e->reset_info[reset_index].id);
                                createDebugText(reset_id_text);
                            }
                        }
                    }
                    else
                    {
                        createDebugText("selected entity deleted");
                    }
                }
                else
                {
                    createDebugText("no entity selected");
                }
            }
        }

        // finished updating state
        world_state = next_world_state;

        physics_accumulator -= physics_timestep;

        if (time_until_allow_game_input > 0) time_until_allow_game_input--;
	}

    // now out of the fixed physics-time loop; stuff out here happens every draw call, not just every physics frame. e.g., camera movement is smooth at whatever framerate game is runinng at.
    // draw calls are also out here, but position_norm and rotation_quat for entites is only updated every physics frame anyway, so even if they're drawn 2-3 times per physics frame 
    // on a 144hz monitor, they're still at the same positions. next step here would be to add interpolations between these states, but i'm waiting until i've got a more sophisticated animation
    // system before doing that. 
    // 
    // also, i don't want to sacrifice the already maybe-very-bad game feel here, and interpolating between states seems to mean that i have to run 1 physics frame, or 16ms, behind, 
    // to have something to interpolate against. there's probably some cheating you can do by predicting the next position and interpolating against that when in the middle of an animation, 
    // but that doesn't solve the problem of an action happening after some delay when pressing a button, which i don't want. 
    // so i also want to set up buffered inputs before setting this up, so i can see if this is actually a problem, and if there's any solution within my current system, or if i can find some 
    // other solution, e.g. just running physics at 120hz.

    // update camera for drawing (every display frame)
    camera_with_ow_offset = camera;
    if (in_overworld)
    {
        Int3 player_delta = int3Subtract(world_state.player.coords, OVERWORLD_CAMERA_CENTER_START);
        int32 screen_offset_x = 0;
        int32 screen_offset_z = 0;
        if (player_delta.x > 0) screen_offset_x = (player_delta.x + (OVERWORLD_SCREEN_SIZE_X / 2)) / OVERWORLD_SCREEN_SIZE_X;
        else                    screen_offset_x = (player_delta.x - (OVERWORLD_SCREEN_SIZE_X / 2)) / OVERWORLD_SCREEN_SIZE_X;
        if (player_delta.z > 0) screen_offset_z = (player_delta.z + (OVERWORLD_SCREEN_SIZE_Z / 2)) / OVERWORLD_SCREEN_SIZE_Z;
        else                    screen_offset_z = (player_delta.z - (OVERWORLD_SCREEN_SIZE_Z / 2)) / OVERWORLD_SCREEN_SIZE_Z;

        camera_with_ow_offset.coords.x = camera.coords.x + (screen_offset_x * OVERWORLD_SCREEN_SIZE_X);
        camera_with_ow_offset.coords.z = camera.coords.z + (screen_offset_z * OVERWORLD_SCREEN_SIZE_Z);
    }

    // DRAW 3D
    {
        updateLaserBuffer();

        // draw lasers
        FOR(laser_buffer_index, 64)
        {
            LaserBuffer lb = laser_buffer[laser_buffer_index];
            if (lb.color == NO_COLOR) continue;

            Vec3 diff = vec3Subtract(lb.end_coords, lb.start_coords);
            Vec3 center = vec3Add(lb.start_coords, vec3ScalarMultiply(diff, 0.5));

            float length = vec3Length(diff);
            Vec3 scale = { LASER_WIDTH, LASER_WIDTH, length };
            Vec4 rotation = {0};
			if (lb.direction == UP || lb.direction == DOWN) rotation = directionToQuaternion(lb.direction, false);
			else rotation = directionToQuaternion(lb.direction, true);
            
            drawAsset(CUBE_3D_LASER_GREEN, LASER, center, scale, rotation, colorToRGB(lb.color));
        }

        /*
        // debug: write laser start and end coords
        FOR(lb_index, 64)
        {
            LaserBuffer lb = laser_buffer[lb_index];
            if (vec3IsEqual(lb.start_coords, VEC3_0)) continue;
            char lb_text[256] = {0};
            snprintf(lb_text, sizeof(lb_text), "lb start coords: %.2f, %.2f, %.2f, lb end coords: %.2f, %.2f, %.2f", lb.start_coords.x, lb.start_coords.y, lb.start_coords.z, lb.end_coords.x, lb.end_coords.y, lb.end_coords.z);
            if (do_debug_text) createDebugText(lb_text);
        }
        */

        // clear laser buffer 
        memset(laser_buffer, 0, sizeof(laser_buffer));

        // draw most things (not player or pack) TODO: after models can include pack here because can be DEFAULT_SCALE. after actual shaders for the color of the player can also include player here
        for (int tile_index = 0; tile_index < 2 * level_dim.x*level_dim.y*level_dim.z; tile_index += 2)
        {
            TileType draw_tile = world_state.buffer[tile_index];
            if (draw_tile == NONE || draw_tile == PLAYER || draw_tile == PACK) continue;
            if (isEntity(draw_tile))
            {
                Entity* e = getEntityAtCoords(bufferIndexToCoords(tile_index));

                if (e->locked) draw_tile = LOCKED_BLOCK;
                if (draw_tile == WIN_BLOCK)
                {
                    if (in_overworld && findInSolvedLevels(e->next_level) != -1) draw_tile = WON_BLOCK;
                    else if (!in_overworld && findInSolvedLevels(next_world_state.level_name) != -1) draw_tile = WON_BLOCK;
                }

                if (game_shader_mode == OLD)
                {
                    drawAsset(getCube3DId(draw_tile), CUBE_3D, e->position_norm, DEFAULT_SCALE, e->rotation_quat, VEC3_0); 
                }
                else
                {
                    drawAsset(getModelId(draw_tile), MODEL_3D, e->position_norm, DEFAULT_SCALE, e->rotation_quat, VEC3_0);
                }
            }
            else
            {
                if (game_shader_mode != OLD && getCube3DId(draw_tile) == CUBE_3D_WATER) 
                {
                    drawAsset(MODEL_3D_WATER, WATER_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToQuaternion(next_world_state.buffer[tile_index + 1], false), VEC3_0);
                }
                else 
                {
                    drawAsset(getCube3DId(draw_tile), CUBE_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToQuaternion(next_world_state.buffer[tile_index + 1], false), VEC3_0);
                }
            }
        }

        if (!world_state.player.removed)
        {
            player = &world_state.player;

            // TODO: this is terrible (fix with shaders)
            bool hit_by_green = false;
            if (player->green_hit.north || player->green_hit.west || player->green_hit.south || player->green_hit.east || player->green_hit.up || player->green_hit.down) hit_by_green = true;
            if      (player->hit_by_red && hit_by_green && player->hit_by_blue) drawAsset(CUBE_3D_PLAYER_WHITE,   CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat, VEC3_0);
            else if (player->hit_by_red && hit_by_green             		  ) drawAsset(CUBE_3D_PLAYER_YELLOW,  CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat, VEC3_0);
            else if (player->hit_by_red &&      	       player->hit_by_blue) drawAsset(CUBE_3D_PLAYER_MAGENTA, CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat, VEC3_0);
            else if (             		   hit_by_green && player->hit_by_blue) drawAsset(CUBE_3D_PLAYER_CYAN,    CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat, VEC3_0);
            else if (player->hit_by_red                 	  				  ) drawAsset(CUBE_3D_PLAYER_RED,     CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat, VEC3_0);
            else if (             		   hit_by_green             		  ) drawAsset(CUBE_3D_PLAYER_GREEN,   CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat, VEC3_0);
            else if (                            		   player->hit_by_blue) drawAsset(CUBE_3D_PLAYER_BLUE,    CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat, VEC3_0);
            else drawAsset(CUBE_3D_PLAYER, CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat, VEC3_0);

            if (do_player_ghost) drawAsset(CUBE_3D_PLAYER_GHOST, CUBE_3D, intCoordsToNorm(player_ghost_coords), PLAYER_SCALE, directionToQuaternion(player_ghost_direction, true), VEC3_0);
            if (do_pack_ghost)   drawAsset(CUBE_3D_PACK_GHOST,   CUBE_3D, intCoordsToNorm(pack_ghost_coords),   PLAYER_SCALE, directionToQuaternion(pack_ghost_direction, true),   VEC3_0);
        }
        if (!world_state.pack.removed) drawAsset(CUBE_3D_PACK, CUBE_3D, world_state.pack.position_norm, PLAYER_SCALE, world_state.pack.rotation_quat, VEC3_0);

        // draw camera boundary lines
		if (time_until_allow_meta_input == 0 && tick_input.t_press && !(editor_state.editor_mode == SELECT_WRITE))
        {
            draw_level_boundary = !draw_level_boundary;
            if (draw_level_boundary) createDebugPopup("level / camera boundary visibility on", LEVEL_BOUNDARY_VISIBILITY_CHANGE);
            else			   		 createDebugPopup("level / camera boundary visibility off", LEVEL_BOUNDARY_VISIBILITY_CHANGE);
			time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }
        if (draw_level_boundary)
        {
			if (in_overworld)
            {
            	// draw camera screen lines
                int32 x_draw_offset = 0;
                int32 z_draw_offset = 0;

                Vec3 x_wall_scale = { (float)OVERWORLD_SCREEN_SIZE_X, 5, 0.01f };
                Vec3 z_wall_scale = { 0.01f, 5, (float)OVERWORLD_SCREEN_SIZE_Z };

                FOR(z_index, 18)
                {
                    FOR(x_index, 12)
                    {
                        Vec3 x_draw_coords = (Vec3)
                        { 
                            (float)(x_draw_offset + OVERWORLD_CAMERA_CENTER_START.x), 3,
                            (float)(z_draw_offset + OVERWORLD_CAMERA_CENTER_START.z) + ((float)OVERWORLD_SCREEN_SIZE_Z / 2)
                        }; 
                        Vec3 z_draw_coords = (Vec3)
                        { 
                            (float)(x_draw_offset + OVERWORLD_CAMERA_CENTER_START.x) - ((float)OVERWORLD_SCREEN_SIZE_X / 2), 3,
                            (float)(z_draw_offset + OVERWORLD_CAMERA_CENTER_START.z)
                        }; 

                        Vec3 outline_offset = (Vec3){ (float)(-2 * OVERWORLD_SCREEN_SIZE_X), 0, (float)(-14 * OVERWORLD_SCREEN_SIZE_Z) };
                        x_draw_coords = vec3Add(x_draw_coords, outline_offset);
                        z_draw_coords = vec3Add(z_draw_coords, outline_offset);

                        drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, x_draw_coords, x_wall_scale, IDENTITY_QUATERNION, VEC3_0);
                    	drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, z_draw_coords, z_wall_scale, IDENTITY_QUATERNION, VEC3_0);
                        x_draw_offset += OVERWORLD_SCREEN_SIZE_X;
                    }
                    x_draw_offset = 0;
                    z_draw_offset += OVERWORLD_SCREEN_SIZE_Z;
                }
            }
			else
            {
            	// draw level boundary
                Vec3 x_draw_coords_near = (Vec3){ -0.5f, 				     (float)level_dim.y / 2.0f, ((float)level_dim.z / 2.0f) };
                Vec3 z_draw_coords_near = (Vec3){ (float)level_dim.x / 2.0f, (float)level_dim.y / 2.0f, -0.5f};
                Vec3 x_draw_coords_far  = (Vec3){ (float)level_dim.x + 0.5f, (float)level_dim.y / 2.0f, (float)level_dim.z / 2.0f };
                Vec3 z_draw_coords_far  = (Vec3){ (float)level_dim.x / 2.0f, (float)level_dim.y / 2.0f, (float)level_dim.z + 0.5f };
                Vec3 x_draw_scale = (Vec3){ 0, 						   (float)level_dim.y, (float)level_dim.z + 1.0f };
                Vec3 z_draw_scale = (Vec3){ (float)level_dim.x + 1.0f, (float)level_dim.y, 0 };
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, x_draw_coords_near, x_draw_scale, IDENTITY_QUATERNION, VEC3_0);
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, z_draw_coords_near, z_draw_scale, IDENTITY_QUATERNION, VEC3_0);
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, x_draw_coords_far,  x_draw_scale, IDENTITY_QUATERNION, VEC3_0);
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, z_draw_coords_far,  z_draw_scale, IDENTITY_QUATERNION, VEC3_0);
            }
        }

        /*
        // temp draw outline around trailing hitboxes
        FOR(th_index, MAX_TRAILING_HITBOX_COUNT)
        {
            TrailingHitbox th = trailing_hitboxes[th_index];
            if (th.frames == 0 || th.hit_direction != NO_DIRECTION) continue;
            drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, intCoordsToNorm(th.coords), DEFAULT_SCALE, IDENTITY_QUATERNION, VEC3_0);
        }
        */
    }

    // DRAW 2D
    {
        Vec3 color_2d = { 1, 0, 0 }; // using alpha as first channel. 2d assets just use sprite atlas, so not using color.
        
		if (editor_state.editor_mode != NO_MODE)
        {
            // crosshair
            Vec3 crosshair_scale = { 35.0f, 35.0f, 0.0f };
            Vec3 center_screen = { ((float)game_display.client_width / 2), ((float)game_display.client_height / 2), 0.0f }; // weird numbers are just adjustment because raycast starts slightly offset 
                                                                                                        		 // i think this is due to windowed mode, but could be issue with raycast.
        	drawAsset(SPRITE_2D_CROSSHAIR, SPRITE_2D, center_screen, crosshair_scale, IDENTITY_QUATERNION, color_2d);

            // picked block
            Vec3 picked_block_scale = { 200.0f, 200.0f, 0.0f };
            Vec3 picked_block_coords = { game_display.client_width - (picked_block_scale.x / 2) - 20, (picked_block_scale.y / 2) + 50, 0.0f };
            drawAsset(getSprite2DId(editor_state.picked_tile), SPRITE_2D, picked_block_coords, picked_block_scale, IDENTITY_QUATERNION, color_2d);

            if (editor_state.selected_id >= 0 && (editor_state.editor_mode == SELECT || editor_state.editor_mode == SELECT_WRITE))
            {
                SpriteId selected_id = getCube3DId(MIRROR);
                if (game_shader_mode != OLD) selected_id = getModelId(getTileTypeFromId(editor_state.selected_id));
                Entity* selected_e = 0;
                if (editor_state.selected_id > 0) selected_e = getEntityFromId(editor_state.selected_id);
                if (selected_e) drawAsset(selected_id, OUTLINE_3D, selected_e->position_norm, DEFAULT_SCALE, selected_e->rotation_quat, VEC3_0);

                if ((editor_state.selected_id / ID_OFFSET_RESET_BLOCK) * ID_OFFSET_RESET_BLOCK == ID_OFFSET_RESET_BLOCK)
                {
                    Entity* rb = getEntityFromId(editor_state.selected_id);
                    FOR(to_reset_index, MAX_RESET_COUNT)
                	{
                        ResetInfo ri = rb->reset_info[to_reset_index];
                        if (ri.id == -1) continue;
                        Entity* to_reset_e = getEntityFromId(ri.id);
                        SpriteId to_reset_id = getCube3DId(MIRROR);
                        if (game_shader_mode != OLD) to_reset_id = getModelId(getTileTypeFromId(to_reset_e->id));
                        drawAsset(to_reset_id, OUTLINE_3D, to_reset_e->position_norm, DEFAULT_SCALE, to_reset_e->rotation_quat, VEC3_0);
                    }
                }
            }
        }

        // handle decrementing timers which should be consistent across physics timesteps
        timer_accumulator += delta_time;
        global_time += delta_time;
        while (timer_accumulator >= 1.0/60.0)
        {
            FOR(popup_index, MAX_DEBUG_POPUP_COUNT) if (debug_popups[popup_index].frames_left > 0) debug_popups[popup_index].frames_left--;
            if (time_until_allow_meta_input > 0) time_until_allow_meta_input--;

            timer_accumulator -= 1.0/60.0;
        }

        // draw debug texts
        if (do_debug_text)
        {
            Vec2 debug_text_coords = debug_text_start_coords;
            FOR(debug_text_index, debug_text_count)
            {
                if (debug_text_buffer[debug_text_index][0] == 0) break;
                drawText(debug_text_buffer[debug_text_index], debug_text_coords, DEFAULT_TEXT_SCALE, 1.0f);
                debug_text_coords.y -= DEBUG_TEXT_Y_DIFF;
            }
        }

        // draw input text at center of screen
        if (editor_state.editor_mode == SELECT || editor_state.editor_mode == SELECT_WRITE)
        {
            Vec2 center_screen = { (float)game_display.client_width / 2, (float)game_display.client_height / 2 };
            drawText(editor_state.edit_buffer.string, center_screen, DEFAULT_TEXT_SCALE, 1.0f);
        }

        // draw debug popups
        FOR(popup_index, MAX_DEBUG_POPUP_COUNT)
        {
            DebugPopup* popup = &debug_popups[popup_index];
            if (popup->frames_left > 0)
            {
                float alpha = 1.0f;
                if (popup->frames_left < 30) alpha = (float)popup->frames_left / 30.0f;
                drawText(popup->text, popup->coords, DEFAULT_TEXT_SCALE, alpha);
            }
        }
    }

    vulkanSubmitFrame(draw_commands, draw_command_count, (float)global_time, camera_with_ow_offset, game_shader_mode);
    vulkanDraw();
}
