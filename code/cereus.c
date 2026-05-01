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
    NORTH,
    WEST,
    SOUTH,
    EAST,
    UP,
    DOWN,
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
    WATER,
    WIN_BLOCK,

    SOURCE_RED,
    SOURCE_BLUE,
    SOURCE_MAGENTA,

    LOCKED_BLOCK,
    LADDER,
    WON_BLOCK,
}
TileType;

typedef enum
{
    NO_COLOR = 0,
    RED,
    BLUE,
    MAGENTA,
}
Color;

typedef enum
{
    MIRROR_SIDE,
    MIRROR_UP,
    MIRROR_DOWN,
}
MirrorOrientation;

// coords are integer coordinates of the entity, position is the floating point coords in world space.
// likewise direction is one of 6 orientations, rotation is the actual rotation passed to renderer.
typedef struct
{
    int32 id;
    Int3 coords;
    Vec3 position;
    Direction direction;
    Vec4 rotation;
    bool removed;

    // for mirrors
    MirrorOrientation mirror_orientation;

    // movement state
    Vec3 velocity;
    bool falling;
    Direction climbing_direction; // TODO: put in temp state, only applies to player

    // for sources/lasers
    Color color;

    // for win blocks
    char next_level[64]; // TODO: make level names an enum so don't need to carry around 64 * char * 2 per entity

    // for locked blocks (and other entities that are locked)
    bool locked;
    char unlocked_by[64]; 
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

    char level_name[64];

    char solved_levels[64][64];
}
WorldState;

typedef struct
{
    int32 id;
    Int3 coords;
    int32 frames;
    TileType type;
}
TrailingHitbox;

typedef struct
{   
    int32 id;
    Direction direction;
    bool on_head;
    Entity* root_entity;
    bool tied_to_pack_and_decoupled; // should interpolate towards end point, but still handle with rest of enities
}
TiedEntity;

// a bunch of state to do with handling what gets pushed when during when the player is turning
typedef struct
{
    int32 pack_intermediate_states_timer;

    Int3 pack_intermediate_coords;
    Direction initial_player_direction;
}
PackTurnState;

// assumes 0 width
typedef struct
{
    Vec3 start_coords;
    Vec3 end_coords;
    Direction direction;
    Color color;
    Vec4 start_clip_plane;
    Vec4 end_clip_plane;
}
LaserBuffer;

typedef enum
{
    WORLD_0,
    WORLD_1,
    WORLD_2,
}
GameProgress;

typedef struct TemporaryState
{
    TrailingHitbox trailing_hitboxes[32]; 

    GameProgress game_progress;
    Int3 restart_position;
    bool in_overworld;
    int32 allow_movement_timer; // if > 0, decrements every frame towards 0, and then able to move. if -1, movement is permanently stopped until some other action resets it.
    bool pack_attached;

    TiedEntity entities_tied_to_movement[32];
    int32 player_hit_by_red;
    int32 player_hit_by_blue;
    PackTurnState pack_turn_state;
}
TemporaryState;

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
    ALT_TO_MAIN,
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
    DRAW_TRAILING_HITBOX_TOGGLE,
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

// UNDO BUFFER STRUCTS

typedef struct
{
    int32 id;
    Int3 old_coords;
    Direction old_direction;
    MirrorOrientation old_mirror_orientation;
    bool was_removed;
}
UndoEntityDelta;

typedef struct
{
    uint8 entity_count;
    uint32 delta_start_pos;
    bool level_changed;
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
#define TAU 6.2831853071f

const Vec3 DEFAULT_SCALE = { 1.0f,  1.0f,  1.0f  };
const Vec3 PLAYER_SCALE  = { 0.75f, 0.75f, 0.75f };

const float LASER_WIDTH = 0.25;
const float MAX_RAYCAST_SEEK_LENGTH = 100.0f;

const float PLAYER_MAX_SPEED = 0.12f;
const int32 TURN_TIME = 10;
const float MIN_DOWN_VELOCITY = -0.12f;
const float MAX_ANGULAR_VELOCITY = (TAU * 0.25f) / 10.0f; // last number is number of frames for a full turn
const float PLAYER_ACCELERATION = 0.04f;
const float PLAYER_MAX_DECELERATION = 0.04f;
const float GRAVITY = -0.03f;

const int32 STANDARD_TIME_UNTIL_ALLOW_INPUT = 9;
const int32 PLACE_BREAK_TIME_UNTIL_ALLOW_INPUT = 5;
const int32 TRAILING_HITBOX_TIME = 7;
const int32 FALL_TRAILING_HITBOX_TIME = 10; // TODO: this number should maybe be derived based on individual circumstance. but maybe just using a max is fine too, since collisions check if a trailing hitbox is relevant before using
const int32 TIME_AFTER_UNDO_UNTIL_PHYSICS_START = 4;

const int32 MAX_ENTITY_INSTANCE_COUNT = 64;
const int32 MAX_ENTITY_PUSH_COUNT = 32;
const int32 MAX_ENTITIES_TIED_TO_MOVEMENT = 32;
const int32 MAX_SOURCE_COUNT = 32;
const int32 MAX_LASER_TRAVEL_DISTANCE = 256;
const int32 MAX_LASER_TURNS_ALLOWED = 16;
const int32 MAX_PUSHABLE_STACK_SIZE = 32;
const int32 MAX_TRAILING_HITBOX_COUNT = 32;
const int32 MAX_LEVEL_COUNT = 64;
const int32 MAX_DEBUG_POPUP_COUNT = 32;

const Int3 AXIS_X = { 1, 0, 0 };
const Int3 AXIS_Y = { 0, 1, 0 };
const Int3 AXIS_Z = { 0, 0, 1 };
const Int3 INT3_0 = { 0, 0, 0 };
const Vec3 VEC3_0 = { 0, 0, 0 };
const Vec4 VEC4_0 = { 0, 0, 0, 0 };
const Vec4 IDENTITY_QUATERNION  = { 0, 0, 0, 1 };

const int32 PLAYER_ID = 1;
const int32 PACK_ID   = 2;
const int32 OUTLINE_DRAW_ID = ASSET_COUNT;
const int32 ID_OFFSET_BOX          = 100 * 1;
const int32 ID_OFFSET_MIRROR       = 100 * 2;
const int32 ID_OFFSET_GLASS        = 100 * 3;
const int32 ID_OFFSET_SOURCE       = 100 * 4;
const int32 ID_OFFSET_WIN_BLOCK    = 100 * 12;
const int32 ID_OFFSET_LOCKED_BLOCK = 100 * 13;

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

const int32 OVERWORLD_SCREEN_SIZE_X = 21;
const int32 OVERWORLD_SCREEN_SIZE_Z = 15;

const double DEFAULT_PHYSICS_TIMESTEP = 1.0/60.0;
double physics_timestep_multiplier = 1.0;
double physics_accumulator = 0;
double timer_accumulator = 0;
double global_time = 0; // will not work as a 'time elapsed' counter in editor mode because it grows slower during time slowdown

const char debug_level_name[64] = "testing";
const char relative_start_level_path_buffer[64] = "data/levels/";
const char source_start_level_path_buffer[64] = "../cereus/data/levels/";
const char solved_level_path[64] = "data/meta/solved-levels.meta";
const char undo_meta_path[64] = "data/meta/undo-buffer.meta";
const char overworld_zero_name[64] = "overworld-zero";

// CAMERA
const float CAMERA_SENSITIVITY = 0.005f;
const float CAMERA_MOVE_STEP = 0.2f;
const float CAMERA_FOV = 15.0f;

DisplayInfo game_display = {0};
Input prev_input = {0}; // copied from previous frame input to generate keys_pressed

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
Int3 ow_player_coords_for_offset = {0};
bool silence_unlocks_due_to_restart_or_undo = false;

float camera_lerp_t = 0.0f;
const float CAMERA_T_TIMESTEP = 0.05f;
int32 camera_target_plane = 0; // y level of xz plane which calculates targeted point during camera interpolation function TODO: should probably be something defined by level

DrawCommand draw_commands[16768] = {0};
int32 draw_command_count = 0;

Int3 level_dim = {0};
WorldState world_state = {0};
TemporaryState temp_state = {0};
LaserBuffer laser_buffer[512] = {0}; // 512 = 64 max sources * 16 max laser turns

WorldState leap_of_faith_world_state_snapshot = {0}; // TODO: doesn't change the buffer, so could save 2MB memory by using some EntitySnapshot struct
TemporaryState leap_of_faith_temp_state_snapshot = {0};
WorldState overworld_zero_state = {0}; // TODO: probably don't have to carry this around, just read from zeroed overworld file when i need this (on restart in overworld)

int32 time_until_allow_meta_input = 0;
int32 time_until_allow_undo_or_restart_input = 0;

// handle undos
UndoBuffer undo_buffer = {0};
int32 undos_performed = 0;
int32 undo_press_timer = 0;
bool restart_last_turn = false;

// debug state
EditorState editor_state = {0};

ShaderMode game_shader_mode = OLD;
bool draw_trailing_hitboxes = false;
bool cheating = false;

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

// MATH HELPER FUNCTIONS

float floatMax(float a, float b)
{
    return a > b ? a : b;
}

float floatAbs(float f)
{
    return f > 0 ? f : -f;
}

/*
float floatFractionalPart(float f)
{
    return f - (int32)f;
}
*/

Vec3 intCoordsToNorm(Int3 int_coords)
{
    return (Vec3){ (float)int_coords.x, (float)int_coords.y, (float)int_coords.z };
}

Int3 roundNormCoordsToInt(Vec3 position)
{
    return (Int3){ (int32)roundf(position.x), (int32)roundf(position.y), (int32)roundf(position.z) };
}

bool intCoordsWithinLevelBounds(Int3 coords) 
{
    return (coords.x >= 0 && coords.y >= 0 && coords.z >= 0 && coords.x < level_dim.x && coords.y < level_dim.y && coords.z < level_dim.z); 
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

Int3 int3ScalarMultiply(Int3 position, int32 scalar) 
{
    return (Int3){ position.x*scalar, position.y*scalar, position.z*scalar }; 
}

Vec3 vec3ScalarMultiply(Vec3 position, float scalar) 
{
    return (Vec3){ position.x*scalar, position.y*scalar, position.z*scalar }; 
}

Vec3 vec3Abs(Vec3 a) 
{
    return (Vec3){ floatAbs(a.x), floatAbs(a.y), floatAbs(a.z) }; 
}

float vec3Inner(Vec3 a, Vec3 b)
{
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

Vec3 vec3OuterProduct(Vec3 a, Vec3 b)
{
    return (Vec3){ a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
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

// QUATERNION MATH

Vec4 quaternionConjugate(Vec4 q)
{
    return (Vec4){ -q.x, -q.y, -q.z, q.w };
}

Vec4 quaternionNegate(Vec4 q)
{
    return (Vec4){ -q.x, -q.y, -q.z, -q.w };
}

// any 3D axis
Vec4 quaternionFromAxis(Vec3 axis, float angle)
{
    float sin = sinf(angle * 0.5f);
    float cos = cosf(angle * 0.5f);
    return (Vec4){ axis.x * sin, axis.y * sin, axis.z * sin, cos};
}

Vec4 quaternionMultiply(Vec4 q_a, Vec4 q_b)
{
    return (Vec4)
    { 
        (q_a.w * q_b.x) + (q_a.x * q_b.w) + (q_a.y * q_b.z) - (q_a.z * q_b.y),
        (q_a.w * q_b.y) - (q_a.x * q_b.z) + (q_a.y * q_b.w) + (q_a.z * q_b.x),
        (q_a.w * q_b.z) + (q_a.x * q_b.y) - (q_a.y * q_b.x) + (q_a.z * q_b.w),
        (q_a.w * q_b.w) - (q_a.x * q_b.x) - (q_a.y * q_b.y) - (q_a.z * q_b.z) 
    };
}

float quaternionInnerProduct(Vec4 q_a, Vec4 q_b)
{
    return (q_a.x * q_b.x) + (q_a.y * q_b.y) + (q_a.z * q_b.z) + (q_a.w * q_b.w);
}

Vec4 quaternionNormalize(Vec4 q)
{
    float length_squared = (q.x * q.x) + (q.y * q.y) + (q.z * q.z) + (q.w * q.w);
    if (length_squared <= 1e-8f) return IDENTITY_QUATERNION;
    float inverse_length = 1.0f / sqrtf(length_squared);
    return (Vec4){ q.x * inverse_length, q.y * inverse_length, q.z * inverse_length, q.w * inverse_length };
}

float getAngleOfYAxisRotation(Vec4 a, Vec4 b)
{
    Vec4 unwound_b = b;
    if (quaternionInnerProduct(a, b) < 0) unwound_b = quaternionNegate(b); // make sure quaternions on same hemisphere
    Vec4 from_a_to_b = quaternionMultiply(unwound_b, quaternionConjugate(a)); // transform * a = b, so transform = b * a^-1
    return 2.0f * atan2f(from_a_to_b.y, from_a_to_b.w); // can do this because rotation is fully around the y axis
}

// TODO: could use the optimised version when i understand quaternions better. for now, this is more transparent
Vec3 vec3RotateByQuaternion(Vec3 v, Vec4 q)
{
    Vec4 v_as_quaternion = (Vec4){ v.x, v.y, v.z, 0 };
    Vec4 q_conjugate = quaternionConjugate(q);
    Vec4 out_v = quaternionMultiply(quaternionMultiply(q, v_as_quaternion), q_conjugate);
    return (Vec3){ out_v.x, out_v.y, out_v.z };
}

// CAMERA STUFF 

Vec4 buildCameraQuaternion(Camera input_camera)
{
    Vec4 quaternion_yaw   = quaternionFromAxis(intCoordsToNorm(AXIS_Y), input_camera.yaw);
    Vec4 quaternion_pitch = quaternionFromAxis(intCoordsToNorm(AXIS_X), input_camera.pitch);
    return quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));
}

// assumes looking toward plane (otherwise negative t value)
Vec3 cameraLookingAtPointOnPlane(Camera input_camera, float plane_y)
{
    Vec3 neg_z_axis = intCoordsToNorm(int3Negate(AXIS_Z)); // standard camera axis before any rotation
    Vec3 forward = vec3RotateByQuaternion(neg_z_axis, buildCameraQuaternion(input_camera)); // get the cameras forward vector
    float t = (plane_y - input_camera.coords.y) / forward.y; // get t value for intersection
    return (Vec3)
    {
        input_camera.coords.x + forward.x * t,
        input_camera.coords.y + forward.y * t,
        input_camera.coords.z + forward.z * t
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

// sets basis for movement which changes with angle, i.e. w will always travel straight ahead, at the same y level
void cameraBasisFromYaw(float yaw, Vec3* right, Vec3* forward)
{
    float sine_yaw = sinf(yaw), cosine_yaw = cosf(yaw);
    *right   = (Vec3){ cosine_yaw, 0,   -sine_yaw };
    *forward = (Vec3){ -sine_yaw,  0, -cosine_yaw };
}

// BUFFER / STATE INTERFACING

int32 coordsToBufferIndexType(Int3 coords)
{
    return 2*(level_dim.x*level_dim.z*coords.y + level_dim.x*coords.z + coords.x); 
}
int32 coordsToBufferIndexDirection(Int3 coords)
{
    return coordsToBufferIndexType(coords) + 1;
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
    world_state.buffer[coordsToBufferIndexType(coords)] = type; 
}

void setTileDirection(Direction direction, Int3 coords, MirrorOrientation mirror_orientation)
{
    world_state.buffer[coordsToBufferIndexDirection(coords)] = (uint8)(direction + 4*mirror_orientation);
}

TileType getTileType(Int3 coords) 
{
    return world_state.buffer[coordsToBufferIndexType(coords)]; 
}

Direction getTileDirection(Int3 coords) 
{
    return world_state.buffer[coordsToBufferIndexDirection(coords)]; 
}

// sets coords and position of an entity to some values, and updates the buffer accordingly 
void moveEntityInBufferAndState(Entity* e, Int3 end_coords, Direction end_direction)
{
    TileType type = getTileType(e->coords); // could also get from id
    setTileType(NONE, e->coords);
    setTileDirection(NO_DIRECTION, e->coords, e->mirror_orientation);
    e->coords = end_coords;
    e->direction = end_direction;
    setTileType(type, end_coords);
    setTileDirection(end_direction, end_coords, e->mirror_orientation);
}

bool isSource(TileType type) 
{
    return (type == SOURCE_RED || type == SOURCE_BLUE || type == SOURCE_MAGENTA);
}

// only checks tile types - doesn't do what canPush does
bool isPushable(TileType type)
{
    return (type == BOX || type == MIRROR || type == PACK || isSource(type));
}

bool isEntity(TileType type)
{
    return (type == BOX || type == MIRROR || type == PACK || type == PLAYER || type == WIN_BLOCK || type == LOCKED_BLOCK || isSource(type));
}

bool canBeUnderwater(TileType type)
{
    return (type == PLAYER || type == PACK || type == BOX || type == MIRROR || isSource(type));
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
            case RED:     return SOURCE_RED;
            case BLUE:    return SOURCE_BLUE;
            case MAGENTA: return SOURCE_MAGENTA;
            default: return NONE;
        }
    }
    else if (check == ID_OFFSET_BOX)          return BOX;
    else if (check == ID_OFFSET_MIRROR)       return MIRROR;
    else if (check == ID_OFFSET_GLASS)        return GLASS;
    else if (check == ID_OFFSET_WIN_BLOCK)    return WIN_BLOCK;
    else if (check == ID_OFFSET_LOCKED_BLOCK) return LOCKED_BLOCK;
    else return NONE;
}

Color getEntityColor(Int3 coords)
{
    switch (getTileType(coords))
    {
        case SOURCE_RED:     return RED;
        case SOURCE_BLUE:    return BLUE;
        case SOURCE_MAGENTA: return MAGENTA;
        default: return NO_COLOR;
    }
}

Entity* getEntityAtCoords(Int3 coords)
{
    TileType tile = getTileType(coords);
    Entity *entity_group = 0;
    if (isSource(tile)) entity_group = world_state.sources;
    else switch(tile)
    {
        case BOX:          entity_group = world_state.boxes;          break;
        case MIRROR:       entity_group = world_state.mirrors;        break;
        case GLASS:        entity_group = world_state.glass_blocks;  break;
        case WIN_BLOCK:    entity_group = world_state.win_blocks;    break;
        case LOCKED_BLOCK: entity_group = world_state.locked_blocks; break;
        case PLAYER: return &world_state.player;
        case PACK:   return &world_state.pack;
        default: return 0;
    }
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (entity_group[entity_index].removed) continue;
        if (int3IsEqual(entity_group[entity_index].coords, coords)) return &entity_group[entity_index];
    }
    return 0;
}

Entity* getEntityFromId(int32 id)
{
    if (id < 0) return 0;
    if (id == PLAYER_ID) return &world_state.player;
    else if (id == PACK_ID) return &world_state.pack;
    else 
    {
        Entity* entity_group = 0;
        int32 switch_value =  ((id / 100) * 100);
        if      (switch_value == ID_OFFSET_BOX)          entity_group = world_state.boxes; 
        else if (switch_value == ID_OFFSET_MIRROR)       entity_group = world_state.mirrors;
        else if (switch_value == ID_OFFSET_GLASS)        entity_group = world_state.glass_blocks;
        else if (switch_value >= ID_OFFSET_SOURCE && switch_value < ID_OFFSET_WIN_BLOCK) entity_group = world_state.sources;
        else if (switch_value == ID_OFFSET_WIN_BLOCK)    entity_group = world_state.win_blocks;
        else if (switch_value == ID_OFFSET_LOCKED_BLOCK) entity_group = world_state.locked_blocks;

        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT) if (entity_group[entity_index].id == id) return &entity_group[entity_index];
        return 0;
    }
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
        case RED:     return RED * 100; 
        case BLUE:    return BLUE * 100;
        case MAGENTA: return MAGENTA * 100;
        default: return 0;
    }
}

int32 entityIdOffset(Entity *entity, Color color)
{
    if      (entity == world_state.boxes)         return ID_OFFSET_BOX;
    else if (entity == world_state.mirrors)       return ID_OFFSET_MIRROR;
    else if (entity == world_state.glass_blocks)  return ID_OFFSET_GLASS;
    else if (entity == world_state.win_blocks)    return ID_OFFSET_WIN_BLOCK;
    else if (entity == world_state.locked_blocks) return ID_OFFSET_LOCKED_BLOCK;
    else if (entity == world_state.sources)       return ID_OFFSET_SOURCE + sourceColorIdOffset(color);
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

// these rotations could be hardcoded
Vec4 directionToQuaternion(Direction direction) 
{
    switch (direction)
    {
        case NORTH: return IDENTITY_QUATERNION;
        case WEST:  return quaternionFromAxis(intCoordsToNorm(AXIS_Y),  0.25f * TAU);
        case SOUTH: return quaternionFromAxis(intCoordsToNorm(AXIS_Y),  0.50f * TAU);
        case EAST:  return quaternionFromAxis(intCoordsToNorm(AXIS_Y), -0.25f * TAU);
        case UP:    return quaternionFromAxis(intCoordsToNorm(AXIS_X),  0.25f * TAU);
        case DOWN:  return quaternionFromAxis(intCoordsToNorm(AXIS_X), -0.25f * TAU);
        default: return (Vec4){ 0, 0, 0, 1 };
    }
}

Vec4 mirrorRotation(Direction direction, MirrorOrientation orientation)
{
    Vec4 direction_as_quaternion = directionToQuaternion(direction);
    switch (orientation)
    {
        case MIRROR_SIDE: return direction_as_quaternion; 
        case MIRROR_UP:   return quaternionMultiply(direction_as_quaternion, quaternionFromAxis(intCoordsToNorm(AXIS_X), -0.25f * TAU));
        case MIRROR_DOWN: return quaternionMultiply(direction_as_quaternion, quaternionFromAxis(intCoordsToNorm(AXIS_X),  0.25f * TAU));
        default: return IDENTITY_QUATERNION;
    }
}

int32 setEntityInstanceInGroup(Entity* entity_group, Int3 coords, Direction direction, Color color) 
{
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (entity_group[entity_index].id != -1) continue;
        entity_group[entity_index].coords = coords;
        entity_group[entity_index].position= intCoordsToNorm(coords); 
        entity_group[entity_index].direction = direction;
        entity_group[entity_index].rotation= directionToQuaternion(direction);
        entity_group[entity_index].color = color;
        entity_group[entity_index].id = entity_index + entityIdOffset(entity_group, color);
        entity_group[entity_index].unlocked_by[0] = '\0';
        entity_group[entity_index].next_level[0] = '\0';
        return entity_group[entity_index].id;
    }
    return 0;
}

// updates position and rotation to be float/quaternion versions of integer coords/direction enum
void setEntityVecsFromInts(Entity* e)
{
    e->position= intCoordsToNorm(e->coords);
    e->rotation = directionToQuaternion(e->direction);
}

// FILE I/O

// .level file structure: 
//
// first byte is version. version 0 is a dense representation, like the buffer i have in memory. 
// version 1 encodes buffer index, tile type, direction for every object, in a sparse representation, and so is much smaller, because >99% of a level is air
// then 3 bytes: x,y,z of level dimensions
// next x*y*z * 2 bytes: actual level buffer. this is still dense. takes up 2MB memory for the largest level (overworld, which is 250*250*16 in dimension; 2 bytes, one for tile type, one for direction)

// then chunking starts: 4 bytes for tag, 4 bytes for size (not including tag or size), and then data
// camera:          tag: CMRA,  size: 24 (6 * 4b),          data: x, y, z, fov, yaw, pitch (as floats)
// win block:       tag: WINB,  size: 76 (3 * 4b + 64b),    data: x, y, z (as int32), char[64] path
// locked block:    tag: LOKB,  size: 76 (3 * 4b + 64b),    data: x, y, z (as int32), char[64] path

void buildLevelPathFromName(char level_name[64], char (*level_path)[64], bool overwrite_source)
{
    char prefix[64];
    if (overwrite_source) memcpy(prefix, source_start_level_path_buffer, sizeof(prefix));
    else                  memcpy(prefix, relative_start_level_path_buffer, sizeof(prefix));
    snprintf(*level_path, sizeof(*level_path), "%s%s.level", prefix, level_name);
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
        fread(&world_state.buffer, 1, level_dim.x*level_dim.y*level_dim.z * 2, file);
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

            world_state.buffer[buffer_index] = (uint8)type;
            world_state.buffer[buffer_index + 1] = (uint8)direction;
        }
    }
}

Camera loadCameraInfo(FILE* file, bool use_alt_camera)
{
    Camera out_camera = {0};

    int32 positions[64] = {0};
    char tag[4] = {0}; 
    if (use_alt_camera) memcpy(&tag, &ALT_CAMERA_CHUNK_TAG, sizeof(tag));
    else            memcpy(&tag, &MAIN_CAMERA_CHUNK_TAG, sizeof(tag));

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
            Entity* wb = &world_state.win_blocks[wb_index];
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

        Entity* entity_group[6] = { world_state.boxes, world_state.mirrors, world_state.locked_blocks, world_state.glass_blocks, world_state.sources, world_state.win_blocks };
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

// keep level_dim in later versions also. can then just write buffer_index rather than the full coords and backsolve on load.
void writeBufferToFile(FILE* file, int32 version)
{
    if (version == 0)
    {
        fwrite(world_state.buffer, 1, level_dim.x*level_dim.y*level_dim.z * 2, file);
    }
    else if (version == 1)
    {
        int32 tile_count = 0;

        fseek(file, 8, SEEK_SET); // leave space for tile_count

        for (int32 buffer_index = 0; buffer_index < level_dim.x*level_dim.y*level_dim.z * 2; buffer_index += 2)
        {
            if (world_state.buffer[buffer_index] == NONE) continue;
            TileType type = (int8)world_state.buffer[buffer_index];
            Direction direction = (int8)world_state.buffer[buffer_index + 1];
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
    else                  memcpy(&tag, MAIN_CAMERA_CHUNK_TAG, sizeof(tag));

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

// doesn't change the camera
bool saveLevelRewrite(char* path)
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
        Entity* wb = &world_state.win_blocks[win_block_index];
        if (wb->removed) continue;
        if (wb->next_level[0] == '\0') continue;
        writeWinBlockToFile(file, wb);
    }

    Entity* entity_group[6] = { world_state.boxes, world_state.mirrors, world_state.locked_blocks, world_state.glass_blocks, world_state.sources, world_state.win_blocks };
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
    fclose(file);
    return true;
}

int32 findInSolvedLevels(char level[64])
{
    if (level[0] == '\0') return INT32_MAX; // if NULL string passed, return large number
    FOR(level_index, MAX_LEVEL_COUNT) if (strcmp(world_state.solved_levels[level_index], level) == 0) return level_index;
    return -1;
}

int32 nextFreeInSolvedLevels(char (*solved_levels)[64][64])
{
    FOR(solved_level_index, MAX_LEVEL_COUNT) if ((*solved_levels)[solved_level_index][0] == 0) return solved_level_index;
    return -1;
}

void addToSolvedLevels(char level[64])
{
    int32 next_free = nextFreeInSolvedLevels(&world_state.solved_levels);
    if (next_free == -1) return; // no free space (should not happen)
    strcpy(world_state.solved_levels[next_free], level);
}

void removeFromSolvedLevels(char level[64])
{
    int32 index = findInSolvedLevels(level);
    if (index == -1 || index > MAX_LEVEL_COUNT) return; // not in solved levels, or null string passed
    memset(world_state.solved_levels[index], 0, sizeof(world_state.solved_levels[0]));
}

void loadSolvedLevelsFromFile()
{
    memset(world_state.solved_levels, 0, sizeof(world_state.solved_levels));
    FILE* file = fopen(solved_level_path, "rb+");
    FOR(level_index, MAX_LEVEL_COUNT)
    {
        if (fread(world_state.solved_levels[level_index], 64, 1, file) != 1) break;
        if (world_state.solved_levels[level_index][0] == 0) break;
    }
    fclose(file);
}

void writeSolvedLevelsToFile()
{
    FILE* file = fopen(solved_level_path, "wb");
    if (!file) return;
    FOR(level_index, MAX_LEVEL_COUNT)
    {
        if (world_state.solved_levels[level_index][0] == 0) break;
        fwrite(&world_state.solved_levels[level_index], 64, 1, file);
    }
    fclose(file);
}

void clearSolvedLevels()
{
    FILE* file = fopen(solved_level_path, "wb");
    fclose(file);
    memset(world_state.solved_levels, 0, sizeof(world_state.solved_levels));
}

// DRAW ASSET

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
        case PACK:         return SPRITE_2D_PACK;
        case WATER:        return SPRITE_2D_WATER;
        case WIN_BLOCK:    return SPRITE_2D_WIN_BLOCK;
        case LOCKED_BLOCK: return SPRITE_2D_LOCKED_BLOCK;
        case LADDER:       return SPRITE_2D_LADDER;

        case SOURCE_RED:     return SPRITE_2D_SOURCE_RED;
        case SOURCE_BLUE:    return SPRITE_2D_SOURCE_BLUE;
        case SOURCE_MAGENTA: return SPRITE_2D_SOURCE_MAGENTA;
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
        case PACK:         return CUBE_3D_PACK;
        case WATER:        return CUBE_3D_WATER;
        case WIN_BLOCK:    return CUBE_3D_WIN_BLOCK;
        case LOCKED_BLOCK: return CUBE_3D_LOCKED_BLOCK;
        case LADDER:       return CUBE_3D_LADDER;
        case WON_BLOCK:    return CUBE_3D_WON_BLOCK;

        case SOURCE_RED:     return CUBE_3D_SOURCE_RED;
        case SOURCE_BLUE:    return CUBE_3D_SOURCE_BLUE;
        case SOURCE_MAGENTA: return CUBE_3D_SOURCE_MAGENTA;
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
        case PACK:         return MODEL_3D_PACK;
        case WATER:        return MODEL_3D_WATER;
        case WIN_BLOCK:    return MODEL_3D_WIN_BLOCK;
        case LOCKED_BLOCK: return MODEL_3D_LOCKED_BLOCK;
        case LADDER:       return MODEL_3D_LADDER;
        case WON_BLOCK:    return MODEL_3D_WON_BLOCK;

        case SOURCE_RED:     return MODEL_3D_SOURCE_RED;
        case SOURCE_BLUE:    return MODEL_3D_SOURCE_BLUE;
        case SOURCE_MAGENTA: return MODEL_3D_SOURCE_MAGENTA;
        default: return 0;
    }
}

void drawAsset(SpriteId id, AssetType type, Vec3 coords, Vec3 scale, Vec4 rotation, Vec4 color, bool do_aabb, Vec4 start_clip_plane, Vec4 end_clip_plane)
{
    if (id < 0) return;
    DrawCommand* command = &draw_commands[draw_command_count++];
    command->sprite_id = id;
    command->type = type;
    command->coords = coords;
    command->scale = scale;
    command->rotation = rotation;
    command->color = color;
    command->do_aabb = do_aabb;
    command->start_clip_plane = start_clip_plane;
    command->end_clip_plane = end_clip_plane;
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
        Vec4 color = { 0.0f, 0.0f, 0.0f, alpha };
        drawAsset(id, SPRITE_2D, draw_coords, draw_scale, IDENTITY_QUATERNION, color, false, VEC4_0, VEC4_0);
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

// RAYCAST ALGORITHM FOR EDITOR

RaycastHit raycastHitCube(Vec3 start, Vec3 direction, float max_distance)
{
    RaycastHit output = {0};
    Int3 current_cube = roundNormCoordsToInt(start);
    start.x += 0.5;
    start.y += 0.5;
    start.z += 0.5;

    // step direction on each axis
    int32 step_x = 0, step_y = 0, step_z = 0;

    if (direction.x > 0.0f)      step_x = 1;
    else if (direction.x < 0.0f) step_x = -1;
    if (direction.y > 0.0f)      step_y = 1;
    else if (direction.y < 0.0f) step_y = -1;
    if (direction.z > 0.0f)      step_z = 1;
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
    else             t_max_x = 1e12f;
    if (step_y != 0) t_max_y = (next_plane_y - start.y) / direction.y;
    else             t_max_y = 1e12f;
    if (step_z != 0) t_max_z = (next_plane_z - start.z) / direction.z;
    else             t_max_z = 1e12f;

    // distance t to traverse an entire plane
    float t_delta_x = 0, t_delta_y = 0, t_delta_z = 0;

    if (step_x != 0) t_delta_x = 1.0f / floatAbs(direction.x);
    else             t_delta_x = 1e12f;
    if (step_y != 0) t_delta_y = 1.0f / floatAbs(direction.y);
    else             t_delta_y = 1e12f;
    if (step_z != 0) t_delta_z = 1.0f / floatAbs(direction.z);
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
        if (world_state.buffer[buffer_index] != tile) continue;
        world_state.buffer[buffer_index] = NONE;
        world_state.buffer[buffer_index + 1] = NORTH;
    }
    entity->coords = coords;
    entity->position = intCoordsToNorm(coords);
    entity->id = id;
    entity->removed = false;
    setTileType(editor_state.picked_tile, coords);
    setTileDirection(NORTH, coords, 0);
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
        case UP:    return int3Add(coords, AXIS_Y);
        case DOWN:  return int3Add(coords, int3Negate(AXIS_Y));

        default: return (Int3){0};
    }
}

// the fact that y is checked last here matters sometimes, notably in the performUndo interpolations.
Direction getDirectionFromCoordDiff(Int3 to_coords, Int3 from_coords)
{
    Int3 diff = int3Subtract(from_coords, to_coords);
    if      (diff.x ==  1) return EAST;
    else if (diff.x == -1) return WEST;
    else if (diff.z ==  1) return SOUTH;
    else if (diff.z == -1) return NORTH;
    else if (diff.y ==  1) return UP;
    else if (diff.y == -1) return DOWN;
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

void createTrailingHitbox(int32 id, Int3 coords, int32 frames, TileType type) // TODO: derive type from id in here instead of passing
{
    int32 hitbox_index = -1;
    FOR(find_hitbox_index, MAX_TRAILING_HITBOX_COUNT)
    {
        if (temp_state.trailing_hitboxes[find_hitbox_index].frames != 0) continue;
        hitbox_index = find_hitbox_index;
        break;
    }
    if (hitbox_index == -1) return;
    temp_state.trailing_hitboxes[hitbox_index].id = id;
    temp_state.trailing_hitboxes[hitbox_index].coords = coords;
    temp_state.trailing_hitboxes[hitbox_index].frames = frames;
    temp_state.trailing_hitboxes[hitbox_index].type = type;
}

bool trailingHitboxAtCoords(Int3 coords, TrailingHitbox* trailing_hitbox)
{
    FOR(trailing_hitbox_index, MAX_TRAILING_HITBOX_COUNT) 
    {
        TrailingHitbox th = temp_state.trailing_hitboxes[trailing_hitbox_index];
        if (int3IsEqual(coords, th.coords) && th.frames > 0) 
        {
            *trailing_hitbox = th;
            return true;
        }
    }
    return false;
}

// VECTOR POSITION HELPERS

float getComponentAlongDirection(Direction direction, Vec3 vector)
{
    switch (direction)
    {
        case SOUTH: case NORTH: return vector.z;
        case WEST:  case EAST:  return vector.x;
        case UP:    case DOWN:  return vector.y;
        default: return 0;
    }
}

// will return negative of component along NORTH, WEST, and DOWN.
float getSignedComponentAlongDirection(Direction direction, Vec3 vector)
{
    switch (direction)
    {
        case SOUTH: return vector.z;
        case NORTH: return -vector.z;
        case EAST:  return vector.x;
        case WEST:  return -vector.x;
        case UP:    return vector.y;
        case DOWN:  return -vector.y;
        default: return 0;
    }
}

Vec3 vec3SetComponentAlongDirection(Direction direction, Vec3 vector, float f)
{
    switch (direction)
    {
        case NORTH:
        case SOUTH: return (Vec3){ vector.x, vector.y, f };
        case WEST:
        case EAST: return (Vec3){ f, vector.y, vector.z };
        case UP:
        case DOWN: return (Vec3){ vector.x, f, vector.z };
        default: return VEC3_0;
    }
}

Vec3 vec3AddFloatToVec3AlongDirection(Direction direction, float f, Vec3 v)
{
    switch (direction)
    {
        case NORTH:
        case SOUTH: return (Vec3){ v.x, v.y, v.z + f };
        case WEST:
        case EAST: return (Vec3){ v.x + f, v.y, v.z };
        case UP:
        case DOWN: return (Vec3){ v.x, v.y + f, v.z };
        default: return VEC3_0;
    }
}

float getDistanceAlongAxis(Direction direction, Vec3 axis_position, Vec3 entity_position)
{
    switch (direction)
    {
        case NORTH:
        case SOUTH: return floatMax(floatAbs(axis_position.x - entity_position.x), floatAbs(axis_position.y - entity_position.y));
        case WEST:
        case EAST: return floatMax(floatAbs(axis_position.y - entity_position.y), floatAbs(axis_position.z - entity_position.z));
        case UP:
        case DOWN: return floatMax(floatAbs(axis_position.x - entity_position.x), floatAbs(axis_position.z - entity_position.z));
        default: return 0;
    }
}

// PUSH ENTITES

bool canPush(Int3 coords, Direction direction)
{
    Int3 current_coords = coords;
    TileType current_tile = getTileType(current_coords);
    FOR(push_index, MAX_ENTITY_PUSH_COUNT)
    {
        Entity* e = getEntityAtCoords(current_coords);
        if (isEntity(current_tile) && e->locked) return false;
        //if (!vec3IsZero(vec3SetComponentAlongDirection(direction, e->velocity, 0))) return false; // e moving in some direction not in the push direction
        if (e->falling) return false;

        // if will fall, don't allow push.
        Int3 coords_below = getNextCoords(current_coords, DOWN);
        TileType type_below = getTileType(coords_below);
        if (type_below == NONE && !temp_state.player_hit_by_blue) return false;

        // check within bounds
        current_coords = getNextCoords(current_coords, direction);
        if (!intCoordsWithinLevelBounds(current_coords)) return false;

        TrailingHitbox th;
        if (trailingHitboxAtCoords(current_coords, &th)) return false;

        current_tile = getTileType(current_coords);
        if (current_tile == NONE) return true;
        if (current_tile == GRID || current_tile == WALL || current_tile == LADDER ) return false;
    }
    return false; // only here if hit the max entity push count
}

bool canPushUp(Int3 coords)
{
    int32 stack_size = getPushableStackSize(coords);
    Int3 check_coords = coords;
    FOR(_, stack_size) check_coords = getNextCoords(check_coords, UP);
    if (getTileType(check_coords) == NONE) return true;
    else return false;
}

// pass in entity id, finds next free in entites_tied_to_movement if not already tracked; returns write id for that entity if already tracked
int32 getWriteIndexInTiedEntities(int32 entity_id)
{
    FOR(tied_entity_index, MAX_ENTITIES_TIED_TO_MOVEMENT)
    {
        if (temp_state.entities_tied_to_movement[tied_entity_index].id == entity_id) return tied_entity_index;
        if (temp_state.entities_tied_to_movement[tied_entity_index].id > 0) continue;
        return tied_entity_index;
    }
    return -1;
}

// assumes at least the bottom of the stack is able to be pushed 
void pushAll(Int3 coords, Direction direction, bool on_head, Entity* root_entity)
{
    Int3 current_coords = coords;
    int32 push_count = 0;
    FOR(push_index, MAX_ENTITY_PUSH_COUNT)
    {
        if (getTileType(current_coords) == NONE) break;
        current_coords = getNextCoords(current_coords, direction);
        push_count++;
    }
    current_coords = getNextCoords(current_coords, oppositeDirection(direction));

    for (int32 inverse_push_index = push_count; inverse_push_index != 0; inverse_push_index--)
    {
        int32 stack_size = getPushableStackSize(current_coords);
        Int3 current_stack_coords = current_coords;
        FOR(stack_index, stack_size)
        {
            Entity* e = getEntityAtCoords(current_stack_coords);
            Int3 next_coords = getNextCoords(e->coords, direction);
            TileType next_type = getTileType(next_coords);

            if (next_type != NONE) break; // this is possible because of the inverse push index seeking. if not none, won't be pushable either, so break.

            createTrailingHitbox(e->id, e->coords, TRAILING_HITBOX_TIME, getTileType(e->coords));
            moveEntityInBufferAndState(e, next_coords, e->direction);

            int32 write_index = getWriteIndexInTiedEntities(e->id);
            if (write_index == -1) continue;

            temp_state.entities_tied_to_movement[write_index].id = e->id;
            temp_state.entities_tied_to_movement[write_index].direction = direction;
            temp_state.entities_tied_to_movement[write_index].on_head = on_head;
            temp_state.entities_tied_to_movement[write_index].root_entity = root_entity;
            temp_state.entities_tied_to_movement[write_index].tied_to_pack_and_decoupled = false;

            current_stack_coords = getNextCoords(current_stack_coords, UP);
        }
        current_coords = getNextCoords(current_coords, oppositeDirection(direction));
    }
}

// assumes able to be pushed
void pushUp(Int3 coords, Entity* root_entity)
{
    int32 stack_size = getPushableStackSize(coords);
    Int3 current_coords = coords;
    FOR(_, stack_size - 1) current_coords = getNextCoords(current_coords, UP); // put current coords at top of stack

    for (int32 inverse_stack_index = stack_size; inverse_stack_index != 0; inverse_stack_index--)
    {
        // iterate down the stack
        Entity* e = getEntityAtCoords(current_coords);
        Int3 next_coords = getNextCoords(current_coords, UP);

        createTrailingHitbox(e->id, e->coords, TRAILING_HITBOX_TIME, getTileType(e->coords));
        moveEntityInBufferAndState(e, next_coords, e->direction);

        int32 write_index = getWriteIndexInTiedEntities(e->id);
        if (write_index == -1) continue;

        temp_state.entities_tied_to_movement[write_index].id = e->id;
        temp_state.entities_tied_to_movement[write_index].direction = UP;
        temp_state.entities_tied_to_movement[write_index].on_head = false; // is this important for anything in this context?
        temp_state.entities_tied_to_movement[write_index].root_entity = root_entity;
        temp_state.entities_tied_to_movement[write_index].tied_to_pack_and_decoupled = false;

        current_coords = getNextCoords(current_coords, DOWN);
    }
}

// LASERS

Vec3 getNormCoordsWithEntityCoordAlongAxis(Direction direction, Vec3 current_norm_coords, Vec3 mirror_position)
{
    Vec3 norm_coords_not_along_axis = vec3SetComponentAlongDirection(direction, current_norm_coords, 0);
    Vec3 mirror_coords_along_axis = vec3ScalarMultiply(directionToVector(direction), getSignedComponentAlongDirection(direction, mirror_position));
    return vec3Add(norm_coords_not_along_axis, mirror_coords_along_axis);
}

void updateLaserBuffer()
{
    Entity* player = &world_state.player;

    // set all lasers to inactive. will make them active in the loop
    memset(laser_buffer, 0, sizeof(laser_buffer));

    temp_state.player_hit_by_red   = false;
    temp_state.player_hit_by_blue  = false;

    // if a source is magenta, create entry in sources_as_primary of it as both red and blue
    // TODO: probably shouldn't rebuild this buffer every time function is called, could just update when sources are moved / on level rebuild
    Entity sources_as_primary[256] = {0};
    int32 primary_index = 0;
    FOR(source_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        Entity* s = &world_state.sources[source_index];
        if (s->removed || s->locked) continue;
        if (s->color < MAGENTA)
        {
            sources_as_primary[primary_index++] = *s;
        }
        else if (s->color == MAGENTA)
        {
            sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = RED;
            sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index++].color = BLUE;
        }
    }

    FOR(source_index, MAX_SOURCE_COUNT) // iterate over laser (primary) sources
    {
        Entity* source = &sources_as_primary[source_index];

        Direction current_direction = source->direction;
        Vec3 current_norm_coords = source->position;
        Int3 current_tile_coords = roundNormCoordsToInt(current_norm_coords);

        // idea here: mirrors and lasers when pushed can collide with themselves because they take up two tiles while the trailing hitbox is active
        // only mirror and laser ids can be skipped.
        int32 id_to_skip = 0;
        int32 id_to_skip_timer = 0;

        FOR(laser_turn_index, MAX_LASER_TURNS_ALLOWED) // iterate over laser segments
        {
            bool no_more_turns = true;

            LaserBuffer* lb = &laser_buffer[source_index * MAX_LASER_TURNS_ALLOWED + laser_turn_index];

            // start of some segment: always move one tile forward from where we are before we start checking for anything
            float laser_source_start_offset = game_shader_mode == OLD ? 0.5f : 0.4f;
            if (laser_turn_index == 0) lb->start_coords = vec3Add(source->position, vec3ScalarMultiply(directionToVector(current_direction), laser_source_start_offset));
            else lb->start_coords = current_norm_coords;
            lb->direction = current_direction;
            lb->color = source->color;
            if (laser_turn_index > 0) lb->start_clip_plane = laser_buffer[source_index * MAX_LASER_TURNS_ALLOWED + laser_turn_index - 1].end_clip_plane;
            else lb->start_clip_plane = (Vec4){ 0, 0, 0, 1 };
            lb->end_clip_plane = (Vec4){ 0, 0, 0, 1 };

            current_norm_coords = vec3Add(directionToVector(current_direction), current_norm_coords);
            current_tile_coords = roundNormCoordsToInt(current_norm_coords);

            FOR(laser_tile_index, MAX_LASER_TRAVEL_DISTANCE) // iterate over individual tiles
            {
                bool advance_tile = true; // used to break out of both for loops, or not

                // first tile on first turn. skip source id.
                if (laser_turn_index == 0 && laser_tile_index == 0)
                {
                    id_to_skip = source->id;
                    id_to_skip_timer = 2;
                }

                // decrease id_to_skip_timer if > 0, so that all entities that get skipped (even those set last pass) have only one check of being skipped. if no more skipping, remove to-skip id 
                if (id_to_skip_timer > 0) id_to_skip_timer--;
                else id_to_skip = 0;

                // stop if oob
                if (!intCoordsWithinLevelBounds(current_tile_coords))
                {
                    lb->end_coords = current_norm_coords;
                    break;
                }

                TileType types_to_check[2] = { NONE, getTileType(current_tile_coords) }; // trailing hitbox, followed by real type; trailing hitbox intersection takes priority
                TrailingHitbox th = {0};
                if (trailingHitboxAtCoords(current_tile_coords, &th) && th.frames > 0)
                {
                    types_to_check[0] = th.type;
                }

                FOR(check, 2)
                {
                    TileType hit_type = types_to_check[check];
                    if (hit_type == NONE) continue;
                    bool this_is_th = check == 0;

                    if (hit_type == PLAYER)
                    {
                        float distance_from_player = getDistanceAlongAxis(current_direction, current_norm_coords, player->position);
                        if (distance_from_player > 0.5f)
                        {
                            // passthrough
                            continue;
                        }

                        Vec3 coords_without_offset = getNormCoordsWithEntityCoordAlongAxis(current_direction, current_norm_coords, player->position);
                        lb->end_coords = vec3Add(coords_without_offset, vec3ScalarMultiply(directionToVector(current_direction), -0.375f));
                        current_norm_coords = player->position;

                        // set player color
                        if (source->color == RED)  temp_state.player_hit_by_red = true;
                        if (source->color == BLUE) temp_state.player_hit_by_blue = true;

                        advance_tile = false;
                        break;
                    }

                    if (hit_type == MIRROR)
                    {
                        // get mirror entity
                        Entity* mirror = {0};
                        if (this_is_th) mirror = getEntityFromId(th.id);
                        else mirror = getEntityAtCoords(current_tile_coords);

                        // check if should skip this id, if so passthrough
                        bool passthrough = false;
                        bool end_here = false;
                        if (mirror->id == id_to_skip) passthrough = true;

                        float distance_from_mirror_along_axes = getDistanceAlongAxis(current_direction, current_norm_coords, mirror->position);
                        if (distance_from_mirror_along_axes > 0.5) passthrough = true;

                        if (passthrough)
                        {
                            // passthrough
                            continue;
                        }

                        // find next laser direction, and mirror normal direction. also decide how to clip: different when hitting the mirror side-on compared to hitting back of mirror
                        Direction next_laser_direction = NO_DIRECTION;
                        Vec3 mirror_normal = VEC3_0;
                        bool backside_clip_plane = false;
                        switch (mirror->mirror_orientation)
                        {
                            case MIRROR_SIDE:
                            {
                                if (current_direction != UP && current_direction != DOWN) // check first because modulo arithmetic assumes 4-way dir
                                {
                                    if (mirror->direction == current_direction) next_laser_direction = (current_direction) + 1 % 4;
                                    else if (current_direction == (mirror->direction + 3) % 4) next_laser_direction = (mirror->direction + 2) % 4;
                                    else backside_clip_plane = true;
                                }

                                Direction front_axis = oppositeDirection(mirror->direction);
                                Direction side_axis = (mirror->direction + 1) % 4;
                                mirror_normal = vec3Normalize(vec3Add(directionToVector(front_axis), directionToVector(side_axis)));
                                break;
                            }
                            case MIRROR_UP:
                            {
                                if (current_direction == DOWN) next_laser_direction = (mirror->direction + 1) % 4;
                                else if (current_direction == UP) backside_clip_plane = true;
                                else if (mirror->direction == (current_direction + 1) % 4) next_laser_direction = UP;
                                else if (mirror->direction == (current_direction + 3) % 4) backside_clip_plane = true;

                                Direction horizontal_axis = (mirror->direction + 1) % 4;
                                mirror_normal = vec3Normalize(vec3Add(directionToVector(horizontal_axis), directionToVector(UP)));
                                break;
                            }
                            case MIRROR_DOWN:
                            {
                                if (current_direction == UP) next_laser_direction = (mirror->direction + 1) % 4;
                                else if (current_direction == DOWN) backside_clip_plane = true;
                                else if (mirror->direction == (current_direction + 1) % 4) next_laser_direction = DOWN;
                                else if (mirror->direction == (current_direction + 3) % 4) backside_clip_plane = true;

                                Direction horizontal_axis = (mirror->direction + 1) % 4;
                                mirror_normal = vec3Normalize(vec3Add(directionToVector(horizontal_axis), directionToVector(DOWN)));
                                break;
                            }
                            default: break;
                        }

                        if (next_laser_direction == NO_DIRECTION) 
                        {
                            Vec3 coords_without_offset = getNormCoordsWithEntityCoordAlongAxis(current_direction, current_norm_coords, mirror->position);
                            if (backside_clip_plane)
                            {
                                lb->end_coords = vec3Add(coords_without_offset, vec3ScalarMultiply(directionToVector(current_direction), 0.5f));

                                float origin_offset = -vec3Inner(mirror_normal, coords_without_offset);
                                lb->end_clip_plane = (Vec4){ -mirror_normal.x, -mirror_normal.y, -mirror_normal.z, -origin_offset };

                                advance_tile = false;
                            }
                            else
                            {
                                lb->end_coords = vec3Add(coords_without_offset, vec3ScalarMultiply(directionToVector(current_direction), -0.38f));
                                advance_tile = false;
                            }
                            break;
                        }

                        /* 
                        TODO: think about this more; i do want this functionality, but this will mean that when pushing as in blue-business-i, the laser will 
                              hit neither mirror nor player for approx. 2 frames, which means that the object will fall. could encode a special case, or just
                              have a fall timer, so that objects take a few frames to start falling after being blue, or ... something
                        if (distance_from_mirror_along_axes > 0.35)
                        {
                            // between 0.5 and 0.3, so this hits the 'edge' of the mirror: break the laser
                            // still want to do later calculations to calculate exact coords to end
                            end_here = true;
                        }
                        */

                        if (distance_from_mirror_along_axes == 0)
                        {
                            lb->end_coords = mirror->position;

                            float origin_offset = -vec3Inner(mirror_normal, lb->end_coords);
                            lb->end_clip_plane = (Vec4){ mirror_normal.x, mirror_normal.y, mirror_normal.z, origin_offset };

                            current_norm_coords = mirror->position;
                            current_direction = next_laser_direction;
                            advance_tile = false;
                            no_more_turns = false;
                            break;
                        }

                        // get difference along next_laser_direction of current_norm_coords vs mirror->position.
                        // this will be relevantly signed because getSignedComponentAlongDirection gives signed output.
                        // add that difference to norm_coords along current_direction. again signs are accounted for because directionToVector gives signed output.
                        // differences along the other axis (the one orthogonal to both current dir and next dir) are ignored, because they don't change point of reflection
                        // to get norm coords, add corresponding difference, plus norm_coord_difference along the axes that aren't current_direction axis
                        Vec3 norm_coord_difference = vec3Subtract(current_norm_coords, mirror->position);
                        float difference_along_next_laser_direction_axis = getSignedComponentAlongDirection(next_laser_direction, norm_coord_difference);
                        Vec3 corresponding_difference_along_current_direction_axis = vec3ScalarMultiply(directionToVector(current_direction), difference_along_next_laser_direction_axis);
                        Vec3 norm_coord_difference_not_along_current_direction_axis = vec3SetComponentAlongDirection(current_direction, norm_coord_difference, 0);
                        current_norm_coords = vec3Add(mirror->position, vec3Add(norm_coord_difference_not_along_current_direction_axis, corresponding_difference_along_current_direction_axis));

                        if (!end_here)
                        {
                            id_to_skip = mirror->id;
                            id_to_skip_timer = 2;
                            no_more_turns = false;
                            current_direction = next_laser_direction;
                        }
                        lb->end_coords = current_norm_coords;

                        // overwrite old clip plane calculation with new end coords
                        float new_origin_offset = -vec3Inner(mirror_normal, lb->end_coords);
                        lb->end_clip_plane = (Vec4){ mirror_normal.x, mirror_normal.y, mirror_normal.z, new_origin_offset };

                        advance_tile = false;
                        break;
                    }

                    // hit type is something that isn't NONE - do default behaviour
                    //if (hit_type != NONE)
                    {
                        Vec3 coords_without_offset = VEC3_0;
                        float offset = 0.0f;
                        // if entity there could be a real hit with a passthrough. in any other case, just stop here.
                        if (isEntity(hit_type))
                        {
                            Entity* e = {0};
                            if (this_is_th) e = getEntityFromId(th.id);
                            else e = getEntityAtCoords(current_tile_coords);

                            // TODO: temp so that i can get into the overworld after this shift
                            if (!e) continue;

                            // check if should skip this id, if so passthrough
                            bool passthrough = false;
                            if (e->id == id_to_skip) passthrough = true;

                            // default distance check for passthrough
                            float distance_from_entity = getDistanceAlongAxis(current_direction, current_norm_coords, e->position);
                            if (distance_from_entity > 0.5) passthrough = true;

                            if (passthrough)
                            {
                                // passthrough
                                continue;
                            }

                            coords_without_offset = getNormCoordsWithEntityCoordAlongAxis(current_direction, current_norm_coords, e->position);
                            offset = -0.5f;
                            if (e->id == PACK_ID) offset = -0.375f;
                        }
                        else
                        {
                            coords_without_offset = getNormCoordsWithEntityCoordAlongAxis(current_direction, current_norm_coords, intCoordsToNorm(current_tile_coords));
                            offset = -0.5f;
                        }

                        lb->end_coords = vec3Add(coords_without_offset, vec3ScalarMultiply(directionToVector(current_direction), offset));
                        advance_tile = false;
                        break;
                    }
                }
                
                if (advance_tile)
                {
                    current_norm_coords = vec3Add(directionToVector(current_direction), current_norm_coords);
                    current_tile_coords = roundNormCoordsToInt(current_norm_coords);
                }
                else break;
            }

            if (no_more_turns) 
            break;
        }
    }
}

// FALLING LOGIC

bool canFall(Entity* e)
{
    if (e->removed) return false;
    Int3 next_coords = getNextCoords(e->coords, DOWN); 

    // only allow fall if below is nothing, or void
    if (!intCoordsWithinLevelBounds(next_coords)) return false;
    if (getTileType(next_coords) != NONE && getTileType(next_coords) != VOID) return false;

    return true;
}

void setFalling(Entity* e)
{
    // canFall will only return true for the bottom entity in a stack. so whenever this check is passed, the first entity is always the one in the bottom of the stack.
    if (!canFall(e)) return;

    // remove if above void: early return for this entity, but call doFallingEntity for the entity above, if there is one, so that it doesn't get its fall interrupted
    Int3 below = getNextCoords(e->coords, DOWN);
    if (getTileType(below) == VOID)
    {
        setTileType(NONE, e->coords);
        setTileDirection(NO_DIRECTION, e->coords, e->mirror_orientation);
        e->removed = true;
        Int3 coords_above = getNextCoords(e->coords, UP);

        if (isPushable(getTileType(coords_above)))
        {
            Entity* e_above = getEntityAtCoords(coords_above);
            if (e_above) setFalling(e_above);
        }
        return;
    }

    Int3 next_coords = getNextCoords(e->coords, DOWN);
    int32 stack_size = getPushableStackSize(e->coords);
    Int3 current_start_coords = e->coords;
    Int3 current_end_coords = next_coords; 

    FOR(stack_fall_index, stack_size)
    {
        Entity* e_in_stack = getEntityAtCoords(current_start_coords);
        if (e_in_stack->removed) return; // if any entities in the stack are removed, or moving, don't do the fall

        // two checks which have to do with breaking a stack so that everything below a point falls, but not above a certain point
        if (e_in_stack->id == PACK_ID && temp_state.pack_attached && stack_fall_index != 0) break; // if e is pack, and pack_attached, and this isn't the entity on which the function was called, break fall here
        if (e_in_stack->id == PLAYER_ID && temp_state.player_hit_by_red && stack_fall_index != 0) break; // similarly, if player is red above a falling stack, then break to allow stack to fall, but keep player floating

        e_in_stack->falling = true;

        current_end_coords = current_start_coords;
        current_start_coords = getNextCoords(current_start_coords, UP);
    }
    return;
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

void updateTextInput(Input *input)
{
    for (int32 chars_typed_index = 0; chars_typed_index < input->text.count; chars_typed_index++)
    {
        uint32 codepoint = input->text.codepoints[chars_typed_index];
        char character = (char)codepoint;
        if (character == '\b') editBackspace();
        else editAppendChar(character);
    }
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
    if (level_name == 0) strcpy(world_state.level_name, debug_level_name);
    else strcpy(world_state.level_name, level_name);

    memset(laser_buffer, 0, sizeof(laser_buffer));

    Entity* player = &world_state.player;
    Entity* pack = &world_state.pack;

    // memset worldstate to 0 (with persistant level_name, and solved levels)
    char persist_level_name[256] = {0};
    char persist_solved_levels[64][64] = {0};
    strcpy(persist_level_name, world_state.level_name);
    memcpy(persist_solved_levels, world_state.solved_levels, sizeof(persist_solved_levels));

    memset(&world_state, 0, sizeof(WorldState));

    strcpy(world_state.level_name, persist_level_name);
    memcpy(world_state.solved_levels, persist_solved_levels, sizeof(persist_solved_levels));

    if (strcmp(world_state.level_name, "overworld") == 0) temp_state.in_overworld = true;
    else temp_state.in_overworld = false;

    // build level_path from level_name
    char level_path[64] = {0};
    buildLevelPathFromName(world_state.level_name, &level_path, false);
    FILE* file = fopen(level_path, "rb+");
    loadBufferInfo(file);
    fclose(file);

    memset(world_state.boxes,         0, sizeof(world_state.boxes)); 
    memset(world_state.mirrors,       0, sizeof(world_state.mirrors));
    memset(world_state.glass_blocks,  0, sizeof(world_state.glass_blocks));
    memset(world_state.sources,       0, sizeof(world_state.sources));
    memset(world_state.win_blocks,    0, sizeof(world_state.win_blocks));
    memset(world_state.locked_blocks, 0, sizeof(world_state.locked_blocks));
    FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT) // NOTE: id -1 distinguishes between 'slot was never used' and removed which means was once used, 
    {                                            // now free. this is important for locked_blocks, so if want to change this, need slot_ever_used bool or something
        world_state.boxes[entity_index].id          = -1;
        world_state.mirrors[entity_index].id        = -1;
        world_state.glass_blocks[entity_index].id   = -1;
        world_state.sources[entity_index].id        = -1;
        world_state.win_blocks[entity_index].id     = -1;
        world_state.locked_blocks[entity_index].id  = -1;
    }

    Entity *entity_group = 0;
    for (int buffer_index = 0; buffer_index < 2 * level_dim.x*level_dim.y*level_dim.z; buffer_index += 2)
    {
        TileType buffer_contents = world_state.buffer[buffer_index];
        if      (buffer_contents == BOX)          entity_group = world_state.boxes;
        else if (buffer_contents == MIRROR)       entity_group = world_state.mirrors;
        else if (buffer_contents == GLASS)        entity_group = world_state.glass_blocks;
        else if (buffer_contents == WIN_BLOCK)    entity_group = world_state.win_blocks;
        else if (buffer_contents == LOCKED_BLOCK) entity_group = world_state.locked_blocks;
        else if (isSource(buffer_contents))       entity_group = world_state.sources;
        if (entity_group != 0)
        {
            int32 count = getEntityCount(entity_group);
            Entity* e = &entity_group[count];
            e->coords = bufferIndexToCoords(buffer_index);
            e->position = intCoordsToNorm(e->coords);
            if (entity_group == world_state.mirrors)
            {
                e->direction = world_state.buffer[buffer_index + 1] % 4;
                e->mirror_orientation = world_state.buffer[buffer_index + 1] / 4;
                e->rotation = mirrorRotation(e->direction, e->mirror_orientation);
            }
            else
            {
                e->direction = world_state.buffer[buffer_index + 1];
                e->mirror_orientation = 0;
                e->rotation = directionToQuaternion(e->direction);
            }
            e->color = getEntityColor(e->coords);
            e->id = getEntityCount(entity_group) + entityIdOffset(entity_group, e->color);
            e->removed = false;
            entity_group = 0;
        }
        else if (world_state.buffer[buffer_index] == PLAYER)
        {
            player->coords = bufferIndexToCoords(buffer_index);
            player->position = intCoordsToNorm(player->coords);
            player->direction = world_state.buffer[buffer_index + 1];
            player->rotation = directionToQuaternion(player->direction);
            player->id = PLAYER_ID;
        }
        else if (world_state.buffer[buffer_index] == PACK)
        {
            pack->coords = bufferIndexToCoords(buffer_index);
            pack->position = intCoordsToNorm(pack->coords);
            pack->direction = world_state.buffer[buffer_index + 1];
            pack->rotation = directionToQuaternion(pack->direction);
            pack->id = PACK_ID;
        }
    }

    file = fopen(level_path, "rb+");
    saved_main_camera = loadCameraInfo(file, false);
    saved_alt_camera = loadCameraInfo(file, true);

    temp_state.allow_movement_timer = 0;
    temp_state.pack_attached = true;
    player->climbing_direction = NO_DIRECTION;

    if (temp_state.in_overworld)
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
    fclose(file);

    loadSolvedLevelsFromFile();

    camera_screen_offset.x = (int32)(camera.coords.x / OVERWORLD_SCREEN_SIZE_X);
    camera_screen_offset.z = (int32)(camera.coords.z / OVERWORLD_SCREEN_SIZE_Z);
    camera.rotation = buildCameraQuaternion(camera);
    camera_target_plane = player->coords.y;

    updateLaserBuffer();
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
    memcpy(&overworld_zero_state, &world_state, sizeof(WorldState));

    gameInitializeState(level_name);
}

void gameRedraw(DisplayInfo display_from_platform)
{
    if (draw_command_count == 0) return;
    game_display = display_from_platform;
    recalculateDebugStartCoords();
    vulkanSubmitFrame(draw_commands, draw_command_count, (float)global_time, camera_with_ow_offset, game_shader_mode); 
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

// don't call if there was level change
void popLastUndoAction()
{
    if (undo_buffer.header_count == 0) return;

    uint32 header_index = (undo_buffer.header_write_pos + MAX_UNDO_ACTIONS - 1) % MAX_UNDO_ACTIONS;
    UndoActionHeader* header = &undo_buffer.headers[header_index];
    undo_buffer.delta_write_pos = header->delta_start_pos;
    undo_buffer.delta_count -= header->entity_count;
    undo_buffer.header_write_pos = header_index;
    undo_buffer.header_count--;
}

// called after a noraml (non-level-change) action
// diffs world_state vs. world_state and stores deltas for every entity that changed
void recordActionForUndo(WorldState* old_state, bool action_was_reset, bool action_was_climb)
{
    if (undo_buffer.header_count >= MAX_UNDO_ACTIONS) evictOldestUndoAction();

    uint32 header_index = undo_buffer.header_write_pos;
    uint32 delta_start = undo_buffer.delta_write_pos;
    uint32 entity_count = 0;

    recordEntityDelta(&old_state->player);
    recordEntityDelta(&old_state->pack);
    entity_count += 2;

    // other entities
    Entity* groups[5] = { old_state->boxes, old_state->mirrors, old_state->sources, old_state->win_blocks, old_state->locked_blocks };
    FOR(group_index, 5)
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
    recordEntityDelta(&world_state.player);
    recordEntityDelta(&world_state.pack);
    entity_count += 2;

    Entity* groups[5] = { world_state.boxes, world_state.mirrors, world_state.sources, world_state.win_blocks, world_state.locked_blocks };
    FOR(group_index, 5)
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

void zeroAnimations()
{
    // set all entities velocity to zero, and rotation to equal their direction
    Entity* moving_entity_group[3] = { world_state.boxes, world_state.mirrors, world_state.sources };
    FOR(group_index, 3)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* e = &moving_entity_group[group_index][entity_index];
            e->velocity = VEC3_0;
            e->rotation = directionToQuaternion(e->direction);
        }
    }

    Entity* player = &world_state.player;
    Entity* pack = &world_state.pack;
    player->rotation = directionToQuaternion(player->direction);
    player->velocity = VEC3_0;
    pack->rotation = directionToQuaternion(pack->direction);
    pack->velocity = VEC3_0;

    player->climbing_direction = NO_DIRECTION;

    memset(temp_state.entities_tied_to_movement, 0, sizeof(temp_state.entities_tied_to_movement));
    temp_state.pack_turn_state.pack_intermediate_states_timer = 0;
}

// returns false only if already at oldest action
bool performUndo()
{
    if (undo_buffer.header_count == 0) return false;

    // clear animations + trailing hitboxes
    memset(temp_state.trailing_hitboxes, 0, sizeof(temp_state.trailing_hitboxes));
    zeroAnimations();

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
            setTileDirection(NORTH, e->coords, e->mirror_orientation);
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
            TileType type = getTileTypeFromId(delta->id);
            e->coords = delta->old_coords;
            e->position = intCoordsToNorm(e->coords);
            e->direction = delta->old_direction;
            e->mirror_orientation = delta->old_mirror_orientation;
            if (type == MIRROR) e->rotation = mirrorRotation(e->direction, e->mirror_orientation);
            else e->rotation = directionToQuaternion(e->direction);
            e->removed = delta->was_removed;

            if (!delta->was_removed)
            {
                setTileType(type, delta->old_coords);
                setTileDirection(delta->old_direction, delta->old_coords, delta->old_mirror_orientation);
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

    //writeUndoBufferToFile();

    return true;
}

void levelChangePrep(char next_level[64])
{
    bool level_was_just_solved = false;
    if (!temp_state.in_overworld && findInSolvedLevels(world_state.level_name) == -1)
    {
        addToSolvedLevels(world_state.level_name);
        writeSolvedLevelsToFile();
        level_was_just_solved = true;
    }
    
    recordLevelChangeForUndo(world_state.level_name, level_was_just_solved);

    if (strcmp(next_level, "overworld") == 0) temp_state.in_overworld = true;
    else temp_state.in_overworld = false;
}

// MOVEMENT

void doStandardMovement(Direction direction, Int3 next_player_coords)
{
    Entity* player = &world_state.player;
    Entity* pack = &world_state.pack;

    // maybe move stack above the player's head
    Int3 coords_above_player = getNextCoords(player->coords, UP);
    bool do_on_head_movement = false;
    if (isPushable(getTileType(coords_above_player)) && canPush(coords_above_player, direction)) do_on_head_movement = true;
    if (temp_state.player_hit_by_blue) do_on_head_movement = false;
    if (do_on_head_movement) pushAll(coords_above_player, direction, true, player);

    createTrailingHitbox(PLAYER_ID, player->coords, TRAILING_HITBOX_TIME, PLAYER);
    moveEntityInBufferAndState(player, next_player_coords, player->direction);

    // move pack also if pack is attached
    if (temp_state.pack_attached)
    {
        createTrailingHitbox(PACK_ID, pack->coords, TRAILING_HITBOX_TIME, PACK);
        Int3 next_pack_coords = getNextCoords(pack->coords, direction);
        moveEntityInBufferAndState(pack, next_pack_coords, pack->direction);
    }
}

void updatePackDetached()
{
    Entity* player = &world_state.player;
    Entity* pack = &world_state.pack;

    TileType tile_behind_player = getTileType(getNextCoords(world_state.player.coords, oppositeDirection(world_state.player.direction)));
    if (tile_behind_player == PACK || temp_state.pack_turn_state.pack_intermediate_states_timer > 0) 
    {
        temp_state.pack_attached = true;
        setTileDirection(player->direction, pack->coords, 0);
        pack->direction = player->direction;
    }
    else temp_state.pack_attached = false;
}

// expects positive value for deceleration
float oneDimensionalDecelerationSimulation(float initial_velocity, float deceleration)
{
    float velocity = initial_velocity;
    float offset = 0;
    while (true)
    {
        offset += velocity;
        velocity -= deceleration;
        if (velocity <= 0) break;
    }
    return offset;
}

float calculateSpeculativeVelocityAlongDirection(Direction direction, float sign)
{
    Entity* player = &world_state.player;

    // get velocity of case where we fully accelerate on this frame. clamp velocity to max speed
    Vec3 velocity_to_add = vec3ScalarMultiply(directionToVector(direction), PLAYER_ACCELERATION); // may be negative, if direction is N or W
    Vec3 unclamped_speculative_velocity = vec3Add(player->velocity, velocity_to_add);             // in that case, velocity is also negative, so add works correctly.
    float unclamped_speculative_velocity_along_direction = getComponentAlongDirection(direction, unclamped_speculative_velocity); // may be negative
    float speculative_velocity_along_direction = unclamped_speculative_velocity_along_direction;
    if (sign == -1.0f) speculative_velocity_along_direction = speculative_velocity_along_direction < -PLAYER_MAX_SPEED ? -PLAYER_MAX_SPEED : speculative_velocity_along_direction;
    else               speculative_velocity_along_direction = speculative_velocity_along_direction >  PLAYER_MAX_SPEED ?  PLAYER_MAX_SPEED : speculative_velocity_along_direction;
    return speculative_velocity_along_direction;
}

bool wouldOvershoot(float speculative_velocity_along_direction, float position_along_direction, float coords_along_direction, float sign)
{
    float offset_from_current_position_if_accelerate = oneDimensionalDecelerationSimulation(sign * speculative_velocity_along_direction, PLAYER_MAX_DECELERATION);
    float position_after_accelerate_then_immediately_decelerate = sign * offset_from_current_position_if_accelerate + position_along_direction;
    bool would_overshoot = false;
    if (sign == -1.0f)
    {
        if (position_after_accelerate_then_immediately_decelerate - 0.001f < coords_along_direction) would_overshoot = true;
    }
    else
    {
        if (position_after_accelerate_then_immediately_decelerate + 0.001f > coords_along_direction) would_overshoot = true;
    }
    return would_overshoot;
}

// GAME LOGIC

void doPhysicsTick()
{
    Entity* player = &world_state.player;
    Entity* pack = &world_state.pack;

    // pack turn sequence
    if (temp_state.pack_turn_state.pack_intermediate_states_timer > 0)
    {
        if (temp_state.pack_turn_state.pack_intermediate_states_timer == TURN_TIME)
        {
            Int3 diagonal_coords = temp_state.pack_turn_state.pack_intermediate_coords;
            Direction diagonal_push_direction = oppositeDirection(player->direction);
            TileType type_at_diagonal = getTileType(diagonal_coords);
            bool allow_diagonal = false;
            bool do_push = false;
            if (type_at_diagonal == NONE) allow_diagonal = true;
            if (isPushable(type_at_diagonal) && canPush(diagonal_coords, diagonal_push_direction))
            {
                allow_diagonal = true;
                do_push = true;
            }
            if (allow_diagonal)
            {
                if (do_push) pushAll(diagonal_coords, diagonal_push_direction, false, pack);
                createTrailingHitbox(PACK_ID, pack->coords, TRAILING_HITBOX_TIME, PACK);
                moveEntityInBufferAndState(pack, diagonal_coords, player->direction);
            }
            else
            {
                player->direction = temp_state.pack_turn_state.initial_player_direction;
                pack->direction = temp_state.pack_turn_state.initial_player_direction;
                temp_state.pack_turn_state.pack_intermediate_states_timer = 0;
                popLastUndoAction();
            }
        }
        else if (temp_state.pack_turn_state.pack_intermediate_states_timer == 7)
        {
            Direction orthogonal_push_direction = temp_state.pack_turn_state.initial_player_direction;
            Int3 orthogonal_coords = getNextCoords(pack->coords, orthogonal_push_direction);
            TileType type_at_orthogonal = getTileType(orthogonal_coords);
            bool allow_orthogonal = false;
            bool do_push = false;
            if (type_at_orthogonal == NONE) allow_orthogonal = true;
            if (isPushable(type_at_orthogonal) && canPush(orthogonal_coords, orthogonal_push_direction))
            {
                allow_orthogonal = true;
                do_push = true;
            }
            if (allow_orthogonal)
            {
                if (do_push) pushAll(orthogonal_coords, orthogonal_push_direction, false, pack);
                createTrailingHitbox(PACK_ID, pack->coords, TRAILING_HITBOX_TIME, PACK);
                moveEntityInBufferAndState(pack, orthogonal_coords, player->direction);
            }
            else
            {
                Int3 start_pack_coords = getNextCoords(player->coords, oppositeDirection(temp_state.pack_turn_state.initial_player_direction));
                moveEntityInBufferAndState(pack, start_pack_coords, player->direction);
                player->direction = temp_state.pack_turn_state.initial_player_direction;
                temp_state.pack_turn_state.pack_intermediate_states_timer = 0;
                popLastUndoAction();

                temp_state.allow_movement_timer = 6; // TODO: i probably want to allow all movement except this same turn again, not just disable all movement for a few frames
            }
        }
    }
    if (temp_state.pack_turn_state.pack_intermediate_states_timer > 0) temp_state.pack_turn_state.pack_intermediate_states_timer--;

    updatePackDetached();

    // climb logic
    if (player->climbing_direction == UP)
    {
        float CLIMBING_SPEED = 0.1f; // TODO: global. also check with something that doesn't divide into 1.0 later

        float y_coord_difference = getComponentAlongDirection(UP, vec3Subtract(intCoordsToNorm(player->coords), player->position));

        if (y_coord_difference > CLIMBING_SPEED)
        {
            // just keep climbing, already commited to this movement.
            player->position.y += CLIMBING_SPEED;
            player->velocity.y = CLIMBING_SPEED;
            pack->position.y += CLIMBING_SPEED;
            pack->velocity.y = CLIMBING_SPEED;
        }
        else
        {
            // movement crosses tile boundary so will require a check against what's above and infront:
            // maybe continue climbing
            // maybe pack should detach but otherwise keep climbing
            // maybe end climb and start moving forwards
            // maybe reverse direction if path is occupied either above or infront

            bool try_climb_more = false;
            bool move_forwards = false;
            bool do_push_forwards = false;
            bool reverse_direction = false;

            Int3 coords_ahead = getNextCoords(player->coords, player->direction);
            TileType type_ahead = getTileType(coords_ahead);

            if (type_ahead == LADDER)
            {
                try_climb_more = true;
            }
            else if (type_ahead == NONE)
            {
                move_forwards = true;
            }
            else if (isPushable(type_ahead) && canPush(coords_ahead, player->direction))
            {
                move_forwards = true;
                do_push_forwards = true;
            }
            else
            {
                reverse_direction = true;
            }

            if (try_climb_more)
            {
                bool climb_more = false;
                bool push_up = false;

                Int3 coords_above_player = getNextCoords(player->coords, UP);
                TileType type_above_player = getTileType(coords_above_player);

                if (type_above_player == NONE)
                {
                    climb_more = true;
                }
                else if (isPushable(type_above_player) && canPushUp(coords_above_player))
                {
                    climb_more = true;
                    push_up = true;
                }

                if (climb_more)
                {
                    createTrailingHitbox(PLAYER_ID, player->coords, TRAILING_HITBOX_TIME, PLAYER);
                    if (push_up) pushUp(coords_above_player, player);
                    moveEntityInBufferAndState(player, coords_above_player, player->direction);

                    player->position.y += CLIMBING_SPEED;
                    player->velocity.y = CLIMBING_SPEED;

                    if (temp_state.pack_attached)
                    {
                        bool pack_stays_with_player = false;
                        bool pack_pushes = false;

                        Int3 coords_above_pack = getNextCoords(pack->coords, UP);
                        TileType type_above_pack = getTileType(coords_above_pack);

                        if (type_above_pack == NONE)
                        {
                            pack_stays_with_player = true;
                        }
                        else if (isPushable(type_above_pack) && canPushUp(coords_above_pack))
                        {
                            pack_stays_with_player = true;
                            pack_pushes = true;
                        }

                        if (pack_stays_with_player)
                        {
                            createTrailingHitbox(PACK_ID, pack->coords, TRAILING_HITBOX_TIME, PACK);
                            if (pack_pushes) pushUp(coords_above_pack, pack);
                            moveEntityInBufferAndState(pack, coords_above_pack, player->direction);

                            pack->position.y += CLIMBING_SPEED;
                            pack->velocity.y = CLIMBING_SPEED;
                        }
                        else
                        {
                            temp_state.pack_attached = false;
                        }
                    }
                }
                else
                {
                    reverse_direction = true;
                }
            }

            if (move_forwards)
            {
                player->position = intCoordsToNorm(player->coords); // normalise y coord
                player->climbing_direction = NO_DIRECTION;
                player->velocity = VEC3_0;
                if (do_push_forwards) pushAll(coords_ahead, player->direction, false, player);
                doStandardMovement(player->direction, coords_ahead);
            }

            if (reverse_direction)
            {
                player->climbing_direction = DOWN;
            }
        }
    }

    if (player->climbing_direction == DOWN)
    {
        // just let player fall for now
        player->climbing_direction = NO_DIRECTION;
    }

    // falling logic for entities
    Entity* falling_entity_group[3] = { world_state.boxes, world_state.mirrors, world_state.sources };
    FOR(group_index, 4)
    {
        // handle pack as the 4th group here (if not attached). then immediately break
        bool is_pack = false;
        if (group_index == 3) is_pack = true;

        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* e;
            if (is_pack)
            {
                if (temp_state.pack_attached) break;
                e = pack;
            }
            else e = &falling_entity_group[group_index][entity_index];

            bool want_to_fall = false;
            if (!e->falling) want_to_fall = true;
            if (canFall(e)) want_to_fall = true;
            if (undo_press_timer > 0) want_to_fall = false;
            if (cheating) want_to_fall = false;
            if (!vec3IsZero(vec3SetComponentAlongDirection(DOWN, vec3Subtract(e->position, intCoordsToNorm(e->coords)), 0))) want_to_fall = false; // not stationary. not using e->velocity because it gets set after, so wouldn't work when pushing stationary object
            if (temp_state.player_hit_by_blue) want_to_fall = false;

            if (want_to_fall) setFalling(e); // only updates false -> true

            if (e->falling)
            {
                float test_y_velocity = e->velocity.y + GRAVITY;
                test_y_velocity = floatMax(test_y_velocity, MIN_DOWN_VELOCITY);
                float test_y_position = e->position.y + test_y_velocity;
                if (test_y_position > getComponentAlongDirection(DOWN, intCoordsToNorm(e->coords)))
                {
                    // within a block: update falling entity position and velocity, but not coords
                    e->velocity.y = test_y_velocity;
                    e->position.y = test_y_position;
                }
                else
                {
                    if (want_to_fall)
                    {
                        TileType type = getTileType(e->coords);
                        createTrailingHitbox(e->id, e->coords, FALL_TRAILING_HITBOX_TIME, type);

                        e->position.y = test_y_position;
                        e->velocity.y = test_y_velocity;
                        Int3 next_coords = getNextCoords(e->coords, DOWN);
                        moveEntityInBufferAndState(e, next_coords, e->direction);
                    }
                    else
                    {
                        e->position.y = (float)e->coords.y;
                        e->falling = false;
                        e->velocity.y = 0;
                    }
                }
            }
            if (is_pack) break;
        }
    }

    // player falling logic
    bool want_to_fall = false;
    if (canFall(player)) want_to_fall = true;
    if (!vec3IsZero(vec3SetComponentAlongDirection(DOWN, vec3Subtract(player->position, intCoordsToNorm(player->coords)), 0))) want_to_fall = false; // not stationary
    if (undo_press_timer > 0) want_to_fall = false;
    if (cheating) want_to_fall = false;
    if (temp_state.player_hit_by_red) want_to_fall = false;
    if (player->climbing_direction != NO_DIRECTION) want_to_fall = false;

    if (want_to_fall) setFalling(player);

    if (player->falling)
    {
        float test_y_velocity = player->velocity.y + GRAVITY;
        test_y_velocity = floatMax(test_y_velocity, MIN_DOWN_VELOCITY);
        float test_y_position = player->position.y + test_y_velocity;
        if (test_y_position > getComponentAlongDirection(DOWN, intCoordsToNorm(player->coords)))
        {
            // within a block: update player coords, and pack coords too, if pack attached
            player->velocity.y = test_y_velocity;
            player->position.y = test_y_position;
            if (temp_state.pack_attached)
            {
                pack->velocity.y = test_y_velocity;
                pack->position.y = test_y_position;
            }
        }
        else
        {
            if (want_to_fall)
            {
                createTrailingHitbox(PLAYER_ID, player->coords, FALL_TRAILING_HITBOX_TIME, PLAYER);

                player->position.y = test_y_position;
                player->velocity.y = test_y_velocity;
                Int3 player_next_coords = getNextCoords(player->coords, DOWN);
                moveEntityInBufferAndState(player, player_next_coords, player->direction);

                if (temp_state.pack_attached)
                {
                    if (canFall(pack))
                    {
                        createTrailingHitbox(PACK_ID, pack->coords, FALL_TRAILING_HITBOX_TIME, PACK);

                        pack->position.y = test_y_position;
                        pack->velocity.y = test_y_velocity;
                        Int3 pack_next_coords = getNextCoords(pack->coords, DOWN);
                        moveEntityInBufferAndState(pack, pack_next_coords, pack->direction);
                    }
                    else
                    {
                        // pack will detach
                        pack->position.y = (float)pack->coords.y;
                        pack->falling = false;
                        pack->velocity.y = 0;
                    }
                }
            }
            else
            {
                player->position.y = (float)player->coords.y;
                player->falling = false;
                player->velocity.y = 0;
                if (temp_state.pack_attached)
                {
                    pack->position.y = (float)pack->coords.y;
                    pack->falling = false;
                    pack->velocity.y = 0;
                }
            }
        }
    }

    // do animations and handle some state, in particular entities tied to player movement
    // this is in this function because of the state handling

    // player movement
    if (player->climbing_direction == NO_DIRECTION)
    {
        // handle directional movement
        FOR(direction_index, 4)
        {
            // only handle velocity / position if offset from the coords
            Vec3 difference_in_player_position = vec3Subtract(intCoordsToNorm(player->coords), player->position);
            float difference_in_position_along_direction = getComponentAlongDirection(direction_index, difference_in_player_position);
            float sign = direction_index == NORTH || direction_index == WEST ? -1.0f : 1.0f;
            if (difference_in_position_along_direction * sign <= 0) continue; // will continue if west picks up a difference in the east direction (and north in south direction)

            float position_along_direction = getComponentAlongDirection(direction_index, player->position);
            float coords_along_direction = getComponentAlongDirection(direction_index, intCoordsToNorm(player->coords));
            float speculative_velocity_along_direction = calculateSpeculativeVelocityAlongDirection(direction_index, sign);
            if (!wouldOvershoot(speculative_velocity_along_direction, position_along_direction, coords_along_direction, sign))
            {
                // no overshooting: accelerate fully
                if (direction_index == NORTH || direction_index == SOUTH) player->velocity.z = speculative_velocity_along_direction;
                else player->velocity.x = speculative_velocity_along_direction;
                player->position = vec3Add(player->position, player->velocity);
            }
            else
            {
                float current_speed = sign * getComponentAlongDirection(direction_index, player->velocity);
                float remaining_distance = sign * difference_in_position_along_direction;
                float stopping_distance = oneDimensionalDecelerationSimulation(current_speed, PLAYER_MAX_DECELERATION);
                float distance_error = remaining_distance - stopping_distance;
                int32 frames_to_stop = (int32)ceilf(current_speed / PLAYER_MAX_DECELERATION);
                float movement_adjustment = distance_error / (float)frames_to_stop;

                float decelerated_speed = current_speed - PLAYER_MAX_DECELERATION;
                if (decelerated_speed < 0) decelerated_speed = 0;

                // move position by velocity + adjustment, then set velocity to decelerated value
                float actual_movement = current_speed + movement_adjustment;

                player->position = vec3AddFloatToVec3AlongDirection(direction_index, sign * actual_movement, player->position);
                if (direction_index == NORTH || direction_index == SOUTH) player->velocity.z = sign * decelerated_speed;
                else player->velocity.x = sign * decelerated_speed;
            }
        }

        // handle turn
        {
            // player rotation
            float total_angle = getAngleOfYAxisRotation(player->rotation, directionToQuaternion(player->direction));
            float frame_count = ceilf((float)fabs(total_angle) / MAX_ANGULAR_VELOCITY);
            if (frame_count <= 1)
            {
                player->rotation = directionToQuaternion(player->direction);
            }
            else
            {
                float step_angle = total_angle / frame_count;
                Vec4 rotation_this_frame = quaternionFromAxis(intCoordsToNorm(AXIS_Y), step_angle);
                player->rotation = quaternionMultiply(rotation_this_frame, player->rotation);
            }

            // pack rotation
            if (temp_state.pack_attached)
            {
                // pack follows player movement if attached
                Vec3 rotated_offset = vec3RotateByQuaternion(intCoordsToNorm(AXIS_Z), player->rotation); // AXIS_Z because pack is 0, 0, 1 relative to player 0, 0, 0, when player has no rotation.
                pack->position = vec3Add(player->position, rotated_offset);
                pack->rotation = player->rotation;
            }
        }
    }

    // handle entities tied to movement
    FOR(tied_entity_index, MAX_ENTITIES_TIED_TO_MOVEMENT)
    {
        TiedEntity* tied_entity_info = &temp_state.entities_tied_to_movement[tied_entity_index];
        if (tied_entity_info->id <= 0) continue;

        Entity* e = getEntityFromId(tied_entity_info->id);
        if (e->falling) 
        {
            tied_entity_info->id = 0;
            continue;
        }

        if (tied_entity_info->direction == NO_DIRECTION)
        {
            // rotation: find rotation from target to current of player, and apply the same rotation to target direction of the entity.
            // TODO: wrap as function?
            float player_target_to_current_angle = getAngleOfYAxisRotation(directionToQuaternion(player->direction), player->rotation);
            Vec4 transform = quaternionFromAxis(intCoordsToNorm(AXIS_Y), player_target_to_current_angle);

            Vec4 base_rotation = IDENTITY_QUATERNION;
            if (getTileTypeFromId(e->id) == MIRROR) base_rotation = mirrorRotation(e->direction, e->mirror_orientation);
            else base_rotation = directionToQuaternion(e->direction);

            e->rotation = quaternionMultiply(transform, base_rotation);

            // follow along with player coords still. this is for the case where player is still moving when this rotation happens.
            e->position.x = player->position.x;
            e->position.z = player->position.z;
        }
        else
        {
            // push
            Entity* root_e = tied_entity_info->root_entity;
            Vec3 difference_in_root_position = vec3Subtract(root_e->position, intCoordsToNorm(root_e->coords));
            float difference_in_root_position_along_direction = getComponentAlongDirection(tied_entity_info->direction, difference_in_root_position);

            if (difference_in_root_position_along_direction != 0)
            {
                Vec3 test_position = vec3AddFloatToVec3AlongDirection(tied_entity_info->direction, difference_in_root_position_along_direction, intCoordsToNorm(e->coords));
                float test_movement_towards_direction = getSignedComponentAlongDirection(tied_entity_info->direction, vec3Subtract(test_position, e->position));

                bool do_standard_entity_move = false;
                if (test_movement_towards_direction > 0) do_standard_entity_move = true; // prevents negative snapping of an object-to-be-pushed towards player
                if (test_movement_towards_direction > 0.5) do_standard_entity_move = false; // prevents too large a jump (happens if pack rotation not done before root_e moves by one tile)
                if (tied_entity_info->tied_to_pack_and_decoupled) do_standard_entity_move = false;

                if (do_standard_entity_move)
                {
                    e->position = test_position;
                    e->velocity = vec3AddFloatToVec3AlongDirection(tied_entity_info->direction, getComponentAlongDirection(tied_entity_info->direction, root_e->velocity), VEC3_0);

                    // apply player rotation too, in case player isn't done rotating when this move happens.
                    float player_target_to_current_angle = getAngleOfYAxisRotation(directionToQuaternion(player->direction), player->rotation);
                    Vec4 transform = quaternionFromAxis(intCoordsToNorm(AXIS_Y), player_target_to_current_angle);

                    Vec4 base_rotation = IDENTITY_QUATERNION;
                    if (getTileTypeFromId(e->id) == MIRROR) base_rotation = mirrorRotation(e->direction, e->mirror_orientation);
                    else base_rotation = directionToQuaternion(e->direction);

                    e->rotation = quaternionMultiply(transform, base_rotation);
                }
                else if (root_e == pack)
                {
                    // here if pack would overshoot, or otherwise misbehave
                    bool close_to_target = fabs(getComponentAlongDirection(tied_entity_info->direction, vec3Subtract(e->position, intCoordsToNorm(e->coords)))) < 0.1;
                    if (test_movement_towards_direction > 0.0 || close_to_target)
                    {
                        tied_entity_info->tied_to_pack_and_decoupled = true;
                        float interpolation_distance_per_frame = 0.1f;
                        float difference = getSignedComponentAlongDirection(tied_entity_info->direction, vec3Subtract(intCoordsToNorm(e->coords), e->position));
                        if (difference < interpolation_distance_per_frame)
                        {
                            e->position = intCoordsToNorm(e->coords);
                            e->velocity = VEC3_0;
                            tied_entity_info->id = 0;
                        }
                        else
                        {
                            float sign = (tied_entity_info->direction == NORTH || tied_entity_info->direction == WEST) ? -1.0f : 1.0f;
                            e->position = vec3AddFloatToVec3AlongDirection(tied_entity_info->direction, sign * interpolation_distance_per_frame, e->position);
                            e->velocity = vec3AddFloatToVec3AlongDirection(tied_entity_info->direction, sign * interpolation_distance_per_frame, VEC3_0);
                        }
                    }
                }
                else if (test_movement_towards_direction < -0.5)
                {
                    // case where object should keep moving, but is offset by one unit because root entity has changed coords, but object on head / on stack will stop here, so isn't pushed by pushAll, but should still continue to end coords
                    test_position = vec3AddFloatToVec3AlongDirection(tied_entity_info->direction, 1, test_position);
                    if (getComponentAlongDirection(tied_entity_info->direction, vec3Subtract(test_position, intCoordsToNorm(e->coords))) < 0.0)
                    {
                        e->position = test_position;
                        e->velocity = vec3AddFloatToVec3AlongDirection(tied_entity_info->direction, getComponentAlongDirection(tied_entity_info->direction, root_e->velocity), VEC3_0);
                    }
                    else
                    {
                        e->position = intCoordsToNorm(e->coords);
                        e->velocity = VEC3_0;
                        tied_entity_info->id = 0;
                    }
                }
            }
            else 
            {
                e->position = intCoordsToNorm(e->coords);
                e->velocity = VEC3_0;
            }

            bool clear_entity_from_tied_to_movement = false;
            if (vec3IsEqual(e->position, intCoordsToNorm(e->coords))) clear_entity_from_tied_to_movement = true;
            if (tied_entity_info->on_head) clear_entity_from_tied_to_movement = false; // case already handled above
            if (clear_entity_from_tied_to_movement) tied_entity_info->id = 0;
        }
    }

    // decrement and clear trailing hitboxes 
    FOR(th_index, MAX_TRAILING_HITBOX_COUNT) 
    {
        TrailingHitbox* th = &temp_state.trailing_hitboxes[th_index];
        if (th->frames > 0) th->frames--;
        if (th->frames == 0) memset(&temp_state.trailing_hitboxes[th_index], 0, sizeof(TrailingHitbox));
    }
}

void gameFrame(double delta_time, Input* input)
{   
    if (delta_time > 0.1) delta_time = 0.1;
    physics_accumulator += delta_time;

    draw_command_count = 0;

    // generate keys_pressed from prev_input and input
    input->keys_pressed = input->keys_held & ~prev_input.keys_held;
    prev_input = *input; // note that prev_input is almost always the same as input, it just persists over the frame

    Entity* player = &world_state.player;
    Entity* pack = &world_state.pack;

    //////////////////
    // CAMERA INPUT //
    //////////////////

    // camera mouse input
    if (editor_state.editor_mode != NO_MODE)
    {
        camera.yaw += input->mouse_dx * CAMERA_SENSITIVITY;
        if (camera.yaw >  0.5f * TAU) camera.yaw -= TAU; 
        if (camera.yaw < -0.5f * TAU) camera.yaw += TAU; 
        camera.pitch += input->mouse_dy * CAMERA_SENSITIVITY;
        float pitch_limit = 0.25f * TAU;
        if (camera.pitch >  pitch_limit) camera.pitch =  pitch_limit; 
        if (camera.pitch < -pitch_limit) camera.pitch = -pitch_limit; 
        camera.rotation = buildCameraQuaternion(camera);
    }
    // camera movement
    if (editor_state.editor_mode != NO_MODE && editor_state.editor_mode != SELECT_WRITE)
    {
        Vec3 right_camera_basis, forward_camera_basis;
        cameraBasisFromYaw(camera.yaw, &right_camera_basis, &forward_camera_basis);

        if (editor_state.editor_mode == PLACE_BREAK || editor_state.editor_mode == SELECT)
        {
            if (input->keys_held & KEY_W) 
            {
                camera.coords.x += forward_camera_basis.x * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
                camera.coords.z += forward_camera_basis.z * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            }
            if (input->keys_held & KEY_A) 
            {
                camera.coords.x -= right_camera_basis.x * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
                camera.coords.z -= right_camera_basis.z * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            }
            if (input->keys_held & KEY_S) 
            {
                camera.coords.x -= forward_camera_basis.x * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
                camera.coords.z -= forward_camera_basis.z * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            }
            if (input->keys_held & KEY_D) 
            {
                camera.coords.x += right_camera_basis.x * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
                camera.coords.z += right_camera_basis.z * CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            }
            if (input->keys_held & KEY_SPACE) camera.coords.y += CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
            if (input->keys_held & KEY_SHIFT) camera.coords.y -= CAMERA_MOVE_STEP * (float)delta_time * 60.0f;
        }
    }

    // alternative camera: switch modes on tab. defined as meta input, so can move player at same time as tab camera change.
    if (input->keys_held & KEY_TAB && time_until_allow_meta_input == 0 && editor_state.editor_mode == NO_MODE) 
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

    ////////////
    // EDITOR //
    ////////////

    // handle text input first
    if (editor_state.editor_mode == SELECT_WRITE)
    {
        char (*writing_to_field)[64] = 0;
        Entity* e = getEntityFromId(editor_state.selected_id);
        if      (editor_state.writing_field == WRITING_FIELD_NEXT_LEVEL)  writing_to_field = &e->next_level;
        else if (editor_state.writing_field == WRITING_FIELD_UNLOCKED_BY) writing_to_field = &e->unlocked_by;

        if (input->keys_pressed & KEY_ENTER)
        {
            memset(*writing_to_field, 0, sizeof(*writing_to_field));
            memcpy(*writing_to_field, editor_state.edit_buffer.string, sizeof(*writing_to_field) - 1);

            editor_state.editor_mode = SELECT;
            editor_state.selected_id = 0;
            editor_state.writing_field = NO_WRITING_FIELD;
        }
        else if (input->keys_held & KEY_ESCAPE)
        {
            editor_state.editor_mode = SELECT;
            editor_state.selected_id = 0;
            editor_state.writing_field = NO_WRITING_FIELD;
        }

        updateTextInput(input);
    }
    else
    {
        memset(&editor_state.edit_buffer, 0, sizeof(editor_state.edit_buffer));
    }

    // MAIN EDITOR MODE FUNCTIONALITY (mostly anything to do with the raycasts)

    if (time_until_allow_meta_input == 0)
    {
        if (editor_state.editor_mode == PLACE_BREAK)
        {
            if (input->keys_held & KEY_LEFT_MOUSE || input->keys_held & KEY_RIGHT_MOUSE || input->keys_held & KEY_MIDDLE_MOUSE || input->keys_held & KEY_R || input->keys_held & KEY_F|| input->keys_held & KEY_H|| input->keys_held & KEY_G)
            {
                Vec3 neg_z_basis = {0, 0, -1};
                RaycastHit raycast_output = raycastHitCube(camera_with_ow_offset.coords, vec3RotateByQuaternion(neg_z_basis, camera_with_ow_offset.rotation), MAX_RAYCAST_SEEK_LENGTH);

                if ((input->keys_held & KEY_LEFT_MOUSE || input->keys_held & KEY_F) && raycast_output.hit) 
                {
                    Entity *entity= getEntityAtCoords(raycast_output.hit_coords);
                    if (entity != 0)
                    {
                        entity->coords = (Int3){0};
                        entity->position = (Vec3){0};
                        entity->removed = true;
                    }
                    setTileType(NONE, raycast_output.hit_coords);
                    setTileDirection(NORTH, raycast_output.hit_coords, 0);
                }
                else if ((input->keys_held & KEY_RIGHT_MOUSE || input->keys_held & KEY_H) && raycast_output.hit) 
                {
                    if (intCoordsWithinLevelBounds(raycast_output.place_coords))
                    {
                        if (editor_state.picked_tile == PLAYER) editorPlaceOnlyInstanceOfTile(player, raycast_output.place_coords, PLAYER, PLAYER_ID);
                        else if (editor_state.picked_tile == PACK) editorPlaceOnlyInstanceOfTile(pack, raycast_output.place_coords, PACK, PACK_ID);
                        if (isSource(editor_state.picked_tile)) 
                        {
                            setTileType(editor_state.picked_tile, raycast_output.place_coords); 
                            setTileDirection(editor_state.picked_direction, raycast_output.place_coords, 0); // TODO: is picked_direction even ever used?
                            setEntityInstanceInGroup(world_state.sources, raycast_output.place_coords, NORTH, getEntityColor(raycast_output.place_coords)); 
                        }
                        else
                        {
                            setTileType(editor_state.picked_tile, raycast_output.place_coords);

                            Entity* entity_group = 0;
                            switch (editor_state.picked_tile)
                            {
                                case BOX:          entity_group = world_state.boxes;          break;
                                case MIRROR:       entity_group = world_state.mirrors;        break;
                                case GLASS:        entity_group = world_state.glass_blocks;  break;
                                case WIN_BLOCK:    entity_group = world_state.win_blocks;    break;
                                case LOCKED_BLOCK: entity_group = world_state.locked_blocks; break;
                                default: entity_group = 0;
                            }
                            if (entity_group != 0) 
                            {
                                setEntityInstanceInGroup(entity_group, raycast_output.place_coords, NORTH, NO_COLOR);
                                setTileDirection(editor_state.picked_direction, raycast_output.place_coords, 0);
                            }
                            else 
                            {
                                setTileDirection(NORTH, raycast_output.place_coords, 0);
                            }
                        }
                    }
                }
                else if (input->keys_held & KEY_R && raycast_output.hit)
                {   
                    TileType type = getTileType(raycast_output.hit_coords);
                    if (type == MIRROR)
                    {
                        Entity* mirror = getEntityAtCoords(raycast_output.hit_coords);
                        if (mirror)
                        {
                            mirror->direction++;
                            if (mirror->direction >= UP)
                            {
                                mirror->direction = NORTH;
                                mirror->mirror_orientation++;
                                if (mirror->mirror_orientation > MIRROR_DOWN) mirror->mirror_orientation = MIRROR_SIDE;
                            }
                            setTileDirection(mirror->direction, raycast_output.hit_coords, mirror->mirror_orientation);
                            mirror->rotation = mirrorRotation(mirror->direction, mirror->mirror_orientation);
                        }
                    }
                    else if (isEntity(type))
                    {
                        Direction direction = getTileDirection(raycast_output.hit_coords);
                        if (direction == DOWN) direction = NORTH;
                        else direction++;
                        setTileDirection(direction, raycast_output.hit_coords, 0); // mirror rotation case is handled later
                        Entity* e = getEntityAtCoords(raycast_output.hit_coords);
                        if (e != 0)
                        {
                            e->direction = direction;
                            e->rotation = directionToQuaternion(direction);
                        }
                    }
                    else if (type == LADDER)
                    {
                        Direction direction = getTileDirection(raycast_output.hit_coords);
                        if (direction == EAST) direction = NORTH;
                        else direction++;
                        setTileDirection(direction, raycast_output.hit_coords, 0);
                    }
                }
                else if ((input->keys_held & KEY_MIDDLE_MOUSE || input->keys_held & KEY_G) && raycast_output.hit) editor_state.picked_tile = getTileType(raycast_output.hit_coords);

                time_until_allow_meta_input = PLACE_BREAK_TIME_UNTIL_ALLOW_INPUT;
            }
            if (input->keys_held & KEY_L)
            {
                editor_state.picked_tile++;
                if (editor_state.picked_tile == LADDER + 1) editor_state.picked_tile = VOID;
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
            if (input->keys_held & KEY_M)
            {
                clearSolvedLevels();
                createDebugPopup("solved levels cleared", NO_TYPE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
        }

        if (editor_state.editor_mode == SELECT)
        {
            if (input->keys_held & KEY_LEFT_MOUSE)
            {
                Vec3 neg_z_basis = {0, 0, -1};
                RaycastHit raycast_output = raycastHitCube(camera_with_ow_offset.coords, vec3RotateByQuaternion(neg_z_basis, camera_with_ow_offset.rotation), MAX_RAYCAST_SEEK_LENGTH);

                if (isEntity(getTileType(raycast_output.hit_coords)))
                {
                    editor_state.selected_id = getEntityAtCoords(raycast_output.hit_coords)->id;
                    editor_state.selected_coords = raycast_output.hit_coords;
                }
                else
                {
                    editor_state.selected_id = -1;
                }
            }

            else if (editor_state.selected_id > 0)
            {
                // start writing next level
                if (input->keys_held & KEY_L) 
                {
                    editor_state.editor_mode = SELECT_WRITE;
                    editor_state.writing_field = WRITING_FIELD_NEXT_LEVEL;
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                }
                // start writing unlocked by
                else if (input->keys_held & KEY_B)
                {
                    editor_state.editor_mode = SELECT_WRITE;
                    editor_state.writing_field = WRITING_FIELD_UNLOCKED_BY;
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;

                }
                // go to selected level of block on q press
                else if (input->keys_held & KEY_Q && editor_state.selected_id / ID_OFFSET_WIN_BLOCK * ID_OFFSET_WIN_BLOCK == ID_OFFSET_WIN_BLOCK)
                {
                    Entity* wb = getEntityFromId(editor_state.selected_id);
                    if (wb->next_level[0] != 0)
                    {
                        levelChangePrep(wb->next_level);
                        gameInitializeState(wb->next_level);
                        writeSolvedLevelsToFile();
                        time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                    }
                }
            }
        }
    }

    // EDITOR KEYBINDS - separate check for time_until_allow_meta_input because might have been modified above, and if so want to skip this
    if (time_until_allow_meta_input == 0 && editor_state.editor_mode != SELECT_WRITE)
    {
        // editor mode toggle
        if (input->keys_held & KEY_0) 
        {
            editor_state.editor_mode = NO_MODE;
            createDebugPopup("game mode", GAMEPLAY_MODE_CHANGE);
        }
        if (input->keys_held & KEY_1) 
        {
            editor_state.editor_mode = PLACE_BREAK;
            createDebugPopup("place / break mode", GAMEPLAY_MODE_CHANGE);
        }
        if (input->keys_held & KEY_2) 
        {
            editor_state.editor_mode = SELECT;
            createDebugPopup("select mode", GAMEPLAY_MODE_CHANGE);
        }

        // toggle cheating
        if (input->keys_held & KEY_3)
        {
            cheating = !cheating;
            if (cheating) createDebugPopup("cheating", CHEAT_MODE_TOGGLE);
            else createDebugPopup("not cheating", CHEAT_MODE_TOGGLE);
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }

        // toggle drawing trailing hitboxes as cube outlines
        if (input->keys_held & KEY_O)
        {
            draw_trailing_hitboxes = !draw_trailing_hitboxes;
            if (draw_trailing_hitboxes) createDebugPopup("showing trailing hitboxes", DRAW_TRAILING_HITBOX_TOGGLE);
            else createDebugPopup("not showing trailing hitboxes", DRAW_TRAILING_HITBOX_TOGGLE);
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }

        // change model states
        if (input->keys_held & KEY_7)
        {
            game_shader_mode = OUTLINE_TEST;
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            createDebugPopup("shader mode: testing outlines", SHADER_MODE_CHANGE);
        }
        if (input->keys_held & KEY_8)
        {
            game_shader_mode = OUTLINE;
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            createDebugPopup("shader mode: outlines", SHADER_MODE_CHANGE);
        }
        if (input->keys_held & KEY_9)
        {
            game_shader_mode = OLD; 
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;


            createDebugPopup("shader mode: old", SHADER_MODE_CHANGE);
        }

        // change camera fov for editor
        if (input->keys_held & KEY_N && editor_state.editor_mode != NO_MODE)
        {
            camera.fov--;
            time_until_allow_meta_input = 4;
        }
        else if (input->keys_held & KEY_B && editor_state.editor_mode != NO_MODE)
        {
            camera.fov++;
            time_until_allow_meta_input = 4;
        }

        // set camera fov to wide for editor
        if (input->keys_held & KEY_J)
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

        // snap camera yaw to nearest axis
        if (input->keys_held & KEY_P)
        {
            float camera_snap_yaw = 0;
            if      (camera.yaw >= TAU * -0.375f && camera.yaw < TAU * -0.125f) camera_snap_yaw = TAU * -0.25f;
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

        // speed up / slow down physics tick
        if (input->keys_held & KEY_DOT)
        {
            char timestep_text[256] = {0};
            if (physics_timestep_multiplier > 1.0)
            {
                physics_timestep_multiplier /= 2;
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                snprintf(timestep_text, sizeof(timestep_text), "physics timestep increased (%f)", physics_timestep_multiplier * DEFAULT_PHYSICS_TIMESTEP);
                createDebugPopup(timestep_text, PHYSICS_TIMESTEP_CHANGE);
            }
            else
            {
                createDebugPopup("physics timestep already at minimum!", PHYSICS_TIMESTEP_CHANGE);
            }
        }
        else if (input->keys_held & KEY_COMMA)
        {
            physics_timestep_multiplier *= 2;
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            char timestep_text[256] = {0};
            snprintf(timestep_text, sizeof(timestep_text), "physics timestep decreased (%f)", physics_timestep_multiplier * DEFAULT_PHYSICS_TIMESTEP);
            createDebugPopup(timestep_text, PHYSICS_TIMESTEP_CHANGE);
        }

        if (input->keys_held & KEY_BACKSPACE)
        {
            camera = saved_main_camera;
            camera.rotation = buildCameraQuaternion(camera);
            camera_mode = MAIN_WAITING;
            camera_lerp_t = 0.0f;
            createDebugPopup("returned camera to saved position", NO_TYPE);
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }

        // toggle debug press
        if (input->keys_held & KEY_Y)
        {
            do_debug_text = !do_debug_text;
            if (do_debug_text) createDebugPopup("debug state visibility on", DEBUG_STATE_VISIBILITY_CHANGE);
            else               createDebugPopup("debug state visibility off", DEBUG_STATE_VISIBILITY_CHANGE);
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }

        // TODO: temp for level setup change
        if (input->keys_held & KEY_5)
        {
            for (int buffer_index = 0; buffer_index < 2 * level_dim.x*level_dim.y*level_dim.z; buffer_index += 2)
            {
                if (world_state.buffer[buffer_index] == WIN_BLOCK)
                {
                    world_state.buffer[buffer_index] = WATER;
                    world_state.buffer[buffer_index + 1] = NORTH;
                }
            }
            memset(&world_state.win_blocks, 0, sizeof(world_state.win_blocks));
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }
    }

    ///////////////////////
    // MAIN PHYSICS LOOP //
    ///////////////////////

    while (physics_accumulator >= (physics_timestep_multiplier * DEFAULT_PHYSICS_TIMESTEP))
    {
        debug_text_count = 0;

        if (editor_state.editor_mode == NO_MODE)
        {
            // assuming gameplay undo and restart (still no undo in editor)
            bool do_undo = false;
            if (input->keys_held & KEY_Z && time_until_allow_undo_or_restart_input == 0) do_undo = true; // allow if held, and time allows
            if (input->keys_pressed & KEY_Z) do_undo = true; // allow if pressed this frame, regardless of time

            if (do_undo)
            {
                // UNDO
                if (performUndo())
                {
                    updatePackDetached();

                    if (undos_performed == 0) time_until_allow_undo_or_restart_input = 9;
                    else if (undos_performed < 2) time_until_allow_undo_or_restart_input = 8;
                    else if (undos_performed < 6) time_until_allow_undo_or_restart_input = 7;
                    else time_until_allow_undo_or_restart_input = 6;

                    undos_performed++;
                    silence_unlocks_due_to_restart_or_undo = true;
                    undo_press_timer = TIME_AFTER_UNDO_UNTIL_PHYSICS_START;
                    temp_state.allow_movement_timer = TIME_AFTER_UNDO_UNTIL_PHYSICS_START;
                }
                else
                {
                    time_until_allow_undo_or_restart_input = 8;
                }

                // TODO: maybe stop gameplay input for a few frames after undo?
            }
            if (time_until_allow_undo_or_restart_input == 0 && input->keys_held & KEY_R)
            {
                // RESTART 
                if (!restart_last_turn) 
                {
                    recordActionForUndo(&world_state, true, false);
                }
                createDebugPopup("level restarted", NO_TYPE);
                // TODO(animations): clear all animations
                Camera save_camera = camera;

                gameInitializeState(world_state.level_name);

                if (temp_state.in_overworld)
                {
                    // copy world state from overworld_zero, but save the solved levels and overwrite the level name
                    char persist_solved_levels[64][64];
                    memcpy(&persist_solved_levels, &world_state.solved_levels, sizeof(char) * 64 * 64);
                    memcpy(&world_state, &overworld_zero_state, sizeof(WorldState));
                    memcpy(&world_state.solved_levels, &persist_solved_levels, sizeof(char) * 64 * 64);
                    memcpy(&world_state.level_name, "overworld", sizeof(char) * 64);

                    moveEntityInBufferAndState(player, temp_state.restart_position, NORTH);
                    setEntityVecsFromInts(player);
                    moveEntityInBufferAndState(pack, getNextCoords(player->coords, SOUTH), NORTH);
                    setEntityVecsFromInts(pack);
                }
                camera = save_camera; 
                restart_last_turn = true;
                silence_unlocks_due_to_restart_or_undo = true;
                time_until_allow_undo_or_restart_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                temp_state.allow_movement_timer = 0;

                updateLaserBuffer();
            }
            if (time_until_allow_meta_input == 0 && input->keys_held & KEY_ESCAPE && !temp_state.in_overworld)
            {
                // leave current level if not in overworld. TODO: why is saving solved levels required here?
                char save_solved_levels[64][64] = {0};
                memcpy(save_solved_levels, world_state.solved_levels, sizeof(save_solved_levels));
                levelChangePrep("overworld");
                gameInitializeState("overworld");
                memcpy(world_state.solved_levels, save_solved_levels, sizeof(save_solved_levels));
                writeSolvedLevelsToFile();
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                temp_state.allow_movement_timer = 0;
            }

            if (temp_state.allow_movement_timer == 0 && (input->keys_held & KEY_W || input->keys_held & KEY_A || input->keys_held & KEY_S || input->keys_held & KEY_D))
            {
                // MOVEMENT 
                Direction input_direction = 0;
                if      (input->keys_held & KEY_W) input_direction = NORTH; 
                else if (input->keys_held & KEY_A) input_direction = WEST; 
                else if (input->keys_held & KEY_S) input_direction = SOUTH; 
                else if (input->keys_held & KEY_D) input_direction = EAST; 

                if (input_direction == player->direction)
                {
                    // FORWARD MOVEMENT
                    Int3 next_player_coords = getNextCoords(player->coords, input_direction);

                    bool allow_input = false;

                    // allow movement if, given acceleration this frame along input direction, we would overshoot.
                    float sign = input_direction == NORTH || input_direction == WEST ? -1.0f : 1.0f;
                    float speculative_velocity_along_direction = calculateSpeculativeVelocityAlongDirection(input_direction, sign);
                    float position_along_direction = getComponentAlongDirection(input_direction, player->position);
                    float coords_along_direction = getComponentAlongDirection(input_direction, intCoordsToNorm(player->coords));
                    if (wouldOvershoot(speculative_velocity_along_direction, position_along_direction, coords_along_direction, sign)) allow_input = true;

                    // get abs(angle) of player current quat -> target quat, and gate on some angle threshold here.
                    float difference_in_player_angle = getAngleOfYAxisRotation(player->rotation, directionToQuaternion(player->direction));
                    if (fabs(difference_in_player_angle) > TAU * 0.25 * 0.2) allow_input = false;

                    // disallow movement if able to fall
                    if (!temp_state.player_hit_by_red && canFall(player)) allow_input = false;
                    // also disallow movement if also moving in some other direction currently - probably just guards against moving while falling
                    if (!vec3IsZero(vec3SetComponentAlongDirection(input_direction, player->velocity, 0))) allow_input = false;

                    if (allow_input)
                    {
                        bool try_walk = false;
                        bool do_push = false;
                        bool try_climb = false;

                        TileType next_tile = getTileType(next_player_coords);
                        switch (next_tile)
                        {
                            case NONE:
                            {
                                // currently not allowing input if trailing hitbox occupies next tile. this check might be too strict sometimes
                                TrailingHitbox th = {0};
                                if (trailingHitboxAtCoords(next_player_coords, &th)) try_walk = false;
                                else try_walk = true;
                                break;
                            }
                            case BOX:
                            case GLASS:
                            case PACK:
                            case MIRROR:
                            case SOURCE_RED:
                            case SOURCE_BLUE:
                            case SOURCE_MAGENTA:
                            {
                                // figure out if push, pause, or fail here.
                                if (canPush(next_player_coords, input_direction))
                                {
                                    do_push = true;
                                    try_walk = true;
                                }
                                break;
                            }
                            case LADDER:
                            {
                                // if ladder is facing towards the player, do the climb
                                if (getTileDirection(next_player_coords) == oppositeDirection(player->direction)) try_climb = true;
                                else { /* TODO(anims): failed animations case */ }
                                break;
                            }
                            default:
                            {
                                // TODO(anims): failed animations case
                                break;
                            }
                        }
                        if (try_walk)
                        {
                            // don't allow walking off edge
                            Int3 coords_below_next_coords = getNextCoords(next_player_coords, DOWN);
                            TileType tile_below_next_coords = getTileType(coords_below_next_coords);

                            // basic check for if walk will be allowed
                            bool allow_walk = true;
                            if (tile_below_next_coords == NONE && !temp_state.player_hit_by_red) allow_walk = false;
                            if (isEntity(tile_below_next_coords) && !vec3IsZero(getEntityAtCoords(coords_below_next_coords)->velocity)) allow_walk = false;

                            if (allow_walk || cheating)
                            {
                                recordActionForUndo(&world_state, false, false);
                                if (do_push) pushAll(next_player_coords, input_direction, false, player);
                                doStandardMovement(input_direction, next_player_coords);
                            }
                            else
                            {
                                // leap of faith logic
                                
                                // create snapshot of current world state
                                memcpy(&leap_of_faith_world_state_snapshot, &world_state, sizeof(WorldState));
                                memcpy(&leap_of_faith_temp_state_snapshot,  &temp_state,  sizeof(TemporaryState));
                                Input input_snapshot = *input;

                                // commit tentative move
                                if (do_push) pushAll(next_player_coords, input_direction, false, player);
                                doStandardMovement(input_direction, next_player_coords);

                                memset(input, 0, sizeof(Input));

                                // simulate forward, and check if red
                                bool would_be_red = false;
                                FOR(_, 8)
                                {
                                    doPhysicsTick();
                                    updateLaserBuffer();
                                    if (temp_state.player_hit_by_red)
                                    {
                                        would_be_red = true;
                                        break;
                                    }
                                }

                                // restore everything
                                memcpy(&world_state, &leap_of_faith_world_state_snapshot, sizeof(WorldState));
                                memcpy(&temp_state,  &leap_of_faith_temp_state_snapshot,  sizeof(TemporaryState));
                                *input = input_snapshot;

                                // if became red, perform move
                                if (would_be_red)
                                {
                                    recordActionForUndo(&world_state, false, false);
                                    if (do_push) pushAll(next_player_coords, input_direction, false, player);
                                    doStandardMovement(input_direction, next_player_coords);
                                }
                            }
                        }
                        else if (try_climb)
                        {
                            // only handles setting climbing direction to UP if player wants to climb up. everything else is handled later, because we want to keep climbing sometimes, even if there's been no input for it.
                            bool do_climb = false; 
                            Int3 coords_above_player = getNextCoords(player->coords, UP);
                            TileType type_above_player = getTileType(coords_above_player);
                            if (type_above_player == NONE) do_climb = true;
                            //else if (isPushable(type_above_player) && canPushUp(coords_above_player) do_climb = true; // TODO: handle push up case (need to reimplement these functions)

                            if (do_climb)
                            {
                                player->climbing_direction = UP;
                            }
                        }
                    }
                }
                else if (input_direction != oppositeDirection(player->direction))
                {
                    // TURN MOVEMENT
                    bool allow_turn = true;
                    if (player->falling) allow_turn = false;

                    // get abs(angle) of player current quat -> target quat, and gate on some angle threshold here.
                    float difference_in_player_angle = getAngleOfYAxisRotation(player->rotation, directionToQuaternion(player->direction));
                    if (fabs(difference_in_player_angle) > TAU * 0.25 * 0.2) allow_turn = false;

                    // get difference in position along axis of travel, and gate on some threshold to target
                    float difference_in_player_position_along_direction = getComponentAlongDirection(player->direction, vec3Subtract(player->position, intCoordsToNorm(player->coords)));
                    if (fabs(difference_in_player_position_along_direction) > 0.2) allow_turn = false;

                    if (allow_turn)
                    {
                        recordActionForUndo(&world_state, false, false);

                        Direction initial_player_direction = player->direction;
                        player->direction = input_direction;
                        setTileDirection(player->direction, player->coords, 0);

                        if (temp_state.pack_attached)
                        {
                            temp_state.pack_turn_state.pack_intermediate_states_timer = TURN_TIME;
                            temp_state.pack_turn_state.pack_intermediate_coords = getNextCoords(pack->coords, oppositeDirection(input_direction));
                            temp_state.pack_turn_state.initial_player_direction = initial_player_direction;
                        }

                        // if not blue, rotate objects stacked above the player
                        if (!temp_state.player_hit_by_blue)
                        {
                            Int3 coords_above = getNextCoords(player->coords, UP);
                            TileType type_above = getTileType(coords_above);

                            int32 stack_size = 0;
                            if (isPushable(type_above)) stack_size = getPushableStackSize(coords_above);

                            // need to add either 1 or -1 to direction of entity being rotated
                            if (stack_size > 0);
                            {
                                int32 direction_add = (4 + player->direction - initial_player_direction) % 4;

                                Int3 current_coords = coords_above;
                                FOR(stack_index, stack_size)
                                {
                                    Entity* e = getEntityAtCoords(current_coords);

                                    e->direction = (e->direction + direction_add) % 4;

                                    int32 write_index = getWriteIndexInTiedEntities(e->id);
                                    temp_state.entities_tied_to_movement[write_index].id = e->id;
                                    temp_state.entities_tied_to_movement[write_index].direction = NO_DIRECTION;
                                    temp_state.entities_tied_to_movement[write_index].on_head = true;
                                    temp_state.entities_tied_to_movement[write_index].root_entity = player;
                                    temp_state.entities_tied_to_movement[write_index].tied_to_pack_and_decoupled = false;

                                    current_coords = getNextCoords(current_coords, UP);
                                }
                            }
                        }
                    }
                }
                else if (input_direction == oppositeDirection(player->direction))
                {
                    // TODO: backwards movement off the edge of a ladder, or else a failed animation
                }
            }
        }
        
        // handle all physics that doesn't have to do with player input on this frame
        doPhysicsTick();

        // win block logic
        if (getTileType(getNextCoords(player->coords, DOWN)) == WIN_BLOCK)
        {
            if (input->keys_held & KEY_Q && time_until_allow_meta_input == 0)
            {
                // go to win_block.next_level if conditions are met
                Entity* wb = getEntityAtCoords(getNextCoords(player->coords, DOWN));
                bool do_win_block_usage = true;
                if (editor_state.editor_mode != NO_MODE) do_win_block_usage = false;
                if (!temp_state.pack_attached) do_win_block_usage = false;
                if (wb->locked) do_win_block_usage = false;
                if (wb->next_level[0] == 0) do_win_block_usage = false; // don't go through if there is no next level here yet

                if (do_win_block_usage)
                {
                    if (temp_state.in_overworld) 
                    {
                        char level_path[64] = {0};
                        buildLevelPathFromName(world_state.level_name, &level_path, false);
                        saveLevelRewrite(level_path);
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
                    // TODO(anims): zero animations of player and pack. probably should clear all animations, period?
                    levelChangePrep(wb->next_level);
                    gameInitializeState(wb->next_level);
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                }
            }
            else if (input->keys_held & KEY_F && time_until_allow_meta_input == 0)
            {
                // add win_block.next_level to solved_levels. this is a debug bind
                bool solve_level = true;
                if (editor_state.editor_mode != NO_MODE) solve_level = false;

                if (solve_level)
                {
                    Entity* wb = getEntityAtCoords(getNextCoords(player->coords, DOWN));
                    if (findInSolvedLevels(wb->next_level) == -1)
                    {
                        int32 next_free = nextFreeInSolvedLevels(&world_state.solved_levels);
                        strcpy(world_state.solved_levels[next_free], wb->next_level);
                    }
                    writeSolvedLevelsToFile();
                    createDebugPopup("level solved!", NO_TYPE);
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                }
            }
        }

        // locked block logic
        // TODO: currently iterating this every frame on every entity, which is pretty wasteful. should instead just change this if some action that could impact locked-ness happened that frame.
        Entity* entity_group[4] = { world_state.boxes, world_state.mirrors, world_state.win_blocks, world_state.sources };
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
            Entity* lb = &world_state.locked_blocks[locked_block_index];
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
                    setTileDirection(NORTH, lb->coords, 0);
                }
                if (!silence_unlocks_due_to_restart_or_undo) createDebugPopup("something was unlocked!", NO_TYPE);
            }
            else if (find_result == -1 && lb->removed)
            {
                lb->removed = false;
                setTileType(LOCKED_BLOCK, lb->coords);
                setTileDirection(NORTH, lb->coords, 0);
            }
        }

        // MISC STUFF

        // disallow input if player above void / water
        TileType tile_type_below_player = getTileType(getNextCoords(player->coords, DOWN));
        if (tile_type_below_player == VOID || tile_type_below_player == WATER) temp_state.allow_movement_timer = -1;

        // reset undos performed if no longer holding z undos
        if (undos_performed > 0 && !input->keys_held & KEY_Z) undos_performed = 0;

        // decrement undo timer if > 0. the timer value means that even a few frames after releasing undo, gravity isn't applied.
        if (undo_press_timer > 0) undo_press_timer--;

        // decrement allow movement timer, if movement should be disabled for a few frames
        if (temp_state.allow_movement_timer > 0) temp_state.allow_movement_timer--;

        // TODO: TEMP: disallow any input when climbing direction isn't no dir. probably want to do something like this that's a bit more intelligent. also probably want to be able to change climbing direction sometimes
        if (player->climbing_direction != NO_DIRECTION) temp_state.allow_movement_timer = 1;

        // all mirrors with direction >=UP are moved to the next orientation with direction NORTH.
        FOR(mirror_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* mirror = &world_state.mirrors[mirror_index];
            if (mirror->direction >= 4) 
            {
                mirror->direction -= 4;
                mirror->mirror_orientation += 1;
                if (mirror->mirror_orientation > MIRROR_DOWN) mirror->mirror_orientation = MIRROR_SIDE;
                setTileDirection(mirror->direction, mirror->coords, mirror->mirror_orientation); // keep buffer in sync
            }
        }

        // update overworld player coords for camera offset if player not removed. if player is removed, these coords persist, so that camera doesn't jump wildly when changing player pos in editor
        if (temp_state.in_overworld)
        {
            if (!player->removed) ow_player_coords_for_offset = player->coords;
        }
        else 
        {
            ow_player_coords_for_offset = INT3_0;
        }

        // update restart coords based on current coords of the player, and also update game progress if this is relevant
        if (player->coords.z > 204) 
        {
            temp_state.restart_position = (Int3){ 58, 2, 225 };
        }
        else if (player->coords.z > 189)
        {
            temp_state.restart_position = (Int3){ 58, 2, 200 };
            if (temp_state.game_progress < WORLD_1) temp_state.game_progress = WORLD_1;
        }
        else if (player->coords.z > 174) 
        {
            temp_state.restart_position = (Int3){ 58, 2, 188 };
        }
        else
        {
            temp_state.restart_position = (Int3){ 58, 2, 170 };
            if (temp_state.game_progress < WORLD_2) temp_state.game_progress = WORLD_2;
        }

        // create debug texts
        if (do_debug_text)
        {
            // display level name
            createDebugText(world_state.level_name);

            // game progress info
            char game_text[256] = {0};
            snprintf(game_text, sizeof(game_text), "game progress: %d", temp_state.game_progress);
            createDebugText(game_text);

            // player info
            char player_text[256] = {0};
            snprintf(player_text, sizeof(player_text), "player info: coords: %i, %i, %i, pos norm: %f, %f, %f, velocity: %f, %f, %f", player->coords.x, player->coords.y, player->coords.z, player->position.x, player->position.y, player->position.z, player->velocity.x, player->velocity.y, player->velocity.z);
            createDebugText(player_text);

            // mirror info
            char mirror_text[256] = {0};
            Entity m = world_state.mirrors[0];
            snprintf(mirror_text, sizeof(mirror_text), "mirror: coords: %i, %i, %i; direction: %i; orientation: %i", m.coords.x, m.coords.y, m.coords.z, m.direction, m.mirror_orientation);
            createDebugText(mirror_text);

            // entities tied to player movement
            char tied_info[256] = {0};
            TiedEntity te_1 = temp_state.entities_tied_to_movement[0];
            TiedEntity te_2 = temp_state.entities_tied_to_movement[1];
            snprintf(tied_info, sizeof(tied_info), "tied entity 1: id: %i, dir: %i, on_head: %i, tied entity 2: id: %i, dir: %i, on_head: %i", te_1.id, te_1.direction, te_1.on_head, te_2.id, te_2.direction, te_2.on_head);
            createDebugText(tied_info);

            char timer_info[256] = {0};
            snprintf(timer_info, sizeof(timer_info), "meta: %i, undo/restart: %i", time_until_allow_meta_input, time_until_allow_undo_or_restart_input);
            createDebugText(timer_info);

            // show current selected id + coords
            char edit_text[256] = {0};
            snprintf(edit_text, sizeof(edit_text), "selected id: %d", editor_state.selected_id);
            createDebugText(edit_text);

            char th_text[256] = {0};
            int32 th_count = 0;
            FOR(th_index, MAX_TRAILING_HITBOX_COUNT) if (temp_state.trailing_hitboxes[th_index].id != 0) th_count++;
            TrailingHitbox th1 = temp_state.trailing_hitboxes[0];
            TrailingHitbox th2 = temp_state.trailing_hitboxes[1];
            TrailingHitbox th3 = temp_state.trailing_hitboxes[2];
            snprintf(th_text, sizeof(th_text), "1: %d %d %d; 2: %d %d %d; 3: %d %d %d; count: %i", th1.type, th1.id, th1.frames, th2.type, th2.id, th2.frames, th3.type, th3.id, th3.frames, th_count);
            createDebugText(th_text);

            char climb_text[256] = {0};
            snprintf(climb_text, sizeof(climb_text), "climbing direction: %i", player->climbing_direction);
            createDebugText(climb_text);

            /*
            // debug lasers
            FOR(lb_index, 64)
            {
                LaserBuffer lb = laser_buffer[lb_index];
                if (vec3IsEqual(lb.start_coords, VEC3_0)) continue;
                char lb_text[256] = {0};
                snprintf(lb_text, sizeof(lb_text), "laser start coords: %.2f, %.2f, %.2f, laser end coords: %.2f, %.2f, %.2f", lb.start_coords.x, lb.start_coords.y, lb.start_coords.z, lb.end_coords.x, lb.end_coords.y, lb.end_coords.z);
                if (do_debug_text) createDebugText(lb_text);
            }
            */

            // show undo deltas in buffer
            char undo_buffer_text[256] = {0};
            snprintf(undo_buffer_text, sizeof(undo_buffer_text), "undo deltas in buffer: %d", undo_buffer.delta_count);
            createDebugText(undo_buffer_text);

            // draw selected id info
            if (editor_state.editor_mode == SELECT || editor_state.editor_mode == SELECT_WRITE)
            {
                if (editor_state.selected_id > 0)
                {
                    Entity* e = getEntityFromId(editor_state.selected_id);
                    if (e)
                    {
                        char selected_id_text[256] = {0};
                        snprintf(selected_id_text, sizeof(selected_id_text), "selected id: %d, coords: %d, %d, %d, direction: %i, mirror_orientation: %i", editor_state.selected_id, e->coords.x, e->coords.y, e->coords.z, e->direction, e->mirror_orientation);
                        createDebugText(selected_id_text);

                        char writing_field_text[256] = {0};
                        char writing_field_state[256] = {0};
                        switch (editor_state.writing_field)
                        {
                            case NO_WRITING_FIELD:          memcpy(writing_field_state, "none",         sizeof(writing_field_state)); break;
                            case WRITING_FIELD_NEXT_LEVEL:  memcpy(writing_field_state, "next level",   sizeof(writing_field_state)); break;
                            case WRITING_FIELD_UNLOCKED_BY: memcpy(writing_field_state, "unlocked by",  sizeof(writing_field_state)); break;
                        }
                        snprintf(writing_field_text, sizeof(writing_field_text), "writing_field: %s", writing_field_state); 
                        createDebugText(writing_field_text);

                        char next_level_text[256] = {0};
                        snprintf(next_level_text, sizeof(next_level_text), "next_level: %s", e->next_level);
                        createDebugText(next_level_text);

                        char unlocked_by_text[256] = {0};
                        snprintf(unlocked_by_text, sizeof(unlocked_by_text), "unlocked_by: %s", e->unlocked_by);
                        createDebugText(unlocked_by_text);
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
        physics_accumulator -= physics_timestep_multiplier * DEFAULT_PHYSICS_TIMESTEP;
        if (time_until_allow_undo_or_restart_input > 0) time_until_allow_undo_or_restart_input--;
    }

    // SAVING STUFF (depends on changed state, so after loop)
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
        if (time_until_allow_meta_input == 0 && (editor_state.editor_mode == PLACE_BREAK || editor_state.editor_mode == SELECT) && (input->keys_held & KEY_C || input->keys_held & KEY_V))
        {
            char tag[4] = {0};
            bool write_alt_camera = false;
            if (input->keys_held & KEY_C) 
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

            if (input->keys_held & KEY_C) saved_main_camera = camera;
            else saved_alt_camera = camera;
        }

        if (time_until_allow_meta_input == 0 && editor_state.editor_mode != SELECT_WRITE && input->keys_held & KEY_X) 
        {
            if (saved_alt_camera.fov > 0) // if there is a saved alt camera
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
        }

        // write level to file on i press
        if (time_until_allow_meta_input == 0 && (editor_state.editor_mode == PLACE_BREAK || editor_state.editor_mode == SELECT) && input->keys_held & KEY_I) 
        {
            saveLevelRewrite(level_path);
            saveLevelRewrite(relative_level_path);
            if (temp_state.in_overworld)
            {
                saveLevelRewrite(overworld_zero_path);
                saveLevelRewrite(overworld_zero_relative_path);

                // overwrite overworld_zero's world state with the new saved one
                memcpy(&overworld_zero_state, &world_state, sizeof(WorldState));
            }
            createDebugPopup("level saved", LEVEL_SAVE);
            writeSolvedLevelsToFile();
        }
    }

    // update camera for drawing. after loop because depends on in_overworld
    camera_with_ow_offset = camera;
    if (temp_state.in_overworld)
    {
        Int3 player_delta = int3Subtract(ow_player_coords_for_offset, OVERWORLD_CAMERA_CENTER_START);
        int32 screen_offset_x = 0;
        int32 screen_offset_z = 0;
        if (player_delta.x > 0) screen_offset_x = (player_delta.x + (OVERWORLD_SCREEN_SIZE_X / 2)) / OVERWORLD_SCREEN_SIZE_X;
        else                    screen_offset_x = (player_delta.x - (OVERWORLD_SCREEN_SIZE_X / 2)) / OVERWORLD_SCREEN_SIZE_X;
        if (player_delta.z > 0) screen_offset_z = (player_delta.z + (OVERWORLD_SCREEN_SIZE_Z / 2)) / OVERWORLD_SCREEN_SIZE_Z;
        else                    screen_offset_z = (player_delta.z - (OVERWORLD_SCREEN_SIZE_Z / 2)) / OVERWORLD_SCREEN_SIZE_Z;

        camera_with_ow_offset.coords.x = camera.coords.x + (screen_offset_x * OVERWORLD_SCREEN_SIZE_X);
        camera_with_ow_offset.coords.z = camera.coords.z + (screen_offset_z * OVERWORLD_SCREEN_SIZE_Z);
    }

    /////////////
    // DRAW 3D //
    /////////////

    {
        // draw lasers

        // final update laser buffer call
        updateLaserBuffer();

        FOR(laser_buffer_index, MAX_SOURCE_COUNT * MAX_LASER_TURNS_ALLOWED)
        {
            LaserBuffer lb = laser_buffer[laser_buffer_index];
            if (lb.color == NO_COLOR) continue; // laser buffer is not dense - this is check that there is actually something here

            Vec3 diff = vec3Subtract(lb.end_coords, lb.start_coords);
            Vec3 center = vec3Add(lb.start_coords, vec3ScalarMultiply(diff, 0.5));

            float length = vec3Length(diff);
            Vec3 scale = { LASER_WIDTH, LASER_WIDTH, length };
            Vec4 rotation = directionToQuaternion(lb.direction);

            Vec3 color_without_alpha = {0};
            switch (lb.color)
            {
                case RED:     color_without_alpha = (Vec3){ 1.0f, 0.0f, 0.0f }; break;
                case BLUE:    color_without_alpha = (Vec3){ 0.0f, 0.0f, 1.0f }; break;
                case MAGENTA: color_without_alpha = (Vec3){ 1.0f, 0.0f, 1.0f }; break;
                default: break;
            }
            float alpha = 1.0f;
            Vec4 color_with_alpha = { color_without_alpha.x, color_without_alpha.y, color_without_alpha.z, alpha };

            drawAsset(0, LASER, center, scale, rotation, color_with_alpha, false, lb.start_clip_plane, lb.end_clip_plane); // the model doesnt matter
        }

        // draw most things (not player or pack) TODO: after models can include pack here because can be DEFAULT_SCALE. after actual shaders for the color of the player can also include player here
        for (int tile_index = 0; tile_index < 2 * level_dim.x*level_dim.y*level_dim.z; tile_index += 2)
        {
            TileType draw_tile = world_state.buffer[tile_index];
            if (draw_tile == NONE || draw_tile == PLAYER || draw_tile == PACK) continue;
            if (isEntity(draw_tile))
            {
                Entity* e = getEntityAtCoords(bufferIndexToCoords(tile_index));
                if (e) // TODO: guard is here while i translate over some levels, since i offset all of the enums, but the level files are still the same
                {
                    bool do_aabb = false;
                    if (canBeUnderwater(draw_tile) && e->position.y < 2.0f)
                    {
                        do_aabb = true;
                    }

                    if (e->locked) draw_tile = LOCKED_BLOCK;
                    if (draw_tile == WIN_BLOCK)
                    {
                        if (temp_state.in_overworld && findInSolvedLevels(e->next_level) != -1) draw_tile = WON_BLOCK;
                        else if (!temp_state.in_overworld && findInSolvedLevels(world_state.level_name) != -1) draw_tile = WON_BLOCK;
                    }

                    if (game_shader_mode == OLD)
                    {
                        drawAsset(getCube3DId(draw_tile), CUBE_3D, e->position, DEFAULT_SCALE, e->rotation, VEC4_0, do_aabb, VEC4_0, VEC4_0); 
                    }
                    else
                    {
                        drawAsset(getModelId(draw_tile), MODEL_3D, e->position, DEFAULT_SCALE, e->rotation, VEC4_0, do_aabb, VEC4_0, VEC4_0);
                    }
                }
            }
            else
            {
                if (game_shader_mode != OLD && getCube3DId(draw_tile) == CUBE_3D_WATER) 
                {
                    drawAsset(MODEL_3D_WATER, WATER_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToQuaternion(world_state.buffer[tile_index + 1]), VEC4_0, false, VEC4_0, VEC4_0);
                }
                drawAsset(getCube3DId(draw_tile), CUBE_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToQuaternion(world_state.buffer[tile_index + 1]), VEC4_0, false, VEC4_0, VEC4_0);
            }
        }

        if (!world_state.player.removed)
        {
            bool do_player_aabb = false;
            if (player->position.y < 2.0f) do_player_aabb = true;

            // TODO: this is terrible (fix with shaders)
            if (temp_state.player_hit_by_red && temp_state.player_hit_by_blue) drawAsset(CUBE_3D_PLAYER_MAGENTA, CUBE_3D, player->position, PLAYER_SCALE, player->rotation, VEC4_0, do_player_aabb, VEC4_0, VEC4_0);
            else if (temp_state.player_hit_by_red)                             drawAsset(CUBE_3D_PLAYER_RED,     CUBE_3D, player->position, PLAYER_SCALE, player->rotation, VEC4_0, do_player_aabb, VEC4_0, VEC4_0);
            else if (temp_state.player_hit_by_blue)                            drawAsset(CUBE_3D_PLAYER_BLUE,    CUBE_3D, player->position, PLAYER_SCALE, player->rotation, VEC4_0, do_player_aabb, VEC4_0, VEC4_0);
            else drawAsset(CUBE_3D_PLAYER, CUBE_3D, player->position, PLAYER_SCALE, player->rotation, VEC4_0, do_player_aabb, VEC4_0, VEC4_0);
        }
        if (!world_state.pack.removed) 
        {
            bool do_pack_aabb = false;
            if (player->position.y < 2.0f) do_pack_aabb = true;
            drawAsset(CUBE_3D_PACK, CUBE_3D, world_state.pack.position, PLAYER_SCALE, world_state.pack.rotation, VEC4_0, do_pack_aabb, VEC4_0, VEC4_0);
        }

        // draw camera boundary lines
        if (time_until_allow_meta_input == 0 && input->keys_held & KEY_T && !(editor_state.editor_mode == SELECT_WRITE))
        {
            draw_level_boundary = !draw_level_boundary;
            if (draw_level_boundary) createDebugPopup("level / camera boundary visibility on", LEVEL_BOUNDARY_VISIBILITY_CHANGE);
            else                     createDebugPopup("level / camera boundary visibility off", LEVEL_BOUNDARY_VISIBILITY_CHANGE);
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }
        if (draw_level_boundary)
        {
            if (temp_state.in_overworld)
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

                        drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, x_draw_coords, x_wall_scale, IDENTITY_QUATERNION, VEC4_0, false, VEC4_0, VEC4_0);
                        drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, z_draw_coords, z_wall_scale, IDENTITY_QUATERNION, VEC4_0, false, VEC4_0, VEC4_0);
                        x_draw_offset += OVERWORLD_SCREEN_SIZE_X;
                    }
                    x_draw_offset = 0;
                    z_draw_offset += OVERWORLD_SCREEN_SIZE_Z;
                }
            }
            else
            {
                // draw level boundary
                Vec3 x_draw_coords_near = (Vec3){ -0.5f,                     (float)level_dim.y / 2.0f, ((float)level_dim.z / 2.0f) };
                Vec3 z_draw_coords_near = (Vec3){ (float)level_dim.x / 2.0f, (float)level_dim.y / 2.0f, -0.5f};
                Vec3 x_draw_coords_far  = (Vec3){ (float)level_dim.x + 0.5f, (float)level_dim.y / 2.0f, (float)level_dim.z / 2.0f };
                Vec3 z_draw_coords_far  = (Vec3){ (float)level_dim.x / 2.0f, (float)level_dim.y / 2.0f, (float)level_dim.z + 0.5f };
                Vec3 x_draw_scale = (Vec3){ 0,                         (float)level_dim.y, (float)level_dim.z + 1.0f };
                Vec3 z_draw_scale = (Vec3){ (float)level_dim.x + 1.0f, (float)level_dim.y, 0 };
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, x_draw_coords_near, x_draw_scale, IDENTITY_QUATERNION, VEC4_0, false, VEC4_0, VEC4_0);
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, z_draw_coords_near, z_draw_scale, IDENTITY_QUATERNION, VEC4_0, false, VEC4_0, VEC4_0);
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, x_draw_coords_far,  x_draw_scale, IDENTITY_QUATERNION, VEC4_0, false, VEC4_0, VEC4_0);
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, z_draw_coords_far,  z_draw_scale, IDENTITY_QUATERNION, VEC4_0, false, VEC4_0, VEC4_0);
            }
        }

        // draw outline around trailing hitboxes
        if (draw_trailing_hitboxes)
        {
            FOR(th_index, MAX_TRAILING_HITBOX_COUNT)
            {
                TrailingHitbox th = temp_state.trailing_hitboxes[th_index];
                if (th.frames == 0) continue;
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, intCoordsToNorm(th.coords), DEFAULT_SCALE, IDENTITY_QUATERNION, VEC4_0, false, VEC4_0, VEC4_0);
            }
        }
    }

    /////////////
    // DRAW 2D //
    /////////////

    {
        Vec4 color_with_alpha = { 0, 0, 0, 1 }; // using alpha as first channel. 2d assets just use sprite atlas, so not using color.
        
        if (editor_state.editor_mode != NO_MODE)
        {
            // crosshair
            Vec3 crosshair_scale = { 35.0f, 35.0f, 0.0f };
            Vec3 center_screen = { ((float)game_display.client_width / 2), ((float)game_display.client_height / 2), 0.0f };
            drawAsset(SPRITE_2D_CROSSHAIR, SPRITE_2D, center_screen, crosshair_scale, IDENTITY_QUATERNION, color_with_alpha, false, VEC4_0, VEC4_0);

            // picked block
            Vec3 picked_block_scale = { 200.0f, 200.0f, 0.0f };
            Vec3 picked_block_coords = { game_display.client_width - (picked_block_scale.x / 2) - 20, (picked_block_scale.y / 2) + 50, 0.0f };
            drawAsset(getSprite2DId(editor_state.picked_tile), SPRITE_2D, picked_block_coords, picked_block_scale, IDENTITY_QUATERNION, color_with_alpha, false, VEC4_0, VEC4_0);

            if (editor_state.selected_id >= 0 && (editor_state.editor_mode == SELECT || editor_state.editor_mode == SELECT_WRITE))
            {
                SpriteId selected_id = getCube3DId(MIRROR);
                if (game_shader_mode != OLD) selected_id = getModelId(getTileTypeFromId(editor_state.selected_id));
                Entity* selected_e = 0;
                if (editor_state.selected_id > 0) selected_e = getEntityFromId(editor_state.selected_id);
                if (selected_e) drawAsset(selected_id, OUTLINE_3D, selected_e->position, DEFAULT_SCALE, selected_e->rotation, VEC4_0, false, VEC4_0, VEC4_0);
            }
        }

        // handle decrementing timers which should be consistent across physics timesteps
        timer_accumulator += delta_time;
        global_time += (delta_time / physics_timestep_multiplier);
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

        // draw debug popup
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
