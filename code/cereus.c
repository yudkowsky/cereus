#include "everything.h"

#define FOR(i, n) for (int i = 0; i < n; i++)

// TEMP: for profiling
__declspec(dllimport) int __stdcall QueryPerformanceCounter(long long* lpPerformanceCount);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(long long* lpFrequency);
__declspec(dllimport) void __stdcall OutputDebugStringA(const char* lpOutputString);

#define DEBUG_TEXT(...) do {                                    \
    char debug_buffer[256] = {0};                               \
    snprintf(debug_buffer, sizeof(debug_buffer), __VA_ARGS__);  \
    createDebugText(debug_buffer);                              \
} while (false)

#define DEBUG_POPUP(popup_type, ...) do {                       \
    char debug_buffer[256] = {0};                               \
    snprintf(debug_buffer, sizeof(debug_buffer), __VA_ARGS__);  \
    createDebugPopup(debug_buffer, (popup_type));               \
} while (false)

// GLOBAL STATE

typedef enum
{
    TILE_TYPE_NONE = 0,
    TILE_TYPE_VOID,
    TILE_TYPE_GRID,
    TILE_TYPE_WALL,
    TILE_TYPE_BOX,
    TILE_TYPE_PLAYER,
    TILE_TYPE_MIRROR,
    TILE_TYPE_GLASS,
    TILE_TYPE_PACK,
    TILE_TYPE_WATER,
    TILE_TYPE_WIN_BLOCK,

    TILE_TYPE_SOURCE_RED,
    TILE_TYPE_SOURCE_BLUE,
    TILE_TYPE_SOURCE_MAGENTA,

    TILE_TYPE_LOCKED_BLOCK,
    TILE_TYPE_LADDER,
    TILE_TYPE_WON_BLOCK,

    TILE_TYPE_COUNT
}
TileType;

typedef enum
{
    MIRROR_SIDE = 0,
    MIRROR_UP,
    MIRROR_DOWN,
}
MirrorOrientation;

typedef enum
{
    NO_DIRECTION = 0,
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
    MOVE_FORWARD = 0,
    MOVE_TURN,
    MOVE_BACK,
}
MoveType;

typedef enum
{
    COLOR_NONE = 0,
    COLOR_RED,
    COLOR_BLUE,
    COLOR_MAGENTA,
}
Color;

typedef enum
{
    EDITOR_MODE_NONE = 0,
    EDITOR_MODE_PLACE_BREAK,
    EDITOR_MODE_SELECT,
    EDITOR_MODE_SELECT_WRITE,
    EDITOR_MODE_WATER_PAINT,
    EDITOR_MODE_ENVIRONMENT,
}
EditorMode;

typedef enum
{
    WRITING_FIELD_NONE = 0,
    WRITING_FIELD_NEXT_LEVEL,
    WRITING_FIELD_UNLOCKED_BY
}
WritingField;

typedef enum
{
    WORLD_0,
    WORLD_1,
    WORLD_2,
}
GameProgress;

typedef enum
{
    MAIN_WAITING,
    MAIN_TO_ALT,
    ALT_WAITING,
    ALT_TO_MAIN,
}
CameraMode;

typedef enum
{
    POPUP_TYPE_NONE = 0,
    POPUP_TYPE_GAMEPLAY_MODE_CHANGE,
    POPUP_TYPE_GAMEPLAY_SPEED_CHANGE,
    POPUP_TYPE_DEBUG_STATE_VISIBILITY_TOGGLE,
    POPUP_TYPE_LEVEL_BOUNDARY_VISIBILITY_TOGGLE,
    POPUP_TYPE_LEVEL_SAVE,
    POPUP_TYPE_MAIN_CAMERA_SAVE,
    POPUP_TYPE_ALT_CAMERA_SAVE,
    POPUP_TYPE_PHYSICS_TIMESTEP_CHANGE,
    POPUP_TYPE_CHEAT_MODE_TOGGLE,
    POPUP_TYPE_SHADER_MODE_CHANGE,
    POPUP_TYPE_DRAW_TRAILING_HITBOX_TOGGLE,
    POPUP_TYPE_STEP_THROUGH_TOGGLE,
    POPUP_TYPE_PAINT_BRUSH_RADIUS_CHANGE,
    POPUP_TYPE_EDITOR_BLOCK_PLACE_OOB,
    POPUP_TYPE_SUN_DIRECTION_CHANGE,
    POPUP_TYPE_LEVEL_Y_CHANGE,
}
PopupType;

typedef struct Entity
{
    int32 id;
    Int3 coords;
    Vec3 position;
    Direction direction;
    Vec4 rotation;
    bool removed;
    bool in_use;

    // for mirrors
    MirrorOrientation mirror_orientation;

    // movement state
    Vec3 velocity;
    Direction moving_direction; // deliberate movement: does not include falling.
    bool falling;
    bool fall_handled; // reset each frame
    bool moving_on_head;
    int32 root_entity_id;
    bool tied_to_pack_and_decoupled;

    // for sources/lasers
    Color color;

    // for win blocks
    char next_level[64]; // NOTE: make level names an enum so don't need to carry around 64 * char * 2 per entity

    // for locked blocks (and other entities that are locked)
    bool locked;
    char unlocked_by[64]; 
}
Entity;

#define MAX_ENTITY_INSTANCE_COUNT 64
#define ENTITY_TYPES 6

// the approx. 2MB buffer is dense representation of the level encoded by coords
typedef struct WorldState
{
    Entity player;
    Entity pack;
    Entity boxes[MAX_ENTITY_INSTANCE_COUNT];
    Entity mirrors[MAX_ENTITY_INSTANCE_COUNT];
    Entity sources[MAX_ENTITY_INSTANCE_COUNT];
    Entity glass_blocks[MAX_ENTITY_INSTANCE_COUNT];
    Entity win_blocks[MAX_ENTITY_INSTANCE_COUNT];
    Entity locked_blocks[MAX_ENTITY_INSTANCE_COUNT];

    uint8 buffer[2000000]; // 2 bytes info per tile 

    char level_name[64];

    char solved_levels[64][64];
}
WorldState;

typedef struct TrailingHitbox
{
    int32 id;
    Int3 coords;
    int32 frames;
    TileType type;
}
TrailingHitbox;

// a bunch of state to do with handling what gets pushed when during when the player is turning
typedef struct PackTurnState
{
    int32 pack_intermediate_states_timer;
    Int3 pack_intermediate_coords;
    Direction initial_player_direction;
    int32 half_failed_turn_timer;
    bool diagonal_push_happened_this_turn; // used for deciding whether or not to pop undo if half-fails
    int32 turn_total_frames;
}
PackTurnState;

typedef struct LaserBuffer
{
    Vec3 start_coords;
    Vec3 end_coords;
    Direction direction;
    Color color;
    Vec4 start_clip_plane;
    Vec4 end_clip_plane;
}
LaserBuffer;

typedef struct TemporaryState
{
    TrailingHitbox trailing_hitboxes[32]; 
    PackTurnState pack_turn_state;
    LaserBuffer laser_buffer[512]; // 512 = 64 max sources * 16 max laser turns
    int32 allow_movement_timer; // if > 0, decrements every frame towards 0, and then able to move. if -1, movement is permanently stopped until some other action resets it.
    bool pack_attached;
    int32 player_hit_by_red;
    int32 player_hit_by_blue_timer;
}
TemporaryState;

// EDITOR STRUCTS

typedef struct EditBuffer
{
    char string[64];
    int32 length;
}
EditBuffer;

typedef struct EditorState
{
    EditorMode editor_mode;
    bool do_wide_camera;
    Int3 selected_coords;
    TileType picked_tile;

    float brush_radius_modifier;

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

typedef struct DebugPopup
{
    int32 frames_left;
    char text[256];
    Vec2 coords;
    PopupType type;
}
DebugPopup;

// UNDO BUFFER STRUCTS

typedef struct UndoEntityDelta
{
    int32 id;
    Int3 old_coords;
    Direction old_direction;
    MirrorOrientation old_mirror_orientation;
    bool was_removed;
}
UndoEntityDelta;

typedef struct UndoActionHeader
{
    uint8 entity_count;
    uint32 delta_start_pos;
    bool level_changed;
}
UndoActionHeader;

typedef struct UndoLevelChange
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
const Vec3 DEFAULT_SCALE = { 1.0f,  1.0f,  1.0f  };
const Vec3 PLAYER_SCALE  = { 0.75f, 0.75f, 0.75f };

const float LASER_WIDTH = 0.25;
const float MAX_RAYCAST_SEEK_LENGTH = 100.0f;

const float PLAYER_MAX_SPEED = 0.12f;
const float MIN_FALL_VELOCITY = -0.15f;
const int32 TURN_TIME = 10;
const float MAX_ANGULAR_VELOCITY = (TAU * 0.25f) / 10.0f; // last number is number of frames for a full turn
const float PLAYER_ACCELERATION = 0.04f;
const float PLAYER_MAX_DECELERATION = 0.04f;
const float CLIMBING_SPEED = 0.12f;
const float GRAVITY = -0.03f;

const int32 STANDARD_TIME_UNTIL_ALLOW_INPUT = 9;
const int32 PLACE_BREAK_TIME_UNTIL_ALLOW_INPUT = 5;
const int32 TRAILING_HITBOX_TIME = 7;
const int32 FALL_TRAILING_HITBOX_TIME = 10;
const int32 SIMULATE_FORWARD_TICK_COUNT = 8;
const int32 HALF_FAILED_PACK_TURN_COOLDOWN = 6;
const int32 HIT_BY_BLUE_TIME = 3;

const int32 MAX_ENTITY_PUSH_COUNT = 32;
const int32 MAX_ENTITIES_TIED_TO_MOVEMENT = 32;
const int32 MAX_LASER_TRAVEL_DISTANCE = 256;
const int32 MAX_LASER_TURNS_ALLOWED = 16;
const int32 MAX_PUSHABLE_STACK_SIZE = 32;
const int32 MAX_TRAILING_HITBOX_COUNT = 32;
const int32 MAX_LEVEL_COUNT = 64;
const int32 MAX_DEBUG_POPUP_TYPE_COUNT = 32;
#define MAX_SOURCE_COUNT 32

const Int3 AXIS_X = { 1, 0, 0 };
const Int3 AXIS_Y = { 0, 1, 0 };
const Int3 AXIS_Z = { 0, 0, 1 };
const Vec4 IDENTITY_QUATERNION  = { 0, 0, 0, 1 };

const int32 PLAYER_ID = 1;
const int32 PACK_ID   = 2;
const int32 ID_OFFSET_BOX          = 100 * 1;
const int32 ID_OFFSET_MIRROR       = 100 * 2;
const int32 ID_OFFSET_GLASS        = 100 * 3;
const int32 ID_OFFSET_SOURCE       = 100 * 4;
const int32 ID_OFFSET_WIN_BLOCK    = 100 * 7;
const int32 ID_OFFSET_LOCKED_BLOCK = 100 * 8;

const int32 FONT_FIRST_ASCII = 32;
const int32 FONT_LAST_ASCII = 126;
const int32 FONT_CELL_WIDTH_PX = 6;
const int32 FONT_CELL_HEIGHT_PX = 10;
const float DEFAULT_TEXT_SCALE = 30.0f;

const char TILE_BUFFER_CHUNK_TAG[4] = "TILE";

const char MAIN_CAMERA_CHUNK_TAG[4] = "CMRA";
const char ALT_CAMERA_CHUNK_TAG[4] = "CAM2";
const int32 CAMERA_CHUNK_SIZE = 24;

const char WIN_BLOCK_CHUNK_TAG[4] = "WINB";
const int32 WIN_BLOCK_CHUNK_SIZE = 76;

const char LOCKED_INFO_CHUNK_TAG[4] = "LOKB";
const int32 LOCKED_INFO_CHUNK_SIZE = 76;

const char SUN_DIRECTION_CHUNK_TAG[4] = "ENVT";
const int32 SUN_DIRECTION_CHUNK_SIZE = 12;

const char WATER_INFO_CHUNK_TAG[4] = "WATR";
const int32 WATER_INFO_CHUNK_SIZE = 4;

const int32 OVERWORLD_SCREEN_SIZE_X = 21;
const int32 OVERWORLD_SCREEN_SIZE_Z = 15;

const float NO_WATER_PLANE_LOW_VALUE = -999.0f;

const double DEFAULT_PHYSICS_TIMESTEP = 1.0/60.0;
double physics_timestep_multiplier = 1.0;
double physics_accumulator = 0; // time accumulator affected by physics timestep
double timer_accumulator = 0; // true time accumulator
double global_time = 0; // will not work as a 'time elapsed' counter in editor mode because also affected by physics timestep
bool step_mode = false;
bool step_to_next_tick = false;

DisplayInfo game_display = {0};
Input prev_input = {0}; // copied from previous frame input to generate keys_pressed

DrawCommand draw_commands[8192] = {0};
int32 draw_command_count = 0;

const char DEBUG_LEVEL_NAME[64] = "testing";
const char RELATIVE_LEVEL_FOLDER_PATH[64] = "data/levels/";
const char SOURCE_LEVEL_FOLDER_PATH[64] = "../cereus/data/levels/";
const char LEVEL_BASE_FILE_NAME[64] = "base.level";
const char WATER_TEXTURE_FILE_NAME[64] = "water.texture";
const char SOLVED_LEVELS_PATH[64] = "data/meta/solved-levels.meta";
const char UNDO_DATA_PATH[64] = "data/meta/undo-buffer.meta";
const char OVERWORLD_ZERO_NAME[64] = "overworld-zero";

// camera
const float CAMERA_SENSITIVITY = 0.0005f;
const float CAMERA_MOVE_STEP = 0.2f;
const float CAMERA_FOV = 15.0f;

Camera camera = {0};
Camera camera_with_ow_offset = {0};
CameraMode camera_mode = MAIN_WAITING;

Camera saved_main_camera = {0};
Camera saved_alt_camera = {0};
Camera saved_overworld_camera = {0};
CameraMode saved_overworld_camera_mode = {0};

const Int3 OVERWORLD_CAMERA_CENTER_START = { 58, 2, 197 };
Int3 camera_screen_offset = {0};
bool draw_level_boundary = false;
Int3 ow_player_coords_for_offset = {0};
bool silence_unlocks_due_to_restart_or_undo = false;

const float CAMERA_T_TIMESTEP = 0.05f;
float camera_lerp_t = 0.0f;
int32 camera_target_plane = 0; // y level of xz plane which calculates targeted point during camera interpolation function

// general state
WorldState world_state = {0};
TemporaryState temp_state = {0};
GameProgress game_progress = WORLD_0;
Int3 level_dim = {0};
Int3 level_origin = {0};
Int3 overworld_restart_coords = {0};
bool in_overworld = true;

float water_plane_y = 0.0f;
Vec3 sun_direction = {0};

Rgba8 water_texture_scratch[WATER_PAINT_MAX_SIDE * WATER_PAINT_MAX_SIDE] = {0};

Entity* player = &world_state.player;
Entity* pack = &world_state.pack;
Entity* all_entity_groups[6] = { world_state.boxes, world_state.mirrors, world_state.locked_blocks, world_state.glass_blocks, world_state.sources, world_state.win_blocks };
Entity* interactible_entity_groups[3] = { world_state.boxes, world_state.mirrors, world_state.sources };
Entity* lockable_entity_groups[4] = { world_state.boxes, world_state.mirrors, world_state.win_blocks, world_state.sources };

WorldState leap_of_faith_world_state_snapshot = {0};
TemporaryState leap_of_faith_temp_state_snapshot = {0};
WorldState overworld_zero_state = {0};
uint8 temp_buffer_array[sizeof(world_state.buffer)];

int32 time_until_allow_meta_input = 0;
int32 time_until_allow_undo_or_restart_input = 0;

// handle undos
UndoBuffer undo_buffer = {0};
int32 undos_performed = 0;
int32 undo_press_timer = 0;
bool restart_last_turn = false;

// debug state
EditorState editor_state = {0};
ShaderMode game_shader_mode = SHADER_MODE_DEFAULT;
bool draw_trailing_hitboxes = false;
bool cheating = false;

// debug text
const int32 MAX_DEBUG_TEXT_COUNT = 32;
const float DEBUG_TEXT_Y_DIFF = 40.0f;
Vec2 debug_text_start_coords = {0};
char debug_text_buffer[32][256] = {0};
int32 debug_text_count = 0;
bool do_debug_text = false;

// debug popups
const float DEBUG_POPUP_TYPE_STEP_SIZE = 30.0f;
const int32 DEFAULT_POPUP_TYPE_TIME = 100;
Vec2 debug_popup_start_coords = {0};
DebugPopup debug_popups[32];

// water paint
WaterPaintTexture water_paint_texture = {0};

// MATH HELPER FUNCTIONS

float floatMax(float a, float b)
{
    return a > b ? a : b;
}

float floatAbs(float f)
{
    return f > 0 ? f : -f;
}

Vec3 vec3FromInt3(Int3 int_coords)
{
    return (Vec3){ (float)int_coords.x, (float)int_coords.y, (float)int_coords.z };
}

Int3 int3FromVec3(Vec3 position)
{
    return (Int3){ (int32)roundf(position.x), (int32)roundf(position.y), (int32)roundf(position.z) };
}

bool int3IsEqual(Int3 a, Int3 b) 
{
    return (a.x == b.x && a.y == b.y && a.z == b.z); 
}

bool vec3IsEqual(Vec3 a, Vec3 b)
{
    return (a.x == b.x && a.y == b.y && a.z == b.z); 
}

bool vec4IsEqual(Vec4 a, Vec4 b)
{
    return (a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w); 
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

Int3 int3ScalarMultiply(Int3 v, int32 s) 
{
    return (Int3){ v.x*s, v.y*s, v.z*s }; 
}

Vec3 vec3ScalarMultiply(Vec3 v, float s) 
{
    return (Vec3){ v.x*s, v.y*s, v.z*s }; 
}

Vec3 vec3Abs(Vec3 v) 
{
    return (Vec3){ floatAbs(v.x), floatAbs(v.y), floatAbs(v.z) }; 
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
    if (length_squared <= 1e-8f) return (Vec3){0}; 
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

// NOTE: could use the optimised version when i understand quaternions better. for now, this is more transparent
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
    Vec4 quaternion_yaw   = quaternionFromAxis(vec3FromInt3(AXIS_Y), input_camera.yaw);
    Vec4 quaternion_pitch = quaternionFromAxis(vec3FromInt3(AXIS_X), input_camera.pitch);
    return quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));
}

// assumes looking toward plane (otherwise negative t value)
Vec3 cameraLookingAtPointOnPlane(Camera input_camera, float plane_y)
{
    Vec3 neg_z_axis = vec3FromInt3(int3Negate(AXIS_Z)); // standard camera axis before any rotation
    Vec3 forward = vec3RotateByQuaternion(neg_z_axis, buildCameraQuaternion(input_camera)); // get the cameras forward vector
    float t = (plane_y - input_camera.coords.y) / forward.y; // get t value for intersection
    return (Vec3)
    {
        input_camera.coords.x + forward.x * t,
        input_camera.coords.y + forward.y * t,
        input_camera.coords.z + forward.z * t
    };
}

Camera lerpCamera(Camera start_cam, Camera end_cam, float t, float target_plane_y)
{
    Camera result = {0};
    result.coords = vec3Add(start_cam.coords, vec3ScalarMultiply(vec3Subtract(end_cam.coords, start_cam.coords), t));
    result.fov = start_cam.fov + (end_cam.fov - start_cam.fov) * t;

    Vec3 target_a = cameraLookingAtPointOnPlane(start_cam, target_plane_y);
    Vec3 target_b = cameraLookingAtPointOnPlane(end_cam,   target_plane_y);
    Vec3 target = vec3Add(target_a, vec3ScalarMultiply(vec3Subtract(target_b, target_a), t));

    // build rotation from looked at point
    Vec3 forward = vec3Normalize(vec3Subtract(target, result.coords));
    result.yaw   = (float)atan2(-(double)forward.x, -(double)forward.z);
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

bool intCoordsWithinLevelBounds(Int3 coords) 
{
    return coords.x >= level_origin.x && coords.y >= level_origin.y && coords.z >= level_origin.z
        && coords.x < level_origin.x + level_dim.x && coords.y < level_origin.y + level_dim.y && coords.z < level_origin.z + level_dim.z;
}

int32 coordsToBufferIndexType(Int3 coords)
{
    int32 x = coords.x - level_origin.x;
    int32 y = coords.y - level_origin.y;
    int32 z = coords.z - level_origin.z;
    return 2 * (level_dim.x*level_dim.z*y + level_dim.x*z + x);
}

int32 coordsToBufferIndexDirection(Int3 coords)
{
    return coordsToBufferIndexType(coords) + 1;
}

Int3 bufferIndexToCoords(int32 buffer_index)
{
    int32 tile_index = buffer_index / 2; // TODO: probably redo this with a struct instead of always dealing with "two bytes"?
    return (Int3){
        (tile_index % level_dim.x) + level_origin.x,
        tile_index / (level_dim.x*level_dim.z) + level_origin.y,
        (tile_index / level_dim.x) % level_dim.z + level_origin.z,
    };
}

void setTileType(TileType type, Int3 coords) 
{
    world_state.buffer[coordsToBufferIndexType(coords)] = type; 
}

void setTileDirection(Direction direction, Int3 coords, MirrorOrientation mirror_orientation)
{
    world_state.buffer[coordsToBufferIndexDirection(coords)] = (uint8)(direction + 8*mirror_orientation);
}

TileType getTileType(Int3 coords) 
{
    if (!intCoordsWithinLevelBounds(coords)) return TILE_TYPE_WALL;
    return world_state.buffer[coordsToBufferIndexType(coords)]; 
}

Direction getTileDirection(Int3 coords) 
{
    if (!intCoordsWithinLevelBounds(coords)) return NO_DIRECTION;
    return world_state.buffer[coordsToBufferIndexDirection(coords)]; 
}

// sets coords and position of an entity to some values, and updates the buffer accordingly 
void moveEntityInBufferAndState(Entity* e, Int3 end_coords, Direction end_direction)
{
    TileType type = getTileType(e->coords); // could also get from id
    setTileType(TILE_TYPE_NONE, e->coords);
    setTileDirection(NO_DIRECTION, e->coords, e->mirror_orientation);
    e->coords = end_coords;
    e->direction = end_direction;
    setTileType(type, end_coords);
    setTileDirection(end_direction, end_coords, e->mirror_orientation);
}

void getLevelMinAndMax(Int3* level_min, Int3* level_max)
{
    *level_min = (Int3){ INT32_MAX, INT32_MAX, INT32_MAX };
    *level_max = (Int3){ INT32_MIN, INT32_MIN, INT32_MIN };
    for (int32 tile_index = 0; tile_index < 2 * level_dim.x*level_dim.y*level_dim.z; tile_index += 2)
    {
        if (world_state.buffer[tile_index] == TILE_TYPE_NONE) continue;
        Int3 coords = bufferIndexToCoords(tile_index);
        if (coords.x > level_max->x) level_max->x = coords.x;
        if (coords.y > level_max->y) level_max->y = coords.y;
        if (coords.z > level_max->z) level_max->z = coords.z;
        if (coords.x < level_min->x) level_min->x = coords.x;
        if (coords.y < level_min->y) level_min->y = coords.y;
        if (coords.z < level_min->z) level_min->z = coords.z;
    }
}

// returns false if reindex would be too large
bool reindexBuffer(Int3 new_origin, Int3 new_dim)
{
    int32 new_total_tiles = new_dim.x*new_dim.y*new_dim.z;
    if (new_total_tiles * 2 > (int32)sizeof(world_state.buffer)) return false;

    for (int32 tile_index = 0; tile_index < 2 * level_dim.x*level_dim.y*level_dim.z; tile_index += 2)
    {
        if (world_state.buffer[tile_index] == TILE_TYPE_NONE) continue;
        Int3 coords = bufferIndexToCoords(tile_index);
        int32 new_x = coords.x - new_origin.x;
        int32 new_y = coords.y - new_origin.y;
        int32 new_z = coords.z - new_origin.z;
        if (new_x < 0 || new_y < 0 || new_z < 0 || new_x >= new_dim.x || new_y >= new_dim.y || new_z >= new_dim.z) continue; // guard in case something goes wrong
        int32 new_index = 2 * (new_dim.x*new_dim.z*new_y + new_dim.x*new_z + new_x);
        temp_buffer_array[new_index] = world_state.buffer[tile_index];
        temp_buffer_array[new_index + 1] = world_state.buffer[tile_index + 1];
    }
    memcpy(world_state.buffer, temp_buffer_array, new_total_tiles * 2);
    memset(temp_buffer_array, 0, new_total_tiles*2);

    // reindex water texture
    int32 old_width  = level_dim.x * WATER_PAINT_RESOLUTION;
    int32 old_height = level_dim.z * WATER_PAINT_RESOLUTION;
    if (old_width  > WATER_PAINT_MAX_SIDE) old_width  = WATER_PAINT_MAX_SIDE;
    if (old_height > WATER_PAINT_MAX_SIDE) old_height = WATER_PAINT_MAX_SIDE;
    int32 new_width  = new_dim.x * WATER_PAINT_RESOLUTION;
    int32 new_height = new_dim.z * WATER_PAINT_RESOLUTION;
    if (new_width  > WATER_PAINT_MAX_SIDE) new_width  = WATER_PAINT_MAX_SIDE;
    if (new_height > WATER_PAINT_MAX_SIDE) new_height = WATER_PAINT_MAX_SIDE;

    int32 shift_x = (new_origin.x - level_origin.x) * WATER_PAINT_RESOLUTION;
    int32 shift_z = (new_origin.z - level_origin.z) * WATER_PAINT_RESOLUTION;

    FOR(scratch_index, new_width * new_height) water_texture_scratch[scratch_index] = (Rgba8){ 0, 0, 0, 255 };

    FOR(new_z, new_height)
    {
        int32 old_z = new_z + shift_z;
        if (old_z < 0 || old_z >= old_height) continue;
        FOR(new_x, new_width)
        {
            int32 old_x = new_x + shift_x;
            if (old_x < 0 || old_x >= old_width) continue;
            water_texture_scratch[new_z * new_width + new_x] = water_paint_texture.values[old_z * old_width + old_x];
        }
    }
    memcpy(water_paint_texture.values, water_texture_scratch, sizeof(Rgba8) * new_width * new_height);
    water_paint_texture.dirty = true;

    level_origin = new_origin;
    level_dim = new_dim;
    return true;
}

// sets water plane to just above lowest water
void recalculateWaterPlane()
{
    for (int32 buffer_index = 0; buffer_index < 2 * level_dim.x*level_dim.y*level_dim.z; buffer_index += 2)
    {
        if (world_state.buffer[buffer_index] != TILE_TYPE_WATER) continue;
        water_plane_y = bufferIndexToCoords(buffer_index).y + 1.4f;
        return;
    }
}

// ENTITY STUFF

bool isSource(TileType type) 
{
    return (type == TILE_TYPE_SOURCE_RED || type == TILE_TYPE_SOURCE_BLUE || type == TILE_TYPE_SOURCE_MAGENTA);
}

// only checks tile types - doesn't do what canPush does
bool isPushable(TileType type)
{
    return (type == TILE_TYPE_BOX || type == TILE_TYPE_MIRROR || type == TILE_TYPE_PACK || type == TILE_TYPE_PLAYER || isSource(type));
}

bool isEntity(TileType type)
{
    return (type == TILE_TYPE_BOX || type == TILE_TYPE_MIRROR || type == TILE_TYPE_PACK || type == TILE_TYPE_PLAYER || type == TILE_TYPE_WIN_BLOCK || type == TILE_TYPE_LOCKED_BLOCK || isSource(type));
}

TileType getTileTypeFromId(int32 id)
{
    if (id == PLAYER_ID) return TILE_TYPE_PLAYER;
    if (id == PACK_ID) return TILE_TYPE_PACK;
    int32 check = (id / 100 * 100);
    if (check >= ID_OFFSET_SOURCE && check < ID_OFFSET_WIN_BLOCK)
    {
        Color source_color = (id - ID_OFFSET_SOURCE) / 100;
        switch (source_color)
        {
            case COLOR_RED:     return TILE_TYPE_SOURCE_RED;
            case COLOR_BLUE:    return TILE_TYPE_SOURCE_BLUE;
            case COLOR_MAGENTA: return TILE_TYPE_SOURCE_MAGENTA;
            default: return TILE_TYPE_NONE;
        }
    }
    else if (check == ID_OFFSET_BOX)          return TILE_TYPE_BOX;
    else if (check == ID_OFFSET_MIRROR)       return TILE_TYPE_MIRROR;
    else if (check == ID_OFFSET_GLASS)        return TILE_TYPE_GLASS;
    else if (check == ID_OFFSET_WIN_BLOCK)    return TILE_TYPE_WIN_BLOCK;
    else if (check == ID_OFFSET_LOCKED_BLOCK) return TILE_TYPE_LOCKED_BLOCK;
    else return TILE_TYPE_NONE;
}

Color getEntityColor(Int3 coords)
{
    switch (getTileType(coords))
    {
        case TILE_TYPE_SOURCE_RED:     return COLOR_RED;
        case TILE_TYPE_SOURCE_BLUE:    return COLOR_BLUE;
        case TILE_TYPE_SOURCE_MAGENTA: return COLOR_MAGENTA;
        default: return COLOR_NONE;
    }
}

Entity* getEntityAtCoords(Int3 coords)
{
    TileType tile = getTileType(coords);
    Entity *entity_group = 0;
    if (isSource(tile)) entity_group = world_state.sources;
    else switch(tile)
    {
        case TILE_TYPE_BOX:          entity_group = world_state.boxes;          break;
        case TILE_TYPE_MIRROR:       entity_group = world_state.mirrors;        break;
        case TILE_TYPE_GLASS:        entity_group = world_state.glass_blocks;  break;
        case TILE_TYPE_WIN_BLOCK:    entity_group = world_state.win_blocks;    break;
        case TILE_TYPE_LOCKED_BLOCK: entity_group = world_state.locked_blocks; break;
        case TILE_TYPE_PLAYER: return &world_state.player;
        case TILE_TYPE_PACK:   return &world_state.pack;
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
    if (id <= 0) return 0;
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

int32 sourceColorIdOffset(Color color)
{
    switch (color)
    {
        case COLOR_RED:     return COLOR_RED * 100; 
        case COLOR_BLUE:    return COLOR_BLUE * 100;
        case COLOR_MAGENTA: return COLOR_MAGENTA * 100;
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
        default: return (Vec3){0};
    }
}

Vec4 directionToRotation(Direction direction, MirrorOrientation orientation)
{
    // NOTE: these rotations could be hardcoded
    Vec4 rotation = IDENTITY_QUATERNION;
    switch (direction)
    {
        case NORTH: rotation = IDENTITY_QUATERNION;                                    break;
        case WEST:  rotation = quaternionFromAxis(vec3FromInt3(AXIS_Y),  0.25f * TAU); break;
        case SOUTH: rotation = quaternionFromAxis(vec3FromInt3(AXIS_Y),  0.50f * TAU); break;
        case EAST:  rotation = quaternionFromAxis(vec3FromInt3(AXIS_Y), -0.25f * TAU); break;
        case UP:    rotation = quaternionFromAxis(vec3FromInt3(AXIS_X),  0.25f * TAU); break;
        case DOWN:  rotation = quaternionFromAxis(vec3FromInt3(AXIS_X), -0.25f * TAU); break;
        default:    rotation = IDENTITY_QUATERNION;                                    break;
    }
    if (orientation > 0)
    {
        if (orientation == MIRROR_UP) rotation = quaternionMultiply(rotation, quaternionFromAxis(vec3FromInt3(AXIS_X), -0.25f * TAU));
        else                          rotation = quaternionMultiply(rotation, quaternionFromAxis(vec3FromInt3(AXIS_X),  0.25f * TAU));
    }

    return rotation;
}

int32 setEntityInstanceInGroup(Entity* entity_group, Int3 coords, Direction direction, MirrorOrientation orientation, Color color) 
{
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (entity_group[entity_index].in_use) continue;
        entity_group[entity_index].coords = coords;
        entity_group[entity_index].position= vec3FromInt3(coords); 
        entity_group[entity_index].direction = direction;
        entity_group[entity_index].rotation = directionToRotation(direction, orientation);
        entity_group[entity_index].color = color;
        entity_group[entity_index].id = entity_index + entityIdOffset(entity_group, color);
        entity_group[entity_index].removed = false;
        entity_group[entity_index].in_use = true;
        entity_group[entity_index].unlocked_by[0] = '\0';
        entity_group[entity_index].next_level[0] = '\0';
        return entity_group[entity_index].id;
    }
    return 0;
}

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

// the fact that y is checked last here matters sometimes
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

int32 getPushableStackSize(Int3 first_coords, Direction seek_direction)
{
    Int3 current_stack_coords = first_coords;
    int32 stack_size = 0;
    FOR(find_stack_size_index, MAX_PUSHABLE_STACK_SIZE)
    {
        TileType next_tile_type = getTileType(current_stack_coords);
        if (!isPushable(next_tile_type)) break;
        stack_size++;
        current_stack_coords = getNextCoords(current_stack_coords, seek_direction);
    }
    return stack_size;
}

// FILE I/O

// .level file structure:
//
// entire file is flat sequence of chunks. each chunk is:
// 4 byte tag
// 4 byte size (not including tag or size)
// then some data
//
// current chunks:
// TILE; size 24 + 6*N; dims and origin int32 x, y, z, then per tile: int32 buffer_index, uint8 type, uint8 direction
// CMRA; size 24;  float x, y, z, fov, yaw, pitch
// CAM2; size 24;  float x, y, z, fov, yaw, pitch
// WINB; size 76;  int32 x, y, z, char[64] next_level
// LOKB; size 76;  int32 x, y, z, char[64] unlocked_by

void buildLevelFolderPath(char (*out_path)[64], char level_name[64], bool overwrite_source)
{
    char prefix[64];
    if (overwrite_source) memcpy(prefix, SOURCE_LEVEL_FOLDER_PATH, sizeof(prefix));
    else                  memcpy(prefix, RELATIVE_LEVEL_FOLDER_PATH, sizeof(prefix));
    snprintf(*out_path, sizeof(*out_path), "%s%s", prefix, level_name);
}

// gets count and byte offsets of every matching tag
int32 getCountAndPositionOfChunk(FILE* file, char tag[4], int32 positions[64])
{
    char chunk[4] = {0};
    int32 chunk_size = 0;
    int32 count = 0;

    fseek(file, 0, SEEK_SET);
    while (true)
    {
        int32 tag_pos = ftell(file);
        if (fread(chunk, 4, 1, file) != 1) return count; // eof
        if (fread(&chunk_size, 4, 1, file) != 1) return count; // some truncation
        if (memcmp(chunk, tag, 4) == 0)
        {
            positions[count] = tag_pos;
            count++;
            if (count >= 64) return count;
        }
        fseek(file, chunk_size, SEEK_CUR);
    }
}

void loadBufferInfo(FILE* file)
{
    fseek(file, 0, SEEK_SET);

    int32 positions[64] = {0};
    if (getCountAndPositionOfChunk(file, TILE_BUFFER_CHUNK_TAG, positions) != 1) return;
    fseek(file, positions[0] + 4, SEEK_SET);

    int32 size = 0;
    fread(&size, 4, 1, file);
    fread(&level_dim.x, 4, 1, file);
    fread(&level_dim.y, 4, 1, file);
    fread(&level_dim.z, 4, 1, file);
    fread(&level_origin.x, 4, 1, file);
    fread(&level_origin.y, 4, 1, file);
    fread(&level_origin.z, 4, 1, file);

    int32 tile_count = (size - 24) / 6;
    FOR(tile_index, tile_count)
    {
        int32 buffer_index = 0;
        uint8 type = TILE_TYPE_NONE;
        uint8 direction = NO_DIRECTION;
        fread(&buffer_index, 4, 1, file);
        fread(&type, 1, 1, file);
        fread(&direction, 1, 1, file);
        world_state.buffer[buffer_index] = type;
        world_state.buffer[buffer_index + 1] = direction;
    }
}

Camera loadCameraInfo(FILE* file, bool use_alt_camera)
{
    Camera out_camera = {0};

    int32 positions[64] = {0};
    char tag[4] = {0}; 
    if (use_alt_camera) memcpy(&tag, &ALT_CAMERA_CHUNK_TAG, sizeof(tag));
    else                memcpy(&tag, &MAIN_CAMERA_CHUNK_TAG, sizeof(tag));

    if (getCountAndPositionOfChunk(file, tag, positions) != 1) return out_camera;

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

        FOR(group_index, 6)
        {
            FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
            {
                Entity* e = &all_entity_groups[group_index][entity_index];
                if (e->coords.x == x && e->coords.y == y && e->coords.z == z)
                {
                    memcpy(e->unlocked_by, path, sizeof(e->unlocked_by));
                    break;
                }
            }
        }
    }
}

void loadSunDirection(FILE* file)
{
    int32 positions[64];
    if (getCountAndPositionOfChunk(file, SUN_DIRECTION_CHUNK_TAG, positions) != 1)
    {
        sun_direction = (Vec3){ 0.0f, -1.0f, 0.0f };
        return;
    }
    fseek(file, positions[0] + 8, SEEK_SET);
    fread(&sun_direction.x, 4, 1, file);
    fread(&sun_direction.y, 4, 1, file);
    fread(&sun_direction.z, 4, 1, file);
}

void loadWaterInfo(FILE* file)
{
    int32 positions[64] = {0};
    if (getCountAndPositionOfChunk(file, WATER_INFO_CHUNK_TAG, positions) != 1)
    {
        recalculateWaterPlane();
        return;
    }
    fseek(file, positions[0] + 8, SEEK_SET);
    fread(&water_plane_y, 4, 1, file);
}

void writeTileChunkToFile(FILE* file)
{
    fwrite(TILE_BUFFER_CHUNK_TAG, 4, 1, file);
    int32 size = 0;
    int32 size_pos = ftell(file); // will backsolve size at this point later
    fseek(file, 4, SEEK_CUR);

    fwrite(&level_dim.x, 4, 1, file);
    fwrite(&level_dim.y, 4, 1, file);
    fwrite(&level_dim.z, 4, 1, file);
    fwrite(&level_origin.x, 4, 1, file);
    fwrite(&level_origin.y, 4, 1, file);
    fwrite(&level_origin.z, 4, 1, file);

    int32 tile_count = 0;
    for (int buffer_index = 0; buffer_index < level_dim.x*level_dim.y*level_dim.z * 2; buffer_index += 2)
    {
        if (world_state.buffer[buffer_index] == TILE_TYPE_NONE) continue;
        uint8 type = world_state.buffer[buffer_index];
        uint8 direction = world_state.buffer[buffer_index + 1];
        fwrite(&buffer_index, 4, 1, file);
        fwrite(&type, 1, 1, file);
        fwrite(&direction, 1, 1, file);
        tile_count++;
    }

    int32 end_pos = ftell(file); // maintain cursor pos at end of write
    size = 24 + tile_count * 6;
    fseek(file, size_pos, SEEK_SET);
    fwrite(&size, 4, 1, file);
    fseek(file, end_pos, SEEK_SET);
}

void writeCameraToFile(FILE* file, Camera* in_camera, bool alt_camera)
{
    char tag[4] = {0};
    if (alt_camera) memcpy(&tag, ALT_CAMERA_CHUNK_TAG,  sizeof(tag));
    else            memcpy(&tag, MAIN_CAMERA_CHUNK_TAG, sizeof(tag));

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

void writeSunDirectionToFile(FILE* file)
{
    fwrite(SUN_DIRECTION_CHUNK_TAG, 4, 1, file);
    fwrite(&SUN_DIRECTION_CHUNK_SIZE, 4, 1, file);
    fwrite(&sun_direction.x, 4, 1, file);
    fwrite(&sun_direction.y, 4, 1, file);
    fwrite(&sun_direction.z, 4, 1, file);
}

void writeWaterInfoToFile(FILE* file)
{
    fwrite(WATER_INFO_CHUNK_TAG, 4, 1, file);
    fwrite(&WATER_INFO_CHUNK_SIZE, 4, 1, file);
    fwrite(&water_plane_y, 4, 1, file);
}

// doesn't change the camera
void writeBaseLevelInfo(char* folder_path)
{
    char level_path[64];
    snprintf(level_path, sizeof(level_path), "%s/%s", folder_path, LEVEL_BASE_FILE_NAME);
    FILE* file = fopen(level_path, "wb");
    if (!file) return;

    writeTileChunkToFile(file);
    writeSunDirectionToFile(file);
    writeWaterInfoToFile(file);
    writeCameraToFile(file, &saved_main_camera, false);
    if (saved_alt_camera.fov != 0) writeCameraToFile(file, &saved_alt_camera, true);

    FOR(win_block_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        Entity* wb = &world_state.win_blocks[win_block_index];
        if (wb->next_level[0] == '\0') continue;
        if (wb->removed) continue;
        writeWinBlockToFile(file, wb);
    }

    FOR(group_index, 6)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* e = &all_entity_groups[group_index][entity_index];
            if (e->removed) continue;
            if (e->unlocked_by[0] == '\0') continue;
            writeLockedInfoToFile(file, e);
        }
    }
    fclose(file);
}

// water texture (separate file)

void loadWaterTexture(char* folder_path)
{
    water_paint_texture.dirty = true;

    // default empty so a level with no texture file doesn't inherit from previous... shouldn't actually matter, i guess
    FOR(pixel, WATER_PAINT_MAX_SIDE * WATER_PAINT_MAX_SIDE) water_paint_texture.values[pixel] = (Rgba8){ 0, 0, 0, 255 };

    char texture_path[64];
    snprintf(texture_path, sizeof(texture_path), "%s/%s", folder_path, WATER_TEXTURE_FILE_NAME);
    FILE* file = fopen(texture_path, "rb");
    if (!file) return;

    int32 texture_width  = level_dim.x * WATER_PAINT_RESOLUTION;
    int32 texture_height = level_dim.z * WATER_PAINT_RESOLUTION;
    if (texture_width  > WATER_PAINT_MAX_SIDE) texture_width  = WATER_PAINT_MAX_SIDE;
    if (texture_height > WATER_PAINT_MAX_SIDE) texture_height = WATER_PAINT_MAX_SIDE;

    fread(water_paint_texture.values, sizeof(Rgba8), texture_width * texture_height, file);
    fclose(file);
}

void writeWaterTexture(char folder_path[64])
{
    char texture_path[64];
    snprintf(texture_path, sizeof(texture_path), "%s/%s", folder_path, WATER_TEXTURE_FILE_NAME);

    // TODO: this should be based off of anything being present in the paint texture; some levels with water won't need a paint texture.
    if (water_plane_y == NO_WATER_PLANE_LOW_VALUE)
    {
        remove(texture_path); // delete this file (if it exists)
        return;
    }

    int32 texture_width  = level_dim.x * WATER_PAINT_RESOLUTION;
    int32 texture_height = level_dim.z * WATER_PAINT_RESOLUTION;
    if (texture_width  > WATER_PAINT_MAX_SIDE) texture_width  = WATER_PAINT_MAX_SIDE;
    if (texture_height > WATER_PAINT_MAX_SIDE) texture_height = WATER_PAINT_MAX_SIDE;

    FILE* file = fopen(texture_path, "wb");
    if (!file) return;
    fwrite(water_paint_texture.values, sizeof(Rgba8), texture_width * texture_height, file);
    fclose(file);
}

// solved levels (separate file)

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
    FILE* file = fopen(SOLVED_LEVELS_PATH, "rb+");
    FOR(level_index, MAX_LEVEL_COUNT)
    {
        if (fread(world_state.solved_levels[level_index], 64, 1, file) != 1) break;
        if (world_state.solved_levels[level_index][0] == 0) break;
    }
    fclose(file);
}

void writeSolvedLevelsToFile()
{
    FILE* file = fopen(SOLVED_LEVELS_PATH, "wb");
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
    FILE* file = fopen(SOLVED_LEVELS_PATH, "wb");
    fclose(file);
    memset(world_state.solved_levels, 0, sizeof(world_state.solved_levels));
}

// DRAW ASSET

SpriteId getSprite2DId(TileType tile)
{
    switch (tile)
    {
        case TILE_TYPE_NONE:         return NO_ID;
        case TILE_TYPE_VOID:         return SPRITE_2D_VOID;
        case TILE_TYPE_GRID:         return SPRITE_2D_GRID;
        case TILE_TYPE_WALL:         return SPRITE_2D_WALL;
        case TILE_TYPE_BOX:          return SPRITE_2D_BOX;
        case TILE_TYPE_PLAYER:       return SPRITE_2D_PLAYER;
        case TILE_TYPE_MIRROR:       return SPRITE_2D_MIRROR;
        case TILE_TYPE_GLASS:        return SPRITE_2D_GLASS;
        case TILE_TYPE_PACK:         return SPRITE_2D_PACK;
        case TILE_TYPE_WATER:        return SPRITE_2D_WATER;
        case TILE_TYPE_WIN_BLOCK:    return SPRITE_2D_WIN_BLOCK;
        case TILE_TYPE_LOCKED_BLOCK: return SPRITE_2D_LOCKED_BLOCK;
        case TILE_TYPE_LADDER:       return SPRITE_2D_LADDER;

        case TILE_TYPE_SOURCE_RED:     return SPRITE_2D_SOURCE_RED;
        case TILE_TYPE_SOURCE_BLUE:    return SPRITE_2D_SOURCE_BLUE;
        case TILE_TYPE_SOURCE_MAGENTA: return SPRITE_2D_SOURCE_MAGENTA;
        default: return 0;
    }
}

SpriteId getCube3DId(TileType tile)
{
    switch (tile)
    {
        case TILE_TYPE_NONE:         return NO_ID;
        case TILE_TYPE_VOID:         return CUBE_3D_VOID;
        case TILE_TYPE_GRID:         return CUBE_3D_GRID;
        case TILE_TYPE_WALL:         return CUBE_3D_WALL;
        case TILE_TYPE_BOX:          return CUBE_3D_BOX;
        case TILE_TYPE_PLAYER:       return CUBE_3D_PLAYER;
        case TILE_TYPE_MIRROR:       return CUBE_3D_MIRROR;
        case TILE_TYPE_GLASS:        return CUBE_3D_GLASS;
        case TILE_TYPE_PACK:         return CUBE_3D_PACK;
        case TILE_TYPE_WATER:        return CUBE_3D_WATER;
        case TILE_TYPE_WIN_BLOCK:    return CUBE_3D_WIN_BLOCK;
        case TILE_TYPE_LOCKED_BLOCK: return CUBE_3D_LOCKED_BLOCK;
        case TILE_TYPE_LADDER:       return CUBE_3D_LADDER;
        case TILE_TYPE_WON_BLOCK:    return CUBE_3D_WON_BLOCK;

        case TILE_TYPE_SOURCE_RED:     return CUBE_3D_SOURCE_RED;
        case TILE_TYPE_SOURCE_BLUE:    return CUBE_3D_SOURCE_BLUE;
        case TILE_TYPE_SOURCE_MAGENTA: return CUBE_3D_SOURCE_MAGENTA;
        default: return 0;
    }
}

SpriteId getModelId(TileType tile)
{
    switch (tile)
    {
        case TILE_TYPE_NONE:         return NO_ID;
        case TILE_TYPE_VOID:         return MODEL_3D_VOID;
        case TILE_TYPE_GRID:         return MODEL_3D_GRID;
        case TILE_TYPE_WALL:         return MODEL_3D_WALL;
        case TILE_TYPE_BOX:          return MODEL_3D_BOX;
        case TILE_TYPE_PLAYER:       return MODEL_3D_PLAYER;
        case TILE_TYPE_MIRROR:       return MODEL_3D_MIRROR;
        case TILE_TYPE_GLASS:        return MODEL_3D_GLASS;
        case TILE_TYPE_PACK:         return MODEL_3D_PACK;
        case TILE_TYPE_WATER:        return MODEL_3D_WATER;
        case TILE_TYPE_WIN_BLOCK:    return MODEL_3D_WIN_BLOCK;
        case TILE_TYPE_LOCKED_BLOCK: return MODEL_3D_LOCKED_BLOCK;
        case TILE_TYPE_LADDER:       return MODEL_3D_LADDER;
        case TILE_TYPE_WON_BLOCK:    return MODEL_3D_WON_BLOCK;

        case TILE_TYPE_SOURCE_RED:     return MODEL_3D_SOURCE_RED;
        case TILE_TYPE_SOURCE_BLUE:    return MODEL_3D_SOURCE_BLUE;
        case TILE_TYPE_SOURCE_MAGENTA: return MODEL_3D_SOURCE_MAGENTA;
        default: return 0;
    }
}

void drawAsset(SpriteId id, AssetType type, Vec3 coords, Vec3 scale, Vec4 rotation, Vec4 color, Vec4 start_clip_plane, Vec4 end_clip_plane)
{
    if (id < 0) return;
    DrawCommand* command = &draw_commands[draw_command_count++];
    command->sprite_id = id;
    command->type = type;
    command->coords = coords;
    command->scale = scale;
    command->rotation = rotation;
    command->color = color;
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
        drawAsset(id, SPRITE_2D, draw_coords, draw_scale, IDENTITY_QUATERNION, color, (Vec4){0}, (Vec4){0});
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
    if (popup_type != POPUP_TYPE_NONE)
    {
        FOR(popup_index, MAX_DEBUG_POPUP_TYPE_COUNT)
        {
            if (debug_popups[popup_index].frames_left == 0) continue;
            if (debug_popups[popup_index].type == popup_type)
            {
                FOR(string_index, 256) if (string[string_index] == '\0') 
                {
                    debug_popups[popup_index].coords.x = debug_popup_start_coords.x - (((float)string_index / 2) * DEFAULT_TEXT_SCALE * ((float)FONT_CELL_WIDTH_PX / (float)FONT_CELL_HEIGHT_PX));
                    break;
                }
                debug_popups[popup_index].frames_left = DEFAULT_POPUP_TYPE_TIME;
                memcpy(debug_popups[popup_index].text, string, 256 * sizeof(char));
                return;
            }
        }
    }

    // no such type exists, or it has no type, so proceed with looking up into next free
    int32 next_free_in_popups = 0;
    FOR(popup_index, MAX_DEBUG_POPUP_TYPE_COUNT) if (debug_popups[popup_index].frames_left == 0)
    {
        next_free_in_popups = popup_index;
        break;
    }

    FOR(string_index, 256) if (string[string_index] == '\0') 
    {
        debug_popups[next_free_in_popups].coords.x = debug_popup_start_coords.x - (((float)string_index / 2) * DEFAULT_TEXT_SCALE * ((float)FONT_CELL_WIDTH_PX / (float)FONT_CELL_HEIGHT_PX));
        break;
    }
    debug_popups[next_free_in_popups].coords.y = debug_popup_start_coords.y + (next_free_in_popups * DEBUG_POPUP_TYPE_STEP_SIZE);
    debug_popups[next_free_in_popups].frames_left = DEFAULT_POPUP_TYPE_TIME;
    debug_popups[next_free_in_popups].type = popup_type;
    memcpy(debug_popups[next_free_in_popups].text, string, 256 * sizeof(char));
}

void createTutorialPopup()
{

}

// RAYCAST ALGORITHM FOR EDITOR

RaycastHit raycastHitCube(Vec3 start, Vec3 direction, float max_distance)
{
    RaycastHit output = {0};
    Int3 current_cube = int3FromVec3(start);
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
        if (tile != TILE_TYPE_NONE)
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
        world_state.buffer[buffer_index] = TILE_TYPE_NONE;
        world_state.buffer[buffer_index + 1] = NORTH;
    }
    entity->coords = coords;
    entity->position = vec3FromInt3(coords);
    entity->id = id;
    entity->removed = false;
    setTileType(editor_state.picked_tile, coords);
    setTileDirection(NORTH, coords, 0);
}

// TRAILING HITBOXES

void createTrailingHitbox(int32 id, Int3 coords, int32 frames)
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
    temp_state.trailing_hitboxes[hitbox_index].type = getTileTypeFromId(id);
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
        default: return (Vec3){0};
    }
}

Vec3 vec3AddFloatAlongDirection(Direction direction, float f, Vec3 v)
{
    switch (direction)
    {
        case NORTH:
        case SOUTH: return (Vec3){ v.x, v.y, v.z + f };
        case WEST:
        case EAST: return (Vec3){ v.x + f, v.y, v.z };
        case UP:
        case DOWN: return (Vec3){ v.x, v.y + f, v.z };
        default: return (Vec3){0};
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

        if (e->falling) return false;

        // if will fall, don't allow push.
        Int3 coords_below = getNextCoords(current_coords, DOWN);
        TileType type_below = getTileType(coords_below);
        if (type_below == TILE_TYPE_NONE && temp_state.player_hit_by_blue_timer == 0) return false;

        // check within bounds
        current_coords = getNextCoords(current_coords, direction);
        if (!intCoordsWithinLevelBounds(current_coords)) return false;

        /*
        // NOTE: this causes edge case in climb up-down-up, go forward, and then try go back again, within trailing hitbox time, when at least 2 tiles are on the players head.
        //       if this check is needed, could try to only allow one trailing hitbox per coordinate - this could have other adverse effects, am not sure.
        TrailingHitbox th;
        if (trailingHitboxAtCoords(current_coords, &th) && th.id != e->id) return false;
        */

        current_tile = getTileType(current_coords);
        if (current_tile == TILE_TYPE_NONE) return true;
        if (current_tile == TILE_TYPE_GRID || current_tile == TILE_TYPE_WALL || current_tile == TILE_TYPE_LADDER ) return false;
    }
    return false; // only here if hit the max entity push count
}

bool canPushVertical(Int3 coords, Direction direction)
{
    int32 stack_size = getPushableStackSize(coords, direction);
    Int3 check_coords = coords;
    FOR(_, stack_size) check_coords = getNextCoords(check_coords, direction);
    TileType type_after_stack = getTileType(check_coords);
    if (type_after_stack == TILE_TYPE_NONE) return true;
    else return false;
}

// assumes at least the bottom of the stack is able to be pushed 
void pushAll(Int3 coords, Direction direction, bool on_head, int32 root_entity_id)
{
    Int3 current_coords = coords;
    int32 push_count = 0;
    FOR(push_index, MAX_ENTITY_PUSH_COUNT)
    {
        if (getTileType(current_coords) == TILE_TYPE_NONE) break;
        current_coords = getNextCoords(current_coords, direction);
        push_count++;
    }
    current_coords = getNextCoords(current_coords, oppositeDirection(direction));

    for (int32 inverse_push_index = push_count; inverse_push_index != 0; inverse_push_index--)
    {
        int32 stack_size = getPushableStackSize(current_coords, UP);
        Int3 current_stack_coords = current_coords;
        FOR(stack_index, stack_size)
        {
            Entity* e = getEntityAtCoords(current_stack_coords);
            Int3 next_coords = getNextCoords(e->coords, direction);
            TileType next_type = getTileType(next_coords);

            if (next_type != TILE_TYPE_NONE) break; // this is possible because of the inverse push index seeking. if not none, won't be pushable either, so break.

            createTrailingHitbox(e->id, e->coords, TRAILING_HITBOX_TIME);
            moveEntityInBufferAndState(e, next_coords, e->direction);

            e->moving_direction = direction;
            e->moving_on_head = on_head;
            e->root_entity_id = root_entity_id;
            e->tied_to_pack_and_decoupled = false;

            current_stack_coords = getNextCoords(current_stack_coords, UP);
        }
        current_coords = getNextCoords(current_coords, oppositeDirection(direction));
    }
}

// assumes able to be pushed
void pushVertical(Int3 coords, int32 root_entity_id, Direction direction)
{
    int32 stack_size = getPushableStackSize(coords, direction);
    Int3 current_coords = coords;
    FOR(_, stack_size - 1) current_coords = getNextCoords(current_coords, direction); // iterate to end of stack

    for (int32 inverse_stack_index = stack_size; inverse_stack_index != 0; inverse_stack_index--)
    {
        // iterate down the stack
        Entity* e = getEntityAtCoords(current_coords);
        Int3 next_coords = getNextCoords(current_coords, direction);

        createTrailingHitbox(e->id, e->coords, TRAILING_HITBOX_TIME);
        moveEntityInBufferAndState(e, next_coords, e->direction);

        e->moving_direction = direction;
        e->moving_on_head = root_entity_id == PLAYER_ID ? true : false;
        e->root_entity_id = root_entity_id;
        e->tied_to_pack_and_decoupled = false;

        current_coords = getNextCoords(current_coords, oppositeDirection(direction));
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
    FOR(laser_index, MAX_SOURCE_COUNT * MAX_LASER_TURNS_ALLOWED) temp_state.laser_buffer[laser_index].color = COLOR_NONE;
    temp_state.player_hit_by_red = false;

    // if a source is magenta, create entry in sources as primary of it as both red and blue
    Entity sources_as_primary[256] = {0};
    int32 primary_index = 0;
    FOR(source_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        Entity* s = &world_state.sources[source_index];
        if (s->removed || s->locked) continue;
        if (s->color < COLOR_MAGENTA)
        {
            sources_as_primary[primary_index] = *s; 
            // color is already set correctly
            primary_index++;
        }
        else if (s->color == COLOR_MAGENTA)
        {
            sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index].color = COLOR_RED;
            primary_index++;
            sources_as_primary[primary_index] = *s;
            sources_as_primary[primary_index].color = COLOR_BLUE;
            primary_index++;
        }
    }

    FOR(source_index, MAX_SOURCE_COUNT) // iterate over laser (primary) sources
    {
        Entity* source = &sources_as_primary[source_index];

        Direction current_direction = source->direction;
        Vec3 current_norm_coords = source->position;
        Int3 current_tile_coords = int3FromVec3(current_norm_coords);

        // idea here: mirrors and lasers when pushed can collide with themselves because they take up two tiles while the trailing hitbox is active
        // only mirror and laser ids can be skipped.
        int32 id_to_skip = 0;
        int32 id_to_skip_timer = 0;

        FOR(laser_turn_index, MAX_LASER_TURNS_ALLOWED) // iterate over laser segments
        {
            bool no_more_turns = true;

            LaserBuffer* lb = &temp_state.laser_buffer[source_index * MAX_LASER_TURNS_ALLOWED + laser_turn_index];

            // start of some segment: always move one tile forward from where we are before we start checking for anything
            float laser_source_start_offset = 0.4f;
            if (laser_turn_index == 0) lb->start_coords = vec3Add(source->position, vec3ScalarMultiply(directionToVector(current_direction), laser_source_start_offset));
            else lb->start_coords = current_norm_coords;
            lb->direction = current_direction;
            lb->color = source->color;
            if (laser_turn_index > 0) lb->start_clip_plane = temp_state.laser_buffer[source_index * MAX_LASER_TURNS_ALLOWED + laser_turn_index - 1].end_clip_plane;
            else lb->start_clip_plane = (Vec4){ 0, 0, 0, 1 };
            lb->end_clip_plane = (Vec4){ 0, 0, 0, 1 };

            current_norm_coords = vec3Add(directionToVector(current_direction), current_norm_coords);
            current_tile_coords = int3FromVec3(current_norm_coords);

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

                // stop if oob, and extend the laser for a bit
                if (!intCoordsWithinLevelBounds(current_tile_coords))
                {
                    lb->end_coords = vec3Add(vec3ScalarMultiply(directionToVector(current_direction), 40.0f), current_norm_coords);
                    break;
                }

                TileType types_to_check[2] = { TILE_TYPE_NONE, getTileType(current_tile_coords) }; // trailing hitbox, followed by real type; trailing hitbox intersection takes priority
                TrailingHitbox th = {0};                                                 // but normal collision will still be checked if the trailing hitbox exists but doesn't hit
                if (trailingHitboxAtCoords(current_tile_coords, &th) && th.frames > 0)
                {
                    types_to_check[0] = th.type;
                }

                FOR(check, 2)
                {
                    TileType hit_type = types_to_check[check];
                    if (hit_type == TILE_TYPE_NONE) continue;
                    bool this_is_th = check == 0;

                    if (hit_type == TILE_TYPE_PLAYER)
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
                        if (source->color == COLOR_RED)  temp_state.player_hit_by_red = true;
                        if (source->color == COLOR_BLUE) temp_state.player_hit_by_blue_timer = HIT_BY_BLUE_TIME;

                        advance_tile = false;
                        break;
                    }

                    if (hit_type == TILE_TYPE_MIRROR)
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
                        Vec3 mirror_normal = (Vec3){0};
                        bool backside_clip_plane = false;
                        switch (mirror->mirror_orientation)
                        {
                            case MIRROR_SIDE:
                            {
                                if (current_direction != UP && current_direction != DOWN) // check first because modulo arithmetic assumes 4-way dir
                                {
                                    if (mirror->direction == current_direction) next_laser_direction = (current_direction - NORTH + 1) % 4 + NORTH;
                                    else if (current_direction == (mirror->direction - NORTH + 3) % 4 + NORTH) next_laser_direction = (mirror->direction - NORTH + 2) % 4 + NORTH;
                                    else backside_clip_plane = true;
                                }

                                Direction front_axis = oppositeDirection(mirror->direction);
                                Direction side_axis = (mirror->direction - NORTH + 1) % 4 + NORTH;
                                mirror_normal = vec3Normalize(vec3Add(directionToVector(front_axis), directionToVector(side_axis)));
                                break;
                            }
                            case MIRROR_UP:
                            {
                                if (current_direction == DOWN) next_laser_direction = (mirror->direction - NORTH + 1) % 4 + NORTH;
                                else if (current_direction == UP) backside_clip_plane = true;
                                else if (mirror->direction == (current_direction - NORTH + 1) % 4 + NORTH) next_laser_direction = UP;
                                else if (mirror->direction == (current_direction - NORTH + 3) % 4 + NORTH) backside_clip_plane = true;

                                Direction horizontal_axis = (mirror->direction - NORTH + 1) % 4 + NORTH;
                                mirror_normal = vec3Normalize(vec3Add(directionToVector(horizontal_axis), directionToVector(UP)));
                                break;
                            }
                            case MIRROR_DOWN:
                            {
                                if (current_direction == UP) next_laser_direction = (mirror->direction - NORTH + 1) % 4 + NORTH;
                                else if (current_direction == DOWN) backside_clip_plane = true;
                                else if (mirror->direction == (current_direction - NORTH + 1) % 4 + NORTH) next_laser_direction = DOWN;
                                else if (mirror->direction == (current_direction - NORTH + 3) % 4 + NORTH) backside_clip_plane = true;

                                Direction horizontal_axis = (mirror->direction - NORTH + 1) % 4 + NORTH;
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
                                lb->end_coords = vec3Add(coords_without_offset, vec3ScalarMultiply(directionToVector(current_direction), 1.0f));

                                float origin_offset = -vec3Inner(mirror_normal, mirror->position);
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

                        if (distance_from_mirror_along_axes > 0.35)
                        {
                            // between 0.5 and 0.35, so this hits the 'edge' of the mirror: break the laser; still want to do later calculations to calculate exact coords to end
                            end_here = true;
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

                        lb->end_coords = current_norm_coords;

                        if (!end_here)
                        {
                            id_to_skip = mirror->id;
                            id_to_skip_timer = 2;
                            no_more_turns = false;
                            current_direction = next_laser_direction;
                        }

                        // overwrite old clip plane calculation with new end coords
                        float new_origin_offset = -vec3Inner(mirror_normal, lb->end_coords);
                        lb->end_clip_plane = (Vec4){ mirror_normal.x, mirror_normal.y, mirror_normal.z, new_origin_offset };

                        advance_tile = false;
                        break;
                    }

                    // hit type is something that isn't NONE - do default behaviour
                    {
                        Vec3 coords_without_offset = (Vec3){0};
                        float offset = 0.0f;
                        // if entity there could be a real hit with a passthrough. in any other case, just stop here.
                        if (isEntity(hit_type))
                        {
                            Entity* e = NULL;
                            if (this_is_th) e = getEntityFromId(th.id);
                            else e = getEntityAtCoords(current_tile_coords);

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
                            coords_without_offset = getNormCoordsWithEntityCoordAlongAxis(current_direction, current_norm_coords, vec3FromInt3(current_tile_coords));
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
                    current_tile_coords = int3FromVec3(current_norm_coords);
                }
                else break;
            }

            if (no_more_turns) 
            break;
        }
    }
}

// TEXT INPUT

void updateTextInput(Input *input)
{
    for (int32 chars_typed_index = 0; chars_typed_index < input->text.count; chars_typed_index++)
    {
        uint32 codepoint = input->text.codepoints[chars_typed_index];
        char character = (char)codepoint;
        EditBuffer* buffer = &editor_state.edit_buffer;
        if (character == '\b')
        {
            if (buffer->length > 0) buffer->length--;
            buffer->string[buffer->length] = 0;
        }
        else 
        {
            if (buffer->length < 256) buffer->string[buffer->length++] = character;
        }
    }
}

void initUndoBuffer()
{
    memset(&undo_buffer, 0, sizeof(UndoBuffer));
    memset(undo_buffer.level_change_indices, 0xFF, sizeof(undo_buffer.level_change_indices));
}

// TODO:
// write function takes >10ms, for some reason? maybe fwrite is just really slow, or i should thread it? 
// either way, i'm not writing undo buffer to a file at all right now, because it causes noticeable lag.
//
// before i decide how to handle this, think about what I actually want the experience to be when closing the game. Should you automatically go back to where you were in a level,
// or be spat out in the overworld where that level is? because if so, I could just write the updated undo buffer when entering a new level? still need to fix speed, can't have 10ms hang
// for no reason, but maybe i don't need to be writing to undo buffer on every move...

/*
void writeUndoBufferToFile()
{
    FILE* file = fopen(UNDO_DATA_PATH, "wb");
    if (!file) return;
    fwrite(&undo_buffer, sizeof(UndoBuffer), 1, file);
    fclose(file);
}
void loadUndoBufferFromFile()
{
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

void initializeLevel(char* level_name)
{
    if (level_name == 0) strcpy(world_state.level_name, DEBUG_LEVEL_NAME);
    else strcpy(world_state.level_name, level_name);

    memset(temp_state.laser_buffer, 0, sizeof(temp_state.laser_buffer));

    // memset worldstate to 0 (with persistant level_name, and solved levels)
    char persist_level_name[256] = {0};
    char persist_solved_levels[64][64] = {0};
    strcpy(persist_level_name, world_state.level_name);
    memcpy(persist_solved_levels, world_state.solved_levels, sizeof(persist_solved_levels));

    memset(&world_state, 0, sizeof(WorldState));

    strcpy(world_state.level_name, persist_level_name);
    memcpy(world_state.solved_levels, persist_solved_levels, sizeof(persist_solved_levels));

    if (strcmp(world_state.level_name, "overworld") == 0) in_overworld = true;
    else in_overworld = false;

    // level_name to folder_path to level_path, use to build buffer
    char folder_path[64];
    char level_path[64];
    buildLevelFolderPath(&folder_path, world_state.level_name, false);
    snprintf(level_path, sizeof(level_path), "%s/%s", folder_path, LEVEL_BASE_FILE_NAME);
    FILE* file = fopen(level_path, "rb+");
    loadBufferInfo(file);
    fclose(file);

    // clear entity data
    memset(world_state.boxes, 0, sizeof(world_state.boxes) * ENTITY_TYPES); 

    // rebuild entity array
    Entity* entity_group = 0;
    for (int buffer_index = 0; buffer_index < 2 * level_dim.x*level_dim.y*level_dim.z; buffer_index += 2)
    {
        TileType buffer_contents = world_state.buffer[buffer_index];
        if      (buffer_contents == TILE_TYPE_BOX)          entity_group = world_state.boxes;
        else if (buffer_contents == TILE_TYPE_MIRROR)       entity_group = world_state.mirrors;
        else if (buffer_contents == TILE_TYPE_GLASS)        entity_group = world_state.glass_blocks;
        else if (buffer_contents == TILE_TYPE_WIN_BLOCK)    entity_group = world_state.win_blocks;
        else if (buffer_contents == TILE_TYPE_LOCKED_BLOCK) entity_group = world_state.locked_blocks;
        else if (isSource(buffer_contents))       entity_group = world_state.sources;

        if (entity_group != 0)
        {
            int32 count = 0;
            FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT) if (entity_group[entity_index].in_use && !entity_group[entity_index].removed) count++;

            Entity* e = &entity_group[count];
            e->coords = bufferIndexToCoords(buffer_index);
            e->position = vec3FromInt3(e->coords);
            if (entity_group == world_state.mirrors)
            {
                e->direction = world_state.buffer[buffer_index + 1] % 8;
                e->mirror_orientation = world_state.buffer[buffer_index + 1] / 8;
                e->rotation = directionToRotation(e->direction, e->mirror_orientation);
            }
            else
            {
                e->direction = world_state.buffer[buffer_index + 1];
                e->mirror_orientation = 0;
                e->rotation = directionToRotation(e->direction, e->mirror_orientation);
            }
            e->moving_direction = NO_DIRECTION;
            e->color = getEntityColor(e->coords);
            e->id = count + entityIdOffset(entity_group, e->color);
            e->removed = false;
            e->in_use = true;
            entity_group = 0;
        }
        else if (world_state.buffer[buffer_index] == TILE_TYPE_PLAYER)
        {
            player->coords = bufferIndexToCoords(buffer_index);
            player->position = vec3FromInt3(player->coords);
            player->direction = world_state.buffer[buffer_index + 1];
            player->rotation = directionToRotation(player->direction, MIRROR_SIDE);
            player->moving_direction = NO_DIRECTION;
            player->id = PLAYER_ID;
            player->in_use = true;
        }
        else if (world_state.buffer[buffer_index] == TILE_TYPE_PACK)
        {
            pack->coords = bufferIndexToCoords(buffer_index);
            pack->position = vec3FromInt3(pack->coords);
            pack->direction = world_state.buffer[buffer_index + 1];
            pack->rotation = directionToRotation(pack->direction, MIRROR_SIDE);
            pack->moving_direction = NO_DIRECTION;
            pack->id = PACK_ID;
            pack->in_use = true;
        }
    }

    // load info from files
    file = fopen(level_path, "rb+");
    saved_main_camera = loadCameraInfo(file, false);
    saved_alt_camera = loadCameraInfo(file, true);
    loadSunDirection(file);
    loadWaterInfo(file);
    loadWinBlockPaths(file);
    loadLockedInfoPaths(file);
    fclose(file);

    loadWaterTexture(folder_path);

    loadSolvedLevelsFromFile();

    // use correct camera when entering overworld
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

    camera_screen_offset.x = (int32)(camera.coords.x / OVERWORLD_SCREEN_SIZE_X);
    camera_screen_offset.z = (int32)(camera.coords.z / OVERWORLD_SCREEN_SIZE_Z);
    camera.rotation = buildCameraQuaternion(camera);
    camera_target_plane = player->coords.y;
    if (in_overworld) ow_player_coords_for_offset = player->coords;

    updateLaserBuffer();
}

void recalculateTextStartCoords()
{
    debug_text_start_coords = (Vec2){ 50.0f, game_display.client_height - 50.0f };
    debug_popup_start_coords = (Vec2){ game_display.client_width / 2.0f, 80.0f };
}

void gameInitialize(char* level_name, DisplayInfo display_from_platform)
{   
    game_display = display_from_platform;
    recalculateTextStartCoords();

    initUndoBuffer();

    // read overworld-zero's world state from file on startup, so it's kept in memory. this is used on restart in the overworld.
    initializeLevel("overworld-zero");
    memcpy(&overworld_zero_state, &world_state, sizeof(WorldState));

    initializeLevel(level_name);
}

RendererInfo getRendererInfo()
{
    RendererInfo info = {0};
    info.camera = camera_with_ow_offset;
    info.level_aabb_min = (Vec3){ level_origin.x - 0.5f, level_origin.y - 0.5f, level_origin.z - 0.5f };
    info.level_aabb_max = (Vec3){ level_origin.x + level_dim.x - 0.5f, level_origin.y + level_dim.y - 0.5f, level_origin.z + level_dim.z - 0.5f };
    info.time = (float)global_time;
    info.water_plane_y = water_plane_y;
    info.shader_mode = game_shader_mode;
    info.water_paint_texture = &water_paint_texture;
    info.sun_direction = sun_direction;
    return info;
}

void gameRedraw(DisplayInfo display_from_platform)
{
    if (draw_command_count == 0) return;
    game_display = display_from_platform;
    recalculateTextStartCoords();
    vulkanSubmitFrame(draw_commands, draw_command_count, getRendererInfo());
    vulkanDraw(false);
}

void updateLockedTiles()
{
    FOR(group_index, 4)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* e = &lockable_entity_groups[group_index][entity_index];
            if (findInSolvedLevels(e->unlocked_by) == -1) e->locked = true; 
            else e->locked = false;
        }
    }
    FOR(locked_block_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        Entity* lb = &world_state.locked_blocks[locked_block_index];
        if (!lb->in_use) continue;
        int32 find_result = findInSolvedLevels(lb->unlocked_by);
        if (find_result == INT32_MAX) continue;
        if (find_result != -1 && !lb->removed)
        {
            // locked block to be unlocked
            lb->removed = true;
            if (getTileType(lb->coords) == TILE_TYPE_LOCKED_BLOCK)
            {
                setTileType(TILE_TYPE_NONE, lb->coords);
                setTileDirection(NORTH, lb->coords, 0);
            }
            if (!silence_unlocks_due_to_restart_or_undo) createDebugPopup("something was unlocked!", POPUP_TYPE_NONE);
        }
        else if (find_result == -1 && lb->removed)
        {
            lb->removed = false;
            setTileType(TILE_TYPE_LOCKED_BLOCK, lb->coords);
            setTileDirection(NORTH, lb->coords, 0);
        }
    }

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
    undo_buffer.deltas[pos].old_mirror_orientation = e->mirror_orientation;
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

// NOTE: this function used to take in action_was_reset and action_was_climb as bools. add back reset if want
//       to only interpolate sometimes. i don't remember why climb was a flag, maybe order of interpolations? 
//       see if still need later, if i decide to add back interpolations on undo.
void recordActionForUndo(WorldState* old_state)
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
            if (!e->in_use) continue;
            recordEntityDelta(e);
            entity_count++;
        }
    }

    undo_buffer.headers[header_index].entity_count = (uint8)entity_count;
    undo_buffer.headers[header_index].delta_start_pos = delta_start;
    undo_buffer.headers[header_index].level_changed = false;
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
            if (!e->in_use) continue;
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

void clearMovementState(Entity* e)
{
    e->position = vec3FromInt3(e->coords);
    e->rotation = directionToRotation(e->direction, e->mirror_orientation);
    e->velocity = (Vec3){0};
    e->moving_direction = NO_DIRECTION;
    e->moving_on_head = false;
    e->root_entity_id = 0;
    e->tied_to_pack_and_decoupled = false;
    e->falling = false;
}

void zeroAnimations()
{
    // set all entities velocity to zero, and rotation to equal their direction
    FOR(group_index, 3)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* e = &interactible_entity_groups[group_index][entity_index];
            clearMovementState(e);
        }
    }
    clearMovementState(player);
    clearMovementState(pack);

    memset(&temp_state, 0, sizeof(TemporaryState));
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
        initializeLevel(level_change->from_level);

        // remove from solved levels if the level was just completed
        if (level_change->remove_from_solved)
        {
            removeFromSolvedLevels(level_change->from_level);
            writeSolvedLevelsToFile();
            updateLockedTiles();
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
            setTileType(TILE_TYPE_NONE, e->coords);
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
            e->position = vec3FromInt3(e->coords);
            e->direction = delta->old_direction;
            e->mirror_orientation = delta->old_mirror_orientation;
            if (type == TILE_TYPE_MIRROR) e->rotation = directionToRotation(e->direction, e->mirror_orientation);
            else e->rotation = directionToRotation(e->direction, e->mirror_orientation);
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

void levelChangePrep(char next_level[64], bool write_solved_levels)
{
    bool level_was_just_solved = false;
    if (!in_overworld && findInSolvedLevels(world_state.level_name) == -1 && write_solved_levels)
    {
        addToSolvedLevels(world_state.level_name);
        writeSolvedLevelsToFile();
        updateLockedTiles();
        level_was_just_solved = true;
    }
    
    recordLevelChangeForUndo(world_state.level_name, level_was_just_solved);

    if (strcmp(next_level, "overworld") == 0) in_overworld = true;
    else in_overworld = false;
}

// MOVEMENT

void doStandardMovement(Direction direction, Int3 next_player_coords)
{
    // maybe move stack above the player's head
    Int3 coords_above_player = getNextCoords(player->coords, UP);
    bool do_on_head_movement = false;
    if (isPushable(getTileType(coords_above_player)) && canPush(coords_above_player, direction)) do_on_head_movement = true;
    if (temp_state.player_hit_by_blue_timer > 0) do_on_head_movement = false;
    if (do_on_head_movement) pushAll(coords_above_player, direction, true, PLAYER_ID);

    createTrailingHitbox(PLAYER_ID, player->coords, TRAILING_HITBOX_TIME);
    moveEntityInBufferAndState(player, next_player_coords, player->direction);

    // move pack also if pack is attached
    if (temp_state.pack_attached)
    {
        createTrailingHitbox(PACK_ID, pack->coords, TRAILING_HITBOX_TIME);
        Int3 next_pack_coords = getNextCoords(pack->coords, direction);
        moveEntityInBufferAndState(pack, next_pack_coords, pack->direction);
    }
}

bool canFall(Entity* e)
{
    Int3 coords_below = getNextCoords(e->coords, DOWN);
    TileType type_below = getTileType(coords_below);
    if (type_below != TILE_TYPE_NONE && type_below != TILE_TYPE_VOID) return false;

    TrailingHitbox th;
    if (trailingHitboxAtCoords(coords_below, &th) && !getEntityFromId(th.id)->falling) return false;

    return true;
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

void mimicRotationalOffset(Entity* copied_e, Entity* e)
{
    float rotation_direction_delta = getAngleOfYAxisRotation(directionToRotation(copied_e->direction, copied_e->mirror_orientation), copied_e->rotation);
    Vec4 transform = quaternionFromAxis(vec3FromInt3(AXIS_Y), rotation_direction_delta);

    Vec4 base_rotation = IDENTITY_QUATERNION;
    if (getTileTypeFromId(e->id) == TILE_TYPE_MIRROR) base_rotation = directionToRotation(e->direction, e->mirror_orientation);
    else base_rotation = directionToRotation(e->direction, e->mirror_orientation);

    e->rotation = quaternionMultiply(transform, base_rotation);
}

void interpolateDecoupledTowardsCoords(Entity* e)
{
    float interpolation_distance_per_frame = 0.1f;
    float difference = getSignedComponentAlongDirection(e->moving_direction, vec3Subtract(vec3FromInt3(e->coords), e->position));
    if (difference < interpolation_distance_per_frame)
    {
        clearMovementState(e);
    }
    else
    {
        float sign = (e->moving_direction == NORTH || e->moving_direction == WEST) ? -1.0f : 1.0f;
        e->position = vec3AddFloatAlongDirection(e->moving_direction, sign * interpolation_distance_per_frame, e->position);
        e->velocity = vec3AddFloatAlongDirection(e->moving_direction, sign * interpolation_distance_per_frame, (Vec3){0});
    }
}

// called on failed or half-failed turn to handle on head entities
void revertHeadStackRotation()
{
    int32 reverse_add = (4 + temp_state.pack_turn_state.initial_player_direction - player->direction) % 4;
    Int3 current_coords = getNextCoords(player->coords, UP);
    int32 stack_size = getPushableStackSize(current_coords, UP);

    FOR(_, stack_size)
    {
        Entity* e = getEntityAtCoords(current_coords);
        e->direction = (e->direction + reverse_add - NORTH) % 4 + NORTH;
        current_coords = getNextCoords(current_coords, UP);
    }
}

bool canPlayerMove(MoveType move_type, Direction input_direction)
{
    // general cases (for all move types)

    if (temp_state.allow_movement_timer != 0) return false;
    if (player->removed) return false;

    // get abs(angle) of player current quat -> target quat, and gate on some angle threshold here.
    float difference_in_player_angle = getAngleOfYAxisRotation(player->rotation, directionToRotation(player->direction, MIRROR_SIDE));
    if (fabs(difference_in_player_angle) > TAU * 0.25 * 0.2) return false; // TODO: expose this angle

    // if able to fall then don't allow movement
    bool player_immune_to_fall = false;
    if (temp_state.player_hit_by_red) player_immune_to_fall = true;
    if (player->moving_direction == UP || player->moving_direction == DOWN) player_immune_to_fall = true;
    if (cheating) player_immune_to_fall = true;
    if (canFall(player) && !player_immune_to_fall) return false;

    switch (move_type)
    {
        case MOVE_FORWARD:
        {
            // allow movement if, given acceleration this frame along input direction, we would overshoot.
            float sign = input_direction == NORTH || input_direction == WEST ? -1.0f : 1.0f;
            float speculative_velocity_along_direction = calculateSpeculativeVelocityAlongDirection(input_direction, sign);
            float position_along_direction = getComponentAlongDirection(input_direction, player->position);
            float coords_along_direction = getComponentAlongDirection(input_direction, vec3FromInt3(player->coords));
            if (!wouldOvershoot(speculative_velocity_along_direction, position_along_direction, coords_along_direction, sign)) return false;

            // disallow movement if also moving in some other direction currently - probably just guards against moving while falling
            if (!vec3IsZero(vec3SetComponentAlongDirection(input_direction, player->velocity, 0))) return false;

            // disallow movement forward if climbing UP. likely doesn't actually matter, would just be walking into a ladder
            if (player->moving_direction == UP) return false;

            return true;
        }
        case MOVE_TURN:
        {
            //if (player->falling) return false; TODO: don't think this was required?
            if (player->moving_direction == UP || player->moving_direction == DOWN) return false;
            
            // get difference in position along axis of travel, and gate on some threshold to target
            float difference_in_player_position_along_direction = getComponentAlongDirection(player->direction, vec3Subtract(player->position, vec3FromInt3(player->coords)));
            if (fabs(difference_in_player_position_along_direction) > 0.3) return false; // TODO: expose this threshold

            if (temp_state.pack_attached)
            {
                // check if would cause half-failed case, and if so check if we already had one of those, and if so disallow turn
                // this defeats half the point of how i handle failed case later... but need to know now!
                Int3 orthogonal_coords = getNextCoords(player->coords, oppositeDirection(input_direction));
                TileType orthogonal_type = getTileType(orthogonal_coords);
                bool pack_would_cause_failed_case_orthogonal = orthogonal_type != TILE_TYPE_NONE && (!isEntity(orthogonal_type) || canPush(orthogonal_coords, player->direction));
                if (pack_would_cause_failed_case_orthogonal && temp_state.pack_turn_state.half_failed_turn_timer != 0) return false;

                // NOTE: when adding full-failed turn animation, will need architecture for guarding that animation

                return true;
            }
        }
        case MOVE_BACK:
        {
        }
    }

    return true;
}

void doPhysicsTick()
{
    // pack turn sequence
    if (temp_state.pack_turn_state.pack_intermediate_states_timer > 0)
    {
        int32 total = temp_state.pack_turn_state.turn_total_frames;
        int32 diagonal_trigger = total < TURN_TIME ? total : TURN_TIME;
        int32 orthogonal_trigger = diagonal_trigger - 3;
        if (orthogonal_trigger < 1) orthogonal_trigger = 1;

        bool this_is_diagonal = false;
        bool this_is_orthogonal = false;
        if (temp_state.pack_turn_state.pack_intermediate_states_timer == diagonal_trigger) this_is_diagonal = true;
        if (temp_state.pack_turn_state.pack_intermediate_states_timer == orthogonal_trigger) this_is_orthogonal = true;

        if (this_is_diagonal || this_is_orthogonal)
        {
            Int3 coords_at_turn;
            Direction push_direction;
            if (this_is_diagonal)
            {
                push_direction = oppositeDirection(player->direction);
                coords_at_turn = temp_state.pack_turn_state.pack_intermediate_coords;
            }
            else
            {
                push_direction = temp_state.pack_turn_state.initial_player_direction;
                coords_at_turn = getNextCoords(pack->coords, push_direction);
            }
            TileType type_at_push = getTileType(coords_at_turn);

            bool allow_turn = false;
            bool do_push = false;
            TrailingHitbox th;
            bool blocked_by_th = trailingHitboxAtCoords(coords_at_turn, &th);

            if (!blocked_by_th)
            {
                if (type_at_push == TILE_TYPE_NONE)
                {
                    allow_turn = true;
                }
                else if (isPushable(type_at_push) && canPush(coords_at_turn, push_direction))
                {
                    allow_turn = true;
                    do_push = true;
                }
            }

            if (allow_turn)
            {
                if (do_push)
                {
                    pushAll(coords_at_turn, push_direction, false, PACK_ID);
                    if (this_is_diagonal) temp_state.pack_turn_state.diagonal_push_happened_this_turn = true;
                }
                createTrailingHitbox(PACK_ID, pack->coords, TRAILING_HITBOX_TIME);
                moveEntityInBufferAndState(pack, coords_at_turn, player->direction);
            }
            else
            {
                if (isPushable(getTileType(getNextCoords(player->coords, UP)))) revertHeadStackRotation();
                if (this_is_orthogonal)
                {
                    Int3 start_pack_coords = getNextCoords(player->coords, oppositeDirection(temp_state.pack_turn_state.initial_player_direction));
                    moveEntityInBufferAndState(pack, start_pack_coords, player->direction);
                }
                if (this_is_diagonal || !temp_state.pack_turn_state.diagonal_push_happened_this_turn) popLastUndoAction();
                player->direction = temp_state.pack_turn_state.initial_player_direction;
                pack->direction = player->direction;
                temp_state.pack_turn_state.half_failed_turn_timer = HALF_FAILED_PACK_TURN_COOLDOWN;
                temp_state.pack_turn_state.pack_intermediate_states_timer = 0;
            }
            if (this_is_orthogonal) temp_state.pack_turn_state.diagonal_push_happened_this_turn = false;
        }
        if (temp_state.pack_turn_state.pack_intermediate_states_timer > 0) temp_state.pack_turn_state.pack_intermediate_states_timer--;
    }

    // reset fall_handled for all falling_entities 
    FOR(group_index, 3) FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT) interactible_entity_groups[group_index][entity_index].fall_handled = false;
    player->fall_handled = false;
    pack->fall_handled = false;

    // falling logic
    FOR(group_index, 5)
    {
        bool is_player = (group_index == 3);
        bool is_pack   = (group_index == 4);
        if (is_pack && temp_state.pack_attached) break;

        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            if (entity_index > 0 && group_index > 2) break; // allow continue in player or pack case

            Entity* e;
            if (is_player) e = player;
            else if (is_pack) e = pack;
            else e = &interactible_entity_groups[group_index][entity_index];

            if (!e->in_use) continue;
            if (e->removed) continue;
            if (e->fall_handled) continue; // happens when entity below is removed due to void, so this would look like bottom, even though already handled

            bool want_to_fall = true;
            if (!canFall(e)) want_to_fall = false;
            if (!vec3IsZero(vec3SetComponentAlongDirection(DOWN, vec3Subtract(e->position, vec3FromInt3(e->coords)), 0))) want_to_fall = false; // not horizontally stationary
            if (!want_to_fall && !e->falling) continue;

            // find the real bottom of the stack (to then interate up from)
            Int3 bottom_coords = e->coords;
            while (true)
            {
                Int3 below_coords = getNextCoords(bottom_coords, DOWN);
                if (!isPushable(getTileType(below_coords))) break;
                Entity* below_e = getEntityAtCoords(below_coords);
                if (below_e == 0 || below_e->fall_handled) break;
                bottom_coords = below_coords;
            }

            int32 stack_size_upper_bound = getPushableStackSize(bottom_coords, UP); // is upper bound - could be less than this, if stack wants to be split, or if separate stacks have seemingly merged
            Int3 current_coords = bottom_coords;
            FOR(stack_index, stack_size_upper_bound)
            {
                Entity* e_in_stack = getEntityAtCoords(current_coords);

                if (!e_in_stack) break; // this shouldn't strictly be needed, but upper bound sometimes overshoots on downclimb.
                if (e_in_stack->fall_handled) break; // another fall_handled check: entity above may have fallen such that they now form one stack (from getNextCoords pov), so guard on already fallen this frame
                if (e_in_stack->id == PACK_ID && temp_state.pack_attached && stack_index != 0) break; // stack split because pack should not fall if attached
                if (e_in_stack->moving_direction != NO_DIRECTION) break;

                e_in_stack->fall_handled = true;
                current_coords = getNextCoords(current_coords, UP);

                // calculate test velocity and position if were to fall this frame
                float test_y_velocity = e_in_stack->velocity.y + GRAVITY;
                test_y_velocity = floatMax(test_y_velocity, MIN_FALL_VELOCITY);
                float test_y_position = e_in_stack->position.y + test_y_velocity;

                // if falling and will only fall within current block, just apply that fall and continue
                if (test_y_position > getComponentAlongDirection(DOWN, vec3FromInt3(e_in_stack->coords)))
                {
                    // will only be here if e.falling, because otherwise would immediately be crossing a boundary
                    if (e_in_stack->id == PLAYER_ID)
                    {
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
                        e_in_stack->velocity.y = test_y_velocity;
                        e_in_stack->position.y = test_y_position;
                    }
                    continue;
                }

                // anything here wants to fall across a tile boundary NOTE: red / blue stopping fall relies on calculating landing to true every frame, ard resetting position/velocity/falling to 0.
                bool landing = false;
                if (!canFall(e_in_stack)) landing = true;
                if (undo_press_timer > 0) landing = true;
                if (cheating) landing = true;

                if (e_in_stack->id == PLAYER_ID && temp_state.player_hit_by_red) landing = true;
                else if (temp_state.player_hit_by_blue_timer != 0) landing = true;

                if (landing)
                {
                    e_in_stack->position.y = (float)e_in_stack->coords.y;
                    e_in_stack->velocity.y = 0.0f;
                    e_in_stack->falling = false;

                    if (e_in_stack == player && temp_state.pack_attached)
                    {
                        pack->position.y = (float)pack->coords.y;
                        pack->velocity.y = 0.0f;
                        pack->falling = false;
                    }
                    continue;
                }

                // anything here will complete the fall
                if (e_in_stack->id == PLAYER_ID)
                {
                    if (!temp_state.player_hit_by_red && player->moving_direction == NO_DIRECTION)
                    {
                        createTrailingHitbox(PLAYER_ID, player->coords, FALL_TRAILING_HITBOX_TIME);
                        player->position.y = test_y_position;
                        player->velocity.y = test_y_velocity;
                        Int3 coords_below = getNextCoords(player->coords, DOWN);
                        Int3 coords_above = getNextCoords(player->coords, UP);

                        if (getTileType(coords_below) == TILE_TYPE_VOID)
                        {
                            setTileType(TILE_TYPE_NONE, player->coords);
                            setTileDirection(NO_DIRECTION, player->coords, 0);
                            player->removed = true;
                            if (temp_state.pack_attached)
                            {
                                setTileType(TILE_TYPE_NONE, pack->coords);
                                setTileDirection(NO_DIRECTION, pack->coords, 0);
                                pack->removed = true;
                            }
                            continue;
                        }

                        moveEntityInBufferAndState(player, coords_below, player->direction);

                        // special case: if falling onto block where tile ahead is ladder (pointing right direction), then player catches the ladder and starts climbing down, and stops falling.
                        Int3 coords_ahead_and_below = getNextCoords(coords_below, player->direction);
                        if (getTileType(coords_ahead_and_below) == TILE_TYPE_LADDER && getTileDirection(coords_ahead_and_below) == oppositeDirection(player->direction))
                        {
                            player->falling = false;
                            player->moving_direction = DOWN;

                            Int3 stack_coords = coords_above;
                            int32 stack_on_head_size = getPushableStackSize(coords_above, UP);
                            FOR(stack_on_head_index, stack_on_head_size)
                            {
                                Entity* e_on_head = getEntityAtCoords(stack_coords);
                                createTrailingHitbox(e_on_head->id, e_on_head->coords, FALL_TRAILING_HITBOX_TIME);
                                e_on_head->position = vec3FromInt3(e_on_head->coords);
                                e_on_head->velocity = (Vec3){0};
                                e_on_head->moving_direction = DOWN;
                                e_on_head->moving_on_head = true;
                                e_on_head->root_entity_id = PLAYER_ID;
                                moveEntityInBufferAndState(e_on_head, getNextCoords(stack_coords, DOWN), e_on_head->direction);

                                stack_coords = getNextCoords(stack_coords, UP);
                            }
                        }
                        else
                        {
                            player->falling = true;
                        }

                        if (temp_state.pack_attached)
                        {
                            if (canFall(pack))
                            {
                                createTrailingHitbox(PACK_ID, pack->coords, FALL_TRAILING_HITBOX_TIME);
                                pack->position.y = test_y_position;
                                pack->velocity.y = test_y_velocity;
                                Int3 pack_next_coords = getNextCoords(pack->coords, DOWN);
                                moveEntityInBufferAndState(pack, pack_next_coords, pack->direction);
                            }
                            else
                            {
                                // pack will detach
                                pack->position.y = (float)pack->coords.y;
                                pack->velocity.y = 0;
                                temp_state.pack_attached = false;
                            }
                        }
                    }
                }
                else
                {
                    if (temp_state.player_hit_by_blue_timer == 0)
                    {
                        createTrailingHitbox(e_in_stack->id, e_in_stack->coords, FALL_TRAILING_HITBOX_TIME);
                        Int3 coords_below = getNextCoords(e_in_stack->coords, DOWN);
                        if (getTileType(coords_below) == TILE_TYPE_VOID)
                        {
                            // fell onto void: remove
                            setTileType(TILE_TYPE_NONE, e_in_stack->coords);
                            setTileDirection(NO_DIRECTION, e_in_stack->coords, 0);
                            e_in_stack->removed = true;
                        }
                        else
                        {
                            e_in_stack->position.y = test_y_position;
                            e_in_stack->velocity.y = test_y_velocity;
                            e_in_stack->falling = true;
                            moveEntityInBufferAndState(e_in_stack, coords_below, e_in_stack->direction);
                        }
                    }
                }
            }
        }
    }

    // climb logic
    // NOTE: UP and DOWN climb cases are different enough that they're still in separate scopes. think about if is worth being smarter here, or if this is clearest
    // NOTE: currently not allowing pushing of objects down if player is above pushable object and she is blue. will find out in level design if this is something I want to actually add.
    if (player->moving_direction == UP || player->moving_direction == DOWN)
    {
        float y_coord_difference = getSignedComponentAlongDirection(player->moving_direction, vec3Subtract(vec3FromInt3(player->coords), player->position));
        if (y_coord_difference > CLIMBING_SPEED)
        {
            // keep climbing, already commited to this movement.
            float sign = player->moving_direction == UP ? 1.0f : -1.0f;
            player->position.y += sign * CLIMBING_SPEED;
            player->velocity.y = sign * CLIMBING_SPEED;
            if (temp_state.pack_attached)
            {
                pack->position.y += sign * CLIMBING_SPEED;
                pack->velocity.y = sign * CLIMBING_SPEED;
            }
        }
        else
        {
            if (player->moving_direction == UP)
            {
                Int3 coords_ahead = getNextCoords(player->coords, player->direction);
                TileType type_ahead = getTileType(coords_ahead);

                if (type_ahead == TILE_TYPE_LADDER)
                {
                    // try climb more
                    Int3 coords_above_player = getNextCoords(player->coords, UP);
                    TileType type_above_player = getTileType(coords_above_player);

                    bool climb_up = false;
                    bool player_push_up = false;
                    if (type_above_player == TILE_TYPE_NONE)
                    {
                        climb_up = true;
                    }
                    else if (isPushable(type_above_player) && canPushVertical(coords_above_player, UP))
                    {
                        climb_up = true;
                        player_push_up = true;
                    }

                    if (climb_up)
                    {
                        if (!temp_state.pack_attached)
                        {
                            // check behind player position for pack: if exists, pack should attach, but only if won't instantly detach.
                            Int3 coords_behind = getNextCoords(player->coords, oppositeDirection(player->direction));
                            if (getTileType(coords_behind) == TILE_TYPE_PACK)
                            {
                                // want to attach
                                Int3 next_coords_for_pack_if_attach = getNextCoords(coords_behind, UP);
                                TileType type_of_next_coords = getTileType(next_coords_for_pack_if_attach);
                                if (type_of_next_coords == TILE_TYPE_NONE) temp_state.pack_attached = true;
                                else if (isPushable(type_of_next_coords) && canPushVertical(coords_above_player, UP)) temp_state.pack_attached = true;
                            }
                        }

                        if (temp_state.pack_attached)
                        {
                            bool pack_stays_with_player = false;
                            bool pack_push_up = false;

                            Int3 coords_above_pack = getNextCoords(pack->coords, UP);
                            TileType type_above_pack = getTileType(coords_above_pack);

                            if (type_above_pack == TILE_TYPE_NONE)
                            {
                                pack_stays_with_player = true;
                            }
                            else if (isPushable(type_above_pack) && canPushVertical(coords_above_pack, UP))
                            {
                                pack_stays_with_player = true;
                                pack_push_up = true;
                            }

                            if (pack_stays_with_player)
                            {
                                createTrailingHitbox(PACK_ID, pack->coords, TRAILING_HITBOX_TIME);
                                if (pack_push_up) pushVertical(coords_above_pack, PACK_ID, UP);
                                moveEntityInBufferAndState(pack, coords_above_pack, player->direction);
                                pack->position.y += CLIMBING_SPEED;
                                pack->velocity.y = CLIMBING_SPEED;
                            }
                            else
                            {
                                temp_state.pack_attached = false;
                                pack->position = vec3FromInt3(pack->coords);
                                pack->velocity = (Vec3){0};
                            }
                        }

                        createTrailingHitbox(PLAYER_ID, player->coords, TRAILING_HITBOX_TIME);
                        if (player_push_up) pushVertical(coords_above_player, PLAYER_ID, UP);
                        moveEntityInBufferAndState(player, coords_above_player, player->direction);
                        player->position.y += CLIMBING_SPEED;
                        player->velocity.y = CLIMBING_SPEED;
                    }
                    else // something unpushable above player
                    {
                        player->moving_direction = DOWN;
                    }
                }
                else // some other type ahead
                {
                    bool move_forwards = false;
                    bool push_forwards = false;
                    if (type_ahead == TILE_TYPE_NONE)
                    {
                        move_forwards = true;
                    }
                    else if (isPushable(type_ahead) && canPush(coords_ahead, player->direction)) 
                    {
                        move_forwards = true;
                        push_forwards = true;
                    }

                    if (move_forwards)
                    {
                        if (!temp_state.pack_attached)
                        {
                            Int3 coords_behind_player = getNextCoords(player->coords, oppositeDirection(player->direction));
                            if (getTileType(coords_behind_player) == TILE_TYPE_PACK) temp_state.pack_attached = true;
                        }

                        player->position = vec3FromInt3(player->coords); // normalize y coord
                        player->velocity = (Vec3){0};
                        player->moving_direction = NO_DIRECTION;
                        if (push_forwards) pushAll(coords_ahead, player->direction, false, PLAYER_ID);
                        doStandardMovement(player->direction, coords_ahead);
                    }
                    else // something unpushable ahead
                    {
                        player->moving_direction = DOWN;
                    }
                }
            }
            else // DOWN movement
            {
                Int3 coords_below_player = getNextCoords(player->coords, DOWN);
                TileType type_below_player = getTileType(coords_below_player);

                if (type_below_player == TILE_TYPE_NONE) 
                {
                    // capture head stack
                    Int3 head_stack_bottom = getNextCoords(player->coords, UP);
                    int32 head_stack_size = 0;
                    if (isPushable(getTileType(head_stack_bottom)) && temp_state.player_hit_by_blue_timer == 0) head_stack_size = getPushableStackSize(head_stack_bottom, UP);

                    if (!temp_state.pack_attached)
                    {
                        // check behind player position for pack: if exists, pack should attach, but only if won't instantly detach.
                        Int3 coords_behind = getNextCoords(player->coords, oppositeDirection(player->direction));
                        if (getTileType(coords_behind) == TILE_TYPE_PACK)
                        {
                            // want to attach
                            Int3 next_coords_for_pack_if_attach = getNextCoords(coords_behind, DOWN);
                            if (getTileType(next_coords_for_pack_if_attach) == TILE_TYPE_NONE) temp_state.pack_attached = true;
                        }
                    }

                    if (temp_state.pack_attached)
                    {
                        Int3 coords_below_pack = getNextCoords(pack->coords, DOWN);
                        TileType type_below_pack = getTileType(coords_below_pack);

                        bool pack_stays_with_player = false;
                        if (type_below_pack == TILE_TYPE_NONE) pack_stays_with_player = true;

                        if (pack_stays_with_player)
                        {
                            createTrailingHitbox(PACK_ID, pack->coords, TRAILING_HITBOX_TIME);
                            moveEntityInBufferAndState(pack, coords_below_pack, pack->direction);
                            pack->position.y -= CLIMBING_SPEED;
                            pack->velocity.y = -CLIMBING_SPEED;
                        }
                        else
                        {
                            temp_state.pack_attached = false;
                            pack->position = vec3FromInt3(pack->coords);
                            pack->velocity = (Vec3){0};
                        }
                    }

                    createTrailingHitbox(PLAYER_ID, player->coords, TRAILING_HITBOX_TIME);
                    moveEntityInBufferAndState(player, coords_below_player, player->direction);
                    player->position.y -= CLIMBING_SPEED;
                    player->velocity.y = -CLIMBING_SPEED;

                    // shift head stack down one tile into space player vacated
                    Int3 current_coords = head_stack_bottom;
                    FOR(stack_index, head_stack_size)
                    {
                        Entity* e = getEntityAtCoords(current_coords);
                        createTrailingHitbox(e->id, e->coords, TRAILING_HITBOX_TIME);
                        moveEntityInBufferAndState(e, getNextCoords(e->coords, DOWN), e->direction);
                        e->moving_direction = DOWN;
                        e->moving_on_head = true;
                        e->root_entity_id = PLAYER_ID;
                        e->tied_to_pack_and_decoupled = false;

                        current_coords = getNextCoords(current_coords, UP);
                    }

                    Int3 new_coords_ahead = getNextCoords(player->coords, player->direction);
                    if (getTileType(new_coords_ahead) != TILE_TYPE_LADDER)
                    {
                        // start falling: will be handled next frame
                        player->moving_direction = NO_DIRECTION;
                    }
                }
                else // tile below isn't NONE: land
                {
                    clearMovementState(player);
                    if (temp_state.pack_attached) clearMovementState(pack);
                }
            }
        }
    }

    // update pack attached if relevant
    TileType tile_behind_player = getTileType(getNextCoords(world_state.player.coords, oppositeDirection(world_state.player.direction)));
    if (tile_behind_player == TILE_TYPE_PACK)
    {
        if (!temp_state.pack_attached && (player->moving_direction == UP || player->moving_direction == DOWN || player->falling))
        {
            // vertical movement of player causing pack attach. in this case, will never have hit correct coords yet;
            // instead, this is handled in the climbing case, where there is a clear "transition to next tile" block.
            temp_state.pack_attached = false;
        }
        else if (pack->falling)
        {
            // if pack still falling, pack is still in air, so should not attach. 
            // player is not moving vertically, so will be no problem with opposite movement causing miss.
            temp_state.pack_attached = false;
        }
        else
        {
            // no vertical movement, so player turned such that pack is behind, or pack has fallen behind player and settled. 
            // set attached to true and let later code handle correct pack movement
            temp_state.pack_attached = true;
        }
    }
    else if (temp_state.pack_turn_state.pack_intermediate_states_timer == 0)
    {
        temp_state.pack_attached = false;
    }

    // player movement

    // handle directional movement
    for (Direction direction_index = NORTH; direction_index < UP; direction_index++) 
    {
        // only handle velocity / position if offset from the coords
        Vec3 difference_in_player_position = vec3Subtract(vec3FromInt3(player->coords), player->position);
        float difference_in_position_along_direction = getComponentAlongDirection(direction_index, difference_in_player_position);
        float sign = direction_index == NORTH || direction_index == WEST ? -1.0f : 1.0f;
        if (difference_in_position_along_direction * sign <= 0) continue; // will continue if west picks up a difference in the east direction (and north in south direction)

        float position_along_direction = getComponentAlongDirection(direction_index, player->position);
        float coords_along_direction = getComponentAlongDirection(direction_index, vec3FromInt3(player->coords));
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
            int32 frames_to_stop = (int32)(ceilf(current_speed / PLAYER_MAX_DECELERATION));
            if (frames_to_stop < 1) frames_to_stop = 1;
            float movement_adjustment = distance_error / (float)frames_to_stop;

            float decelerated_speed = current_speed - PLAYER_MAX_DECELERATION;
            if (decelerated_speed < 0) decelerated_speed = 0;

            // move position by velocity + adjustment, then set velocity to decelerated value
            float actual_movement = current_speed + movement_adjustment;

            player->position = vec3AddFloatAlongDirection(direction_index, sign * actual_movement, player->position);
            if (direction_index == NORTH || direction_index == SOUTH) player->velocity.z = sign * decelerated_speed;
            else player->velocity.x = sign * decelerated_speed;
        }
        player->moving_direction = direction_index;
    }
    if (vec3IsZero(player->velocity)) player->moving_direction = NO_DIRECTION;

    // player rotation
    float total_angle = getAngleOfYAxisRotation(player->rotation, directionToRotation(player->direction, MIRROR_SIDE));
    float frame_count = ceilf((float)fabs(total_angle) / MAX_ANGULAR_VELOCITY - 1e-3f);
    if (frame_count <= 1)
    {
        player->rotation = directionToRotation(player->direction, MIRROR_SIDE);
    }
    else
    {
        float step_angle = total_angle / frame_count;
        Vec4 rotation_this_frame = quaternionFromAxis(vec3FromInt3(AXIS_Y), step_angle);
        player->rotation = quaternionMultiply(rotation_this_frame, player->rotation);
    }

    // pack rotation and movement
    bool do_pack_swing = false;
    bool do_pack_swing_without_y = false;
    bool pack_is_stationary = vec4IsEqual(pack->rotation, directionToRotation(pack->direction, MIRROR_SIDE));

    if (temp_state.pack_attached)
    {
        if (temp_state.pack_turn_state.pack_intermediate_states_timer == 0 && pack_is_stationary)
        {
            pack->position = vec3AddFloatAlongDirection(player->direction, player->direction == NORTH || player->direction == WEST ? 1.0f : -1.0f, player->position);
        }
        else
        {
            do_pack_swing = true;
        }
    }
    else if (!pack_is_stationary && !pack->moving_on_head)
    {
        // pack has detached, but should still be rotating: player is falling, and that has caused pack detach. keep rotation and movement, but stay at same y level
        do_pack_swing = true;
        do_pack_swing_without_y = true;
    }

    if (do_pack_swing)
    {
        Vec3 rotated_offset = vec3RotateByQuaternion(vec3FromInt3(AXIS_Z), player->rotation); // AXIS_Z because pack is 0, 0, 1 relative to player 0, 0, 0, when player has no rotation.
        Vec3 new_pack_position = vec3Add(player->position, rotated_offset);
        pack->rotation = player->rotation;
        if (do_pack_swing_without_y) pack->position = vec3SetComponentAlongDirection(UP, new_pack_position, pack->position.y);
        else pack->position = new_pack_position;
    }

    // handle moving entities
    FOR(group_index, 4)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            if (group_index == 3 && (entity_index > 0 || temp_state.pack_attached)) break;

            Entity* e;
            if (group_index == 3) e = pack;
            else e = &interactible_entity_groups[group_index][entity_index];

            if (e->moving_direction == NO_DIRECTION && !e->moving_on_head) continue;

            Entity* root_e = getEntityFromId(e->root_entity_id);
            if (!root_e) continue;

            if (e->moving_direction == NO_DIRECTION || e->moving_direction == DOWN || e->moving_direction == UP)
            {
                if (e->moving_direction == NO_DIRECTION && e->moving_on_head) 
                {
                    mimicRotationalOffset(player, e);
                    if (vec4IsEqual(e->rotation, directionToRotation(e->direction, e->mirror_orientation)))
                    {
                        clearMovementState(e);
                        continue;
                    }
                }

                if (e->moving_direction == NO_DIRECTION)
                {
                    // must be rotation. follow along with player coords still. this is for the case where player is still moving when this rotation happens.
                    // check that player isn't moving into a position where the object can't go before applying this movement
                    Int3 previous_player_coords = getNextCoords(player->coords, oppositeDirection(player->direction));
                    Int3 previous_player_coords_with_moving_entity_y = int3FromVec3(vec3SetComponentAlongDirection(UP, vec3FromInt3(previous_player_coords), (float)e->coords.y));
                    Entity* e_exists_if_no_push = getEntityAtCoords(previous_player_coords_with_moving_entity_y); // if this entity exists, that means push hasn't been allowed to happen
                    if (!(e_exists_if_no_push && e_exists_if_no_push->id == e->id)) // probably don't need the second check, how would there be a different entity in this position?
                    {
                        e->position.x = player->position.x;
                        e->position.z = player->position.z;
                    }
                }
                else
                {
                    // climbing, this object is on head or on pack
                    if (player->moving_direction == NO_DIRECTION)
                    {
                        // reset
                        clearMovementState(e);
                    }
                    else
                    {
                        e->position.x = (float)e->coords.x;
                        e->position.z = (float)e->coords.z;
                        e->position.y = player->position.y + (float)(e->coords.y - player->coords.y);
                    }
                }
            }
            else
            {
                // push
                Vec3 difference_in_root_position = vec3Subtract(root_e->position, vec3FromInt3(root_e->coords));
                float difference_in_root_position_along_direction = getComponentAlongDirection(e->moving_direction, difference_in_root_position);

                if (difference_in_root_position_along_direction != 0)
                {
                    // root entity is still moving

                    Vec3 test_position = vec3AddFloatAlongDirection(e->moving_direction, difference_in_root_position_along_direction, vec3FromInt3(e->coords));
                    float test_movement_towards_direction = getSignedComponentAlongDirection(e->moving_direction, vec3Subtract(test_position, e->position));

                    bool entity_pack_decoupled = e->tied_to_pack_and_decoupled;
                    bool entity_on_head_hit_something = false;

                    if (test_movement_towards_direction > 0.5f)
                    {
                        // moving too quickly
                        if (root_e == pack) entity_pack_decoupled = true; // half-failed turn causes push
                        else entity_on_head_hit_something = true;
                    }
                    else if (test_movement_towards_direction < 0.0f && test_movement_towards_direction >= -0.5f) 
                    {
                        continue; // moving backwards, e.g. snap to player when push triggered, but player hasn't gotten there yet. but not super large diff
                    }
                    else if (test_movement_towards_direction < -0.5f)
                    {
                        if (e->moving_on_head) entity_on_head_hit_something = true;
                    }

                    if (entity_pack_decoupled)
                    {
                        // entity with root pack should disregard what pack is doing
                        bool close_to_target = fabs(getComponentAlongDirection(e->moving_direction, vec3Subtract(e->position, vec3FromInt3(e->coords)))) < 0.1;
                        if (test_movement_towards_direction > 0.0 || close_to_target)
                        {
                            e->tied_to_pack_and_decoupled = true;
                            interpolateDecoupledTowardsCoords(e);
                        }
                    }
                    else if (entity_on_head_hit_something)
                    {
                        // case where object should keep moving, but is offset by one unit because root entity has changed coords, but object on head / on stack will stop here, so isn't pushed by pushAll, but should still continue to end coords
                        test_position = vec3AddFloatAlongDirection(e->moving_direction, 1.0f, test_position);
                        if (getComponentAlongDirection(e->moving_direction, vec3Subtract(test_position, vec3FromInt3(e->coords))) < 0.0f)
                        {
                            e->position = test_position;
                            e->velocity = vec3AddFloatAlongDirection(e->moving_direction, getComponentAlongDirection(e->moving_direction, root_e->velocity), (Vec3){0});
                        }
                        else
                        {
                            clearMovementState(e);
                        }
                    }
                    else
                    {
                        // normal behavior
                        e->position = test_position;
                        e->velocity = vec3AddFloatAlongDirection(e->moving_direction, getComponentAlongDirection(e->moving_direction, root_e->velocity), (Vec3){0});

                        if (e->moving_on_head)
                        {
                            // apply player rotation too, in case player isn't done rotating when this move happens.
                            mimicRotationalOffset(player, e);
                        }
                    }
                }
                else 
                {
                    if (!vec3IsEqual(e->position, vec3FromInt3(e->coords)) && root_e == pack)
                    {
                        // this is pack at rest but entity not, which means this is half-failed turn case: entity should interpolate towards target
                        e->tied_to_pack_and_decoupled = true;
                        interpolateDecoupledTowardsCoords(e);
                    }
                    else
                    {
                        clearMovementState(e);
                    }
                }

                bool clear_entity_from_moving = false;
                if (vec3IsEqual(e->position, vec3FromInt3(e->coords))) clear_entity_from_moving = true;
                //if (e->moving_on_head) clear_entity_from_moving = false; // case already handled above
                if (clear_entity_from_moving) clearMovementState(e);
            }
        }
    }

    // decrement and clear trailing hitboxes 
    FOR(th_index, MAX_TRAILING_HITBOX_COUNT) 
    {
        TrailingHitbox* th = &temp_state.trailing_hitboxes[th_index];
        if (th->frames > 0) th->frames--;
        if (th->frames == 0) memset(&temp_state.trailing_hitboxes[th_index], 0, sizeof(TrailingHitbox));
    }

    // decrement various timers
    if (undo_press_timer > 0) undo_press_timer--;
    if (temp_state.allow_movement_timer > 0) temp_state.allow_movement_timer--;
    if (temp_state.pack_turn_state.half_failed_turn_timer > 0) temp_state.pack_turn_state.half_failed_turn_timer--;
    if (temp_state.player_hit_by_blue_timer > 0) temp_state.player_hit_by_blue_timer--;
}

GameResult gameFrame(double delta_time, Input* input)
{   
    // TEMP: for profiling
    long long frequency;
    long long t_start, t_after_reload, t_after_input, t_after_physics, t_after_saving, t_after_game, t_after_submit, t_after_draw;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&t_start);

    // reload all models changed on disk
    vulkanReloadChangedModels();
    QueryPerformanceCounter(&t_after_reload);

    if (delta_time > 0.1) delta_time = 0.1;
    physics_accumulator += delta_time;

    draw_command_count = 0;

    //////////////////
    // CAMERA INPUT //
    //////////////////

    // camera mouse input
    if (editor_state.editor_mode != EDITOR_MODE_NONE)
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
    if (editor_state.editor_mode != EDITOR_MODE_NONE && editor_state.editor_mode != EDITOR_MODE_SELECT_WRITE)
    {
        Vec3 right_camera_basis, forward_camera_basis;
        cameraBasisFromYaw(camera.yaw, &right_camera_basis, &forward_camera_basis);

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

    // alternative camera: switch modes on tab. defined as meta input, so can move player at same time as tab camera change.
    if (input->keys_held & KEY_TAB && time_until_allow_meta_input == 0 && editor_state.editor_mode == EDITOR_MODE_NONE) 
    {
        if (saved_alt_camera.fov != 0)
        {
            if (camera_mode == MAIN_WAITING || camera_mode == ALT_TO_MAIN) camera_mode = MAIN_TO_ALT;
            else camera_mode = ALT_TO_MAIN;
        }
        time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
    }

    ////////////
    // EDITOR //
    ////////////

    // handle text input first
    if (editor_state.editor_mode == EDITOR_MODE_SELECT_WRITE)
    {
        char (*writing_to_field)[64] = 0;
        Entity* e = getEntityFromId(editor_state.selected_id);
        if      (editor_state.writing_field == WRITING_FIELD_NEXT_LEVEL)  writing_to_field = &e->next_level;
        else if (editor_state.writing_field == WRITING_FIELD_UNLOCKED_BY) writing_to_field = &e->unlocked_by;

        if (input->keys_pressed & KEY_ENTER)
        {
            memset(*writing_to_field, 0, sizeof(*writing_to_field));
            memcpy(*writing_to_field, editor_state.edit_buffer.string, sizeof(*writing_to_field) - 1);

            editor_state.editor_mode = EDITOR_MODE_SELECT;
            editor_state.selected_id = 0;
            editor_state.writing_field = WRITING_FIELD_NONE;
        }
        else if (input->keys_held & KEY_ESCAPE)
        {
            editor_state.editor_mode = EDITOR_MODE_SELECT;
            editor_state.selected_id = 0;
            editor_state.writing_field = WRITING_FIELD_NONE;
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }

        updateTextInput(input);
    }
    else
    {
        memset(&editor_state.edit_buffer, 0, sizeof(editor_state.edit_buffer));
    }

    // MAIN EDITOR MODE FUNCTIONALITY

    if (time_until_allow_meta_input == 0)
    {
        RaycastHit raycast_output = raycastHitCube(camera_with_ow_offset.coords, vec3RotateByQuaternion(vec3Negate(vec3FromInt3(AXIS_Z)), camera_with_ow_offset.rotation), MAX_RAYCAST_SEEK_LENGTH);

        // place / break / rotate tiles
        if (editor_state.editor_mode == EDITOR_MODE_PLACE_BREAK)
        {
            if ((input->keys_held & KEY_LEFT_MOUSE || input->keys_held & KEY_F) && raycast_output.hit) 
            {
                Entity *entity = getEntityAtCoords(raycast_output.hit_coords);
                if (entity != 0)
                {
                    entity->coords = (Int3){0};
                    entity->position = (Vec3){0};
                    entity->removed = true;
                }
                setTileType(TILE_TYPE_NONE, raycast_output.hit_coords);
                setTileDirection(NORTH, raycast_output.hit_coords, 0);

                Int3 level_min, level_max;
                getLevelMinAndMax(&level_min, &level_max);
                if (level_min.x <= level_max.x)
                {
                    // not an empty level
                    Int3 actual_level_dim = {0};
                    actual_level_dim.x = level_max.x - level_min.x + 1;
                    actual_level_dim.z = level_max.z - level_min.z + 1;
                    int32 content_height = level_max.y - level_min.y + 1;
                    actual_level_dim.y = level_dim.y > content_height ? level_dim.y : content_height;
                    reindexBuffer(level_min, actual_level_dim);
                    recalculateWaterPlane();
                }

                time_until_allow_meta_input = PLACE_BREAK_TIME_UNTIL_ALLOW_INPUT;
            }
            else if ((input->keys_held & KEY_RIGHT_MOUSE || input->keys_held & KEY_H) && raycast_output.hit) 
            {
                bool place_allowed = false;
                if (!intCoordsWithinLevelBounds(raycast_output.place_coords))
                {
                    // tile doesn't fit: grow level sizes to include, if possible
                    Int3 new_origin = level_origin;
                    if (raycast_output.place_coords.x < new_origin.x) new_origin.x = raycast_output.place_coords.x;
                    if (raycast_output.place_coords.y < new_origin.y) new_origin.y = raycast_output.place_coords.y;
                    if (raycast_output.place_coords.z < new_origin.z) new_origin.z = raycast_output.place_coords.z;

                    Int3 new_max = int3Subtract(int3Add(level_origin, level_dim), (Int3){ 1,1,1 }); // set new_max to previous old max coord
                    if (raycast_output.place_coords.x > new_max.x) new_max.x = raycast_output.place_coords.x;
                    if (raycast_output.place_coords.y > new_max.y) new_max.y = raycast_output.place_coords.y;
                    if (raycast_output.place_coords.z > new_max.z) new_max.z = raycast_output.place_coords.z;

                    Int3 new_dim = int3Add(int3Subtract(new_max, new_origin), (Int3){ 1,1,1 });
                    place_allowed = reindexBuffer(new_origin, new_dim);
                }
                else
                {
                    place_allowed = true;
                }

                if (place_allowed)
                {
                    if (editor_state.picked_tile == TILE_TYPE_PLAYER) editorPlaceOnlyInstanceOfTile(player, raycast_output.place_coords, TILE_TYPE_PLAYER, PLAYER_ID);
                    else if (editor_state.picked_tile == TILE_TYPE_PACK) editorPlaceOnlyInstanceOfTile(pack, raycast_output.place_coords, TILE_TYPE_PACK, PACK_ID);
                    if (isSource(editor_state.picked_tile)) 
                    {
                        setTileType(editor_state.picked_tile, raycast_output.place_coords); 
                        setTileDirection(NORTH, raycast_output.place_coords, 0);
                        setEntityInstanceInGroup(world_state.sources, raycast_output.place_coords, NORTH, MIRROR_SIDE, getEntityColor(raycast_output.place_coords)); 
                    }
                    else
                    {
                        setTileType(editor_state.picked_tile, raycast_output.place_coords);

                        Entity* entity_group = 0;
                        switch (editor_state.picked_tile)
                        {
                            case TILE_TYPE_BOX:          entity_group = world_state.boxes;         break;
                            case TILE_TYPE_MIRROR:       entity_group = world_state.mirrors;       break;
                            case TILE_TYPE_GLASS:        entity_group = world_state.glass_blocks;  break;
                            case TILE_TYPE_WIN_BLOCK:    entity_group = world_state.win_blocks;    break;
                            case TILE_TYPE_LOCKED_BLOCK: entity_group = world_state.locked_blocks; break;
                            default: entity_group = 0;
                        }
                        if (entity_group != 0) 
                        {
                            setEntityInstanceInGroup(entity_group, raycast_output.place_coords, NORTH, MIRROR_SIDE, COLOR_NONE);
                            setTileDirection(NORTH, raycast_output.place_coords, 0);
                        }
                        else 
                        {
                            setTileDirection(NORTH, raycast_output.place_coords, 0);
                        }
                    }
                    recalculateWaterPlane();
                }
                else
                {
                    createDebugPopup("block placement OOB", POPUP_TYPE_EDITOR_BLOCK_PLACE_OOB);
                }

                time_until_allow_meta_input = PLACE_BREAK_TIME_UNTIL_ALLOW_INPUT;
            }
            else if (input->keys_held & KEY_R && raycast_output.hit)
            {   
                TileType type = getTileType(raycast_output.hit_coords);
                if (type == TILE_TYPE_MIRROR)
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
                        mirror->rotation = directionToRotation(mirror->direction, mirror->mirror_orientation);
                    }
                }
                else if (isEntity(type))
                {
                    Direction direction = getTileDirection(raycast_output.hit_coords);
                    if (direction == DOWN) direction = NORTH;
                    else direction++;
                    setTileDirection(direction, raycast_output.hit_coords, 0);
                    Entity* e = getEntityAtCoords(raycast_output.hit_coords);
                    if (e != 0)
                    {
                        e->direction = direction;
                        e->rotation = directionToRotation(direction, MIRROR_SIDE);
                    }
                }
                else if (type == TILE_TYPE_LADDER)
                {
                    Direction direction = getTileDirection(raycast_output.hit_coords);
                    if (direction == EAST) direction = NORTH;
                    else direction++;
                    setTileDirection(direction, raycast_output.hit_coords, 0);
                }

                time_until_allow_meta_input = PLACE_BREAK_TIME_UNTIL_ALLOW_INPUT;
            }
            else if ((input->keys_held & KEY_MIDDLE_MOUSE || input->keys_held & KEY_G) && raycast_output.hit) 
            {
                editor_state.picked_tile = getTileType(raycast_output.hit_coords);
                time_until_allow_meta_input = PLACE_BREAK_TIME_UNTIL_ALLOW_INPUT;
            }
        }

        // select and edit tile metadata
        if (editor_state.editor_mode == EDITOR_MODE_SELECT)
        {
            if (input->keys_held & KEY_LEFT_MOUSE)
            {
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
                    editor_state.editor_mode = EDITOR_MODE_SELECT_WRITE;
                    editor_state.writing_field = WRITING_FIELD_NEXT_LEVEL;
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                }
                // start writing unlocked by
                else if (input->keys_held & KEY_B)
                {
                    editor_state.editor_mode = EDITOR_MODE_SELECT_WRITE;
                    editor_state.writing_field = WRITING_FIELD_UNLOCKED_BY;
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;

                }
                // go to selected level of block on q press
                else if (input->keys_held & KEY_Q && editor_state.selected_id / ID_OFFSET_WIN_BLOCK * ID_OFFSET_WIN_BLOCK == ID_OFFSET_WIN_BLOCK)
                {
                    Entity* wb = getEntityFromId(editor_state.selected_id);
                    if (wb->next_level[0] != 0)
                    {
                        levelChangePrep(wb->next_level, false);
                        initializeLevel(wb->next_level);
                        writeSolvedLevelsToFile();
                        time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                    }
                }
            }
        }

        // paint onto water texture
        if (editor_state.editor_mode == EDITOR_MODE_WATER_PAINT)
        {
            if (input->mouse_scroll_this_frame != 0)
            {
                editor_state.brush_radius_modifier += input->mouse_scroll_this_frame; // does nothing on most frames
                int32 paint_radius = (int32)(16.0f + editor_state.brush_radius_modifier);
                if (input->mouse_scroll_this_frame != 0) DEBUG_POPUP(POPUP_TYPE_PAINT_BRUSH_RADIUS_CHANGE, "new brush size: %i", paint_radius);
            }
            else if ((input->keys_held & KEY_LEFT_MOUSE || input->keys_held & KEY_F || input->keys_held & KEY_RIGHT_MOUSE || input->keys_held & KEY_H) && camera.pitch < 0)
            {
                int32 paint_radius = (int32)(16.0f + editor_state.brush_radius_modifier);
                int32 erase_radius = (int32)(32.0f + editor_state.brush_radius_modifier);

                float paint_magnitude = 0.1f;
                int32 brush_radius = 0;
                if (input->keys_held & KEY_LEFT_MOUSE || input->keys_held & KEY_F)
                {
                    brush_radius = paint_radius;
                }
                else if (input->keys_held & KEY_RIGHT_MOUSE || input->keys_held & KEY_H) 
                {
                    paint_magnitude = -paint_magnitude;
                    brush_radius = erase_radius;
                }

                Vec3 point_on_plane = cameraLookingAtPointOnPlane(camera_with_ow_offset, water_plane_y); // TODO: have camera be correct, make camera -> camera_without_overworld_offset or something.
                Int2 center = {0};
                center.x = (int32)((point_on_plane.x - level_origin.x + (0.5 / WATER_PAINT_RESOLUTION) + 0.5f) * WATER_PAINT_RESOLUTION);
                center.y = (int32)((point_on_plane.z - level_origin.z + (0.5 / WATER_PAINT_RESOLUTION) + 0.5f) * WATER_PAINT_RESOLUTION);
                Int2 top_left = { center.x - brush_radius, center.y - brush_radius };
                int32 texture_width  = level_dim.x * WATER_PAINT_RESOLUTION;
                int32 texture_height = level_dim.z * WATER_PAINT_RESOLUTION;
                if (texture_width  > WATER_PAINT_MAX_SIDE) texture_width  = WATER_PAINT_MAX_SIDE;
                if (texture_height > WATER_PAINT_MAX_SIDE) texture_height = WATER_PAINT_MAX_SIDE;

                FOR(y_index, 2 * brush_radius - 1)
                {
                    FOR(x_index, 2 * brush_radius - 1)
                    {
                        Int2 draw_pos = { top_left.x + x_index, top_left.y + y_index };
                        if (draw_pos.x < 0 || draw_pos.y < 0 || draw_pos.x >= texture_width || draw_pos.y >= texture_height ) continue;
                        float draw_multiplier = 1.0f;
                        float unnormalized_multiplier = (float)(brush_radius*brush_radius - ((x_index - brush_radius)*(x_index - brush_radius) + (y_index - brush_radius)*(y_index - brush_radius)));
                        if (unnormalized_multiplier <= 0) continue;
                        draw_multiplier = unnormalized_multiplier / (brush_radius*brush_radius);

                        int32 in_array = draw_pos.y * texture_width + draw_pos.x;
                        float speculative_value = (water_paint_texture.values[in_array].r / 255.0f) + (paint_magnitude * draw_multiplier);
                        if      (speculative_value > 1.0f) speculative_value = 1.0f;
                        else if (speculative_value < 0.0f) speculative_value = 0.0f;
                        water_paint_texture.values[in_array].r = (uint8)(speculative_value * 255.0f + 0.5f);
                    }
                }
                water_paint_texture.dirty = true;
            }
            if (input->keys_held & KEY_R)
            {
                // reset
                FOR(i, WATER_PAINT_MAX_SIDE * WATER_PAINT_MAX_SIDE) water_paint_texture.values[i] = (Rgba8){ 0, 0, 0, 255 };

                water_paint_texture.dirty = true;
                createDebugPopup("reset water texture", POPUP_TYPE_NONE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
        }
    }

    // EDITOR KEYBINDS - separate check for time_until_allow_meta_input because might have been modified above, and if so want to skip this
    if (time_until_allow_meta_input == 0 && editor_state.editor_mode != EDITOR_MODE_SELECT_WRITE)
    {
        // editor mode toggle
        if (input->keys_held & KEY_0) 
        {
            editor_state.editor_mode = EDITOR_MODE_NONE;
            createDebugPopup("game mode", POPUP_TYPE_GAMEPLAY_MODE_CHANGE);
        }
        if (input->keys_held & KEY_1) 
        {
            editor_state.editor_mode = EDITOR_MODE_PLACE_BREAK;
            createDebugPopup("place / break mode", POPUP_TYPE_GAMEPLAY_MODE_CHANGE);
        }
        if (input->keys_held & KEY_2) 
        {
            editor_state.editor_mode = EDITOR_MODE_SELECT;
            createDebugPopup("select mode", POPUP_TYPE_GAMEPLAY_MODE_CHANGE);
        }
        if (input->keys_held & KEY_3)
        {
            editor_state.editor_mode = EDITOR_MODE_WATER_PAINT;
            createDebugPopup("paint mode", POPUP_TYPE_GAMEPLAY_MODE_CHANGE);
        }
        if (input->keys_held & KEY_4)
        {
            editor_state.editor_mode = EDITOR_MODE_ENVIRONMENT;
            createDebugPopup("environment mode", POPUP_TYPE_GAMEPLAY_MODE_CHANGE);
        }

        // change shader mode
        if (input->keys_held & KEY_7)
        {
            game_shader_mode = SHADER_MODE_OUTLINE_TEST;
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            createDebugPopup("shader mode: testing outlines", POPUP_TYPE_SHADER_MODE_CHANGE);
        }
        if (input->keys_held & KEY_8)
        {
            game_shader_mode = SHADER_MODE_DEFAULT;
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            createDebugPopup("shader mode: outlines", POPUP_TYPE_SHADER_MODE_CHANGE);
        }

        // toggle cheating
        if (input->keys_held & KEY_9)
        {
            cheating = !cheating;
            if (cheating) createDebugPopup("cheating", POPUP_TYPE_CHEAT_MODE_TOGGLE);
            else createDebugPopup("not cheating", POPUP_TYPE_CHEAT_MODE_TOGGLE);
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }

        if (input->keys_held & KEY_BACKSPACE)
        {
            camera = saved_main_camera;
            camera.rotation = buildCameraQuaternion(camera);
            camera_mode = MAIN_WAITING;
            camera_lerp_t = 0.0f;
            createDebugPopup("returned camera to saved position", POPUP_TYPE_NONE);
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
        }

        // per-mode handling
        // this organisation is kind of bad: already have some 'per mode handling' for select and for painting above, they just happen to do some more 'stuff'
        // first, edge case on either game or place break for gameplay speed stuff

        if (editor_state.editor_mode == EDITOR_MODE_NONE || editor_state.editor_mode == EDITOR_MODE_PLACE_BREAK)
        {
            // speed up / slow down physics tick
            if (input->keys_held & KEY_DOT)
            {
                physics_timestep_multiplier /= 2;
                if (physics_timestep_multiplier < 1.0) physics_timestep_multiplier = 1.0;
                DEBUG_POPUP(POPUP_TYPE_PHYSICS_TIMESTEP_CHANGE, "physics speed decreased: %.4f (%.6fs per tick)", 1.0 / physics_timestep_multiplier, physics_timestep_multiplier * DEFAULT_PHYSICS_TIMESTEP);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
            else if (input->keys_held & KEY_COMMA)
            {
                physics_timestep_multiplier *= 2;
                DEBUG_POPUP(POPUP_TYPE_PHYSICS_TIMESTEP_CHANGE, "physics speed increased: %.4f (%.6fs per tick)", 1.0 / physics_timestep_multiplier, physics_timestep_multiplier * DEFAULT_PHYSICS_TIMESTEP);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // handle step through physics mode
            if (input->keys_held & KEY_6)
            {
                step_mode = !step_mode;
                if (step_mode) createDebugPopup("step through physics on", POPUP_TYPE_STEP_THROUGH_TOGGLE);
                else           createDebugPopup("step through physics off", POPUP_TYPE_STEP_THROUGH_TOGGLE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
            if (input->keys_held & KEY_K)
            {
                step_to_next_tick = true;
                createDebugPopup("stepped to next tick", POPUP_TYPE_NONE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
        }

        if (editor_state.editor_mode == EDITOR_MODE_PLACE_BREAK)
        {
            // toggle wide camera
            if (input->keys_held & KEY_J)
            {
                editor_state.do_wide_camera = !editor_state.do_wide_camera;
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                if (editor_state.do_wide_camera)
                {
                    if (editor_state.editor_mode == EDITOR_MODE_NONE)
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

            // change camera fov for editor
            if (input->keys_held & KEY_N && editor_state.editor_mode != EDITOR_MODE_NONE)
            {
                camera.fov--;
                time_until_allow_meta_input = 4;
            }
            else if (input->keys_held & KEY_B && editor_state.editor_mode != EDITOR_MODE_NONE)
            {
                camera.fov++;
                time_until_allow_meta_input = 4;
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
                DEBUG_POPUP(POPUP_TYPE_NONE, "camera yaw snapped to: %.3f", camera_snap_yaw);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // toggle debug text 
            if (input->keys_held & KEY_Y)
            {
                do_debug_text = !do_debug_text;
                if (do_debug_text) createDebugPopup("debug state visibility on", POPUP_TYPE_DEBUG_STATE_VISIBILITY_TOGGLE);
                else               createDebugPopup("debug state visibility off", POPUP_TYPE_DEBUG_STATE_VISIBILITY_TOGGLE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // toggle camera boundary lines
            if (time_until_allow_meta_input == 0 && input->keys_held & KEY_T && !(editor_state.editor_mode == EDITOR_MODE_SELECT_WRITE))
            {
                draw_level_boundary = !draw_level_boundary;
                if (draw_level_boundary) createDebugPopup("level / camera boundary visibility on", POPUP_TYPE_LEVEL_BOUNDARY_VISIBILITY_TOGGLE);
                else                     createDebugPopup("level / camera boundary visibility off", POPUP_TYPE_LEVEL_BOUNDARY_VISIBILITY_TOGGLE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // toggle drawing trailing hitboxes as cube outlines
            if (input->keys_held & KEY_O)
            {
                draw_trailing_hitboxes = !draw_trailing_hitboxes;
                if (draw_trailing_hitboxes) createDebugPopup("showing trailing hitboxes", POPUP_TYPE_DRAW_TRAILING_HITBOX_TOGGLE);
                else createDebugPopup("not showing trailing hitboxes", POPUP_TYPE_DRAW_TRAILING_HITBOX_TOGGLE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // increment chosen tile
            if (input->keys_held & KEY_L)
            {
                editor_state.picked_tile++;
                if (editor_state.picked_tile == TILE_TYPE_LADDER + 1) editor_state.picked_tile = TILE_TYPE_VOID;
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // clear solved levels
            if (input->keys_held & KEY_M)
            {
                clearSolvedLevels();
                createDebugPopup("solved levels cleared", POPUP_TYPE_NONE);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
        }
        else if (editor_state.editor_mode == EDITOR_MODE_ENVIRONMENT)
        {
            // change light direction
            if (input->keys_held & KEY_L)
            {
                sun_direction = vec3Normalize(vec3RotateByQuaternion(vec3Negate(vec3FromInt3(AXIS_Z)), camera.rotation));
                DEBUG_POPUP(POPUP_TYPE_SUN_DIRECTION_CHANGE, "sun direction set: %.3f, %.3f, %.3f", sun_direction.x, sun_direction.y, sun_direction.z);
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }

            // change y max
            if (input->keys_held & KEY_DOT)
            {
                Int3 new_dim = (Int3){ level_dim.x, level_dim.y + 1, level_dim.z };
                if (reindexBuffer(level_origin, new_dim)) DEBUG_POPUP(POPUP_TYPE_NONE, "level y dim: %i", level_dim.y);
                else DEBUG_POPUP(POPUP_TYPE_LEVEL_Y_CHANGE, "level too large to grow");
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
            else if (input->keys_held & KEY_COMMA)
            {
                Int3 level_min, level_max;
                getLevelMinAndMax(&level_min, &level_max);
                bool empty_level = level_min.x > level_max.x;
                int32 current_top = level_origin.y + level_dim.y - 1;
                bool top_row_empty = empty_level || current_top > level_max.y;
                if (level_dim.y > 1 && top_row_empty)
                {
                    Int3 new_dim = (Int3){ level_dim.x, level_dim.y - 1, level_dim.z };
                    reindexBuffer(level_origin, new_dim);
                    DEBUG_POPUP(POPUP_TYPE_LEVEL_Y_CHANGE, "level y dim: %i", level_dim.y);
                }
                else
                {
                    DEBUG_POPUP(POPUP_TYPE_LEVEL_Y_CHANGE, "can't shrink: there is tile on top row");
                }
                time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            }
        }
    }

    // MISC STUFF BEFORE PHYSICS LOOP

    if (time_until_allow_meta_input == 0 && input->keys_held & KEY_ESCAPE && editor_state.editor_mode != EDITOR_MODE_SELECT_WRITE)
    {
        if (in_overworld)
        {
            // exit game
            return GAME_QUIT;
        }
        else
        {
            // NOTE: used to persist solved levels over level change and game init, but appears unnecessary
            levelChangePrep("overworld", false);
            initializeLevel("overworld");
            time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
            temp_state.allow_movement_timer = 0;
        }
    }

    QueryPerformanceCounter(&t_after_input);

    ///////////////////////
    // MAIN PHYSICS LOOP //
    ///////////////////////

    if (step_mode) 
    {
        if (step_to_next_tick) 
        {
            physics_accumulator = physics_timestep_multiplier * DEFAULT_PHYSICS_TIMESTEP;
            step_to_next_tick = false;
        }
        else physics_accumulator = 0.0;
    }

    while (physics_accumulator >= (physics_timestep_multiplier * DEFAULT_PHYSICS_TIMESTEP))
    {
        debug_text_count = 0;

        // generate keys_pressed from prev_input and input
        input->keys_pressed = input->keys_held & ~prev_input.keys_held;
        prev_input = *input; // note that prev_input is almost always the same as input, it just persists over the frame

        if (editor_state.editor_mode == EDITOR_MODE_NONE)
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
                    if (undos_performed == 0) time_until_allow_undo_or_restart_input = 9;
                    else if (undos_performed < 2) time_until_allow_undo_or_restart_input = 8;
                    else if (undos_performed < 6) time_until_allow_undo_or_restart_input = 7;
                    else time_until_allow_undo_or_restart_input = 6;

                    undos_performed++;
                    silence_unlocks_due_to_restart_or_undo = true;
                    undo_press_timer = time_until_allow_undo_or_restart_input;
                    temp_state.allow_movement_timer = time_until_allow_undo_or_restart_input;
                }
                else
                {
                    time_until_allow_undo_or_restart_input = 8;
                }
            }
            if (time_until_allow_undo_or_restart_input == 0 && input->keys_held & KEY_R)
            {
                // RESTART 
                if (!restart_last_turn) 
                {
                    recordActionForUndo(&world_state);
                }
                createDebugPopup("level restarted", POPUP_TYPE_NONE);
                zeroAnimations();
                Camera save_camera = camera;

                initializeLevel(world_state.level_name);

                if (in_overworld)
                {
                    // copy world state from overworld_zero, but save the solved levels and overwrite the level name
                    char persist_solved_levels[64][64];
                    memcpy(&persist_solved_levels, &world_state.solved_levels, sizeof(char) * 64 * 64);
                    memcpy(&world_state, &overworld_zero_state, sizeof(WorldState));
                    memcpy(&world_state.solved_levels, &persist_solved_levels, sizeof(char) * 64 * 64);
                    memcpy(&world_state.level_name, "overworld", sizeof(char) * 64);

                    moveEntityInBufferAndState(player, overworld_restart_coords, NORTH);
                    player->rotation = directionToRotation(player->direction, MIRROR_SIDE);
                    player->position = vec3FromInt3(player->coords);
                    moveEntityInBufferAndState(pack, getNextCoords(player->coords, SOUTH), NORTH);
                    pack->rotation = directionToRotation(pack->direction, MIRROR_SIDE);
                    pack->position = vec3FromInt3(pack->coords);
                }
                camera = save_camera; 
                restart_last_turn = true;
                silence_unlocks_due_to_restart_or_undo = true;
                time_until_allow_undo_or_restart_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                temp_state.allow_movement_timer = 0;

                updateLaserBuffer();
            }

            // HANDLE INPUT

            //bool wasd_held = input->keys_held & KEY_W || input->keys_held & KEY_A || input->keys_held & KEY_S || input->keys_held & KEY_D;
            bool wasd_pressed = input->keys_pressed & KEY_W || input->keys_pressed & KEY_A || input->keys_pressed & KEY_S || input->keys_pressed & KEY_D;

            if (wasd_pressed)
            {
                Direction input_direction = NO_DIRECTION;
                if      (input->keys_held & KEY_W) input_direction = NORTH; 
                else if (input->keys_held & KEY_A) input_direction = WEST; 
                else if (input->keys_held & KEY_S) input_direction = SOUTH; 
                else if (input->keys_held & KEY_D) input_direction = EAST; 

                MoveType move_type = 0;
                if (input_direction == player->direction) move_type = MOVE_FORWARD;
                else if (input_direction == oppositeDirection(player->direction)) move_type = MOVE_BACK;
                else move_type = MOVE_TURN;

                bool player_can_move = canPlayerMove(move_type, input_direction);

                if (player_can_move && move_type == MOVE_FORWARD)
                {
                    // FORWARD MOVEMENT
                    Int3 next_player_coords = getNextCoords(player->coords, input_direction);

                    // TODO: is this case still handled? it may not return true on the interior in canPlayerMove.
                    if (player->moving_direction == DOWN)
                    {
                        player->moving_direction = UP;
                    }
                    else
                    {
                        bool try_walk = false;
                        bool do_push = false;
                        bool try_climb = false;

                        TileType next_tile = getTileType(next_player_coords);
                        switch (next_tile)
                        {
                            case TILE_TYPE_NONE:
                            {
                                // currently not allowing input if trailing hitbox occupies next tile. this check might be too strict sometimes
                                TrailingHitbox th = {0};
                                if (trailingHitboxAtCoords(next_player_coords, &th)) try_walk = false;
                                else try_walk = true;
                                break;
                            }
                            case TILE_TYPE_BOX:
                            case TILE_TYPE_GLASS:
                            case TILE_TYPE_PACK:
                            case TILE_TYPE_MIRROR:
                            case TILE_TYPE_SOURCE_RED:
                            case TILE_TYPE_SOURCE_BLUE:
                            case TILE_TYPE_SOURCE_MAGENTA:
                            {
                                // figure out if push, pause, or fail here.
                                if (canPush(next_player_coords, input_direction))
                                {
                                    do_push = true;
                                    try_walk = true;
                                }
                                break;
                            }
                            case TILE_TYPE_LADDER:
                            {
                                // currently will slow player down to correct location before allowing the climb (and not allow buffered climb input)
                                bool ladder_facing_player = getTileDirection(next_player_coords) == oppositeDirection(player->direction);
                                bool player_at_correct_location = vec3IsEqual(player->position, vec3FromInt3(player->coords));
                                if (ladder_facing_player && player_at_correct_location) try_climb = true;
                                break;
                            }
                            default:
                            {
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
                            if (tile_below_next_coords == TILE_TYPE_NONE && !temp_state.player_hit_by_red) allow_walk = false;
                            if (isEntity(tile_below_next_coords) && !vec3IsZero(getEntityAtCoords(coords_below_next_coords)->velocity)) allow_walk = false;

                            if (allow_walk || cheating)
                            {
                                recordActionForUndo(&world_state);
                                if (do_push) pushAll(next_player_coords, input_direction, false, PLAYER_ID);
                                doStandardMovement(input_direction, next_player_coords);
                            }
                            else
                            {
                                // leap of faith logic

                                // create snapshot of current state
                                memcpy(&leap_of_faith_world_state_snapshot, &world_state, sizeof(WorldState));
                                memcpy(&leap_of_faith_temp_state_snapshot,  &temp_state,  sizeof(TemporaryState));
                                Input input_snapshot = *input;

                                // commit tentative move
                                if (do_push) pushAll(next_player_coords, input_direction, false, PLAYER_ID);
                                doStandardMovement(input_direction, next_player_coords);

                                memset(input, 0, sizeof(Input));

                                // simulate forward, and check if red
                                bool would_be_red = false;
                                FOR(_, SIMULATE_FORWARD_TICK_COUNT)
                                {
                                    doPhysicsTick();
                                    updateLaserBuffer();
                                    if (temp_state.player_hit_by_red)
                                    {
                                        would_be_red = true;
                                        break;
                                    }
                                }

                                // restore state
                                memcpy(&world_state, &leap_of_faith_world_state_snapshot, sizeof(WorldState));
                                memcpy(&temp_state,  &leap_of_faith_temp_state_snapshot,  sizeof(TemporaryState));
                                *input = input_snapshot;

                                // if became red, perform move
                                if (would_be_red)
                                {
                                    recordActionForUndo(&world_state);
                                    if (do_push) pushAll(next_player_coords, input_direction, false, PLAYER_ID);
                                    doStandardMovement(input_direction, next_player_coords);
                                }
                            }
                        }
                        else if (try_climb)
                        {
                            // only handles setting climbing direction to UP if player wants to climb up. everything else is handled later, 
                            // because i want to keep climbing sometimes, even if there's been no input for it.
                            Int3 coords_above_player = getNextCoords(player->coords, UP);
                            TileType type_above_player = getTileType(coords_above_player);

                            bool do_climb = false; 
                            if (type_above_player == TILE_TYPE_NONE) do_climb = true;
                            else if (isPushable(type_above_player) && canPushVertical(coords_above_player, UP)) do_climb = true;
                            if (do_climb)
                            {
                                recordActionForUndo(&world_state);
                                player->moving_direction = UP;
                            }
                        }
                    }
                }
                else if (player_can_move && move_type == MOVE_TURN)
                {
                    recordActionForUndo(&world_state);

                    Direction initial_player_direction = player->direction;
                    player->direction = input_direction;
                    setTileDirection(player->direction, player->coords, 0);

                    if (temp_state.pack_attached)
                    {
                        float turn_angle = getAngleOfYAxisRotation(player->rotation, directionToRotation(player->direction, MIRROR_SIDE));
                        int32 rotation_frames = (int32)ceilf((float)fabs(turn_angle) / MAX_ANGULAR_VELOCITY - 1e-3f);
                        temp_state.pack_turn_state.pack_intermediate_states_timer = rotation_frames;
                        temp_state.pack_turn_state.turn_total_frames = rotation_frames;
                        temp_state.pack_turn_state.pack_intermediate_coords = getNextCoords(pack->coords, oppositeDirection(input_direction));
                        temp_state.pack_turn_state.initial_player_direction = initial_player_direction;
                    }

                    // if not blue, rotate objects stacked above the player
                    if (temp_state.player_hit_by_blue_timer == 0)
                    {
                        Int3 coords_above = getNextCoords(player->coords, UP);
                        TileType type_above = getTileType(coords_above);

                        int32 stack_size = 0;
                        if (isPushable(type_above)) stack_size = getPushableStackSize(coords_above, UP);

                        // need to add either 1 or -1 to direction of entity being rotated
                        if (stack_size > 0)
                        {
                            int32 direction_add = (4 + player->direction - initial_player_direction) % 4;
                            Int3 current_coords = coords_above;

                            FOR(stack_index, stack_size)
                            {
                                Entity* e = getEntityAtCoords(current_coords);
                                e->direction = (e->direction + direction_add - NORTH) % 4 + NORTH;
                                e->moving_direction = NO_DIRECTION;
                                e->moving_on_head = true;
                                e->root_entity_id = PLAYER_ID;
                                e->tied_to_pack_and_decoupled = false;

                                current_coords = getNextCoords(current_coords, UP);
                            }
                        }
                    }
                }
                else if (player_can_move && move_type == MOVE_BACK)
                {
                    // BACKWARDS MOVEMENT
                    Direction move_direction = oppositeDirection(player->direction);

                    Int3 coords_below = getNextCoords(player->coords, DOWN);
                    TileType type_below = getTileType(coords_below);

                    if (player->moving_direction == UP)
                    {
                        player->moving_direction = DOWN;
                    }
                    else if (type_below == TILE_TYPE_LADDER && getTileDirection(coords_below) == move_direction)
                    {
                        Int3 next_player_coords = getNextCoords(player->coords, move_direction);
                        Int3 next_pack_coords = getNextCoords(pack->coords, move_direction);

                        bool do_push = false;
                        bool allow_down_climb = false;

                        Int3 coords_below_next_player_coords = getNextCoords(next_player_coords, DOWN);
                        if (getTileType(coords_below_next_player_coords) == TILE_TYPE_NONE)
                        {
                            Int3 pushing_coords = (Int3){0};
                            if (temp_state.pack_attached) pushing_coords = next_pack_coords;
                            else pushing_coords = next_player_coords;

                            TileType type_to_push = getTileType(pushing_coords);
                            if (type_to_push == TILE_TYPE_NONE)
                            {
                                allow_down_climb = true;
                            }
                            else if (isPushable(type_to_push) && canPush(pushing_coords, move_direction))
                            {
                                do_push = true;
                                allow_down_climb = true;
                            }
                        }

                        if (allow_down_climb)
                        {
                            recordActionForUndo(&world_state);

                            // just move back. when move is done, player will start to fall for 0 frames. she will be 
                            // caught by the ladder by a special case in the falling logic (set climbing direction to DOWN, etc.)

                            // move pack first, because moving backwards.
                            if (temp_state.pack_attached)
                            {
                                createTrailingHitbox(PACK_ID, pack->coords, TRAILING_HITBOX_TIME);
                                if (do_push) pushAll(next_pack_coords, move_direction, false, PACK_ID);
                                moveEntityInBufferAndState(pack, next_pack_coords, pack->direction);
                            }
                            else
                            {
                                if (do_push) pushAll(next_player_coords, move_direction, false, PLAYER_ID);
                            }

                            // move stuff on head
                            Int3 coords_on_head = getNextCoords(player->coords, UP);
                            TileType type_on_head = getTileType(coords_on_head);
                            if (isPushable(type_on_head) && canPush(coords_on_head, move_direction)) pushAll(coords_on_head, move_direction, true, PLAYER_ID);

                            createTrailingHitbox(PLAYER_ID, player->coords, TRAILING_HITBOX_TIME);
                            moveEntityInBufferAndState(player, next_player_coords, player->direction);

                            // set velocity to 0 for sharper movement
                            player->velocity = (Vec3){0};
                        }
                    }
                }
            }
        }

        updateLaserBuffer();

        // handle all physics that doesn't have to do with player input on this frame
        doPhysicsTick();

        // win block logic
        if (getTileType(getNextCoords(player->coords, DOWN)) == TILE_TYPE_WIN_BLOCK)
        {
            if (input->keys_held & KEY_Q && time_until_allow_meta_input == 0)
            {
                // go to win_block.next_level if conditions are met
                Entity* wb = getEntityAtCoords(getNextCoords(player->coords, DOWN));
                bool do_win_block_usage = true;
                if (editor_state.editor_mode != EDITOR_MODE_NONE) do_win_block_usage = false;
                if (!temp_state.pack_attached) do_win_block_usage = false;
                if (wb->locked) do_win_block_usage = false;
                if (wb->next_level[0] == 0) do_win_block_usage = false; // don't go through if there is no next level here yet

                if (do_win_block_usage)
                {
                    if (in_overworld) 
                    {
                        char folder_path[64] = {0};
                        buildLevelFolderPath(&folder_path, world_state.level_name, false);
                        writeBaseLevelInfo(folder_path);
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

                    zeroAnimations();
                    levelChangePrep(wb->next_level, true);
                    initializeLevel(wb->next_level);
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                }
            }
            else if (input->keys_held & KEY_F && time_until_allow_meta_input == 0)
            {
                // add win_block.next_level to solved_levels. this is a debug bind
                bool solve_level = true;
                if (editor_state.editor_mode != EDITOR_MODE_NONE) solve_level = false;

                if (solve_level)
                {
                    Entity* wb = getEntityAtCoords(getNextCoords(player->coords, DOWN));
                    if (findInSolvedLevels(wb->next_level) == -1)
                    {
                        int32 next_free = nextFreeInSolvedLevels(&world_state.solved_levels);
                        strcpy(world_state.solved_levels[next_free], wb->next_level);
                    }
                    writeSolvedLevelsToFile();
                    createDebugPopup("level solved!", POPUP_TYPE_NONE);
                    time_until_allow_meta_input = STANDARD_TIME_UNTIL_ALLOW_INPUT;
                }
            }
        }

        // MISC STUFF

        // disallow input if player above void / water
        TileType tile_type_below_player = getTileType(getNextCoords(player->coords, DOWN));
        if (tile_type_below_player == TILE_TYPE_VOID || tile_type_below_player == TILE_TYPE_WATER) temp_state.allow_movement_timer = -1;

        // reset undos performed if no longer holding z undos
        if (undos_performed > 0 && !(input->keys_held & KEY_Z)) undos_performed = 0;

        // update overworld player coords for camera offset if player not removed
        if (in_overworld)
        {
            if (!player->removed) ow_player_coords_for_offset = player->coords;
        }
        else 
        {
            ow_player_coords_for_offset = (Int3){0};
        }

        // update restart coords based on current coords of the player, and also update game progress if this is relevant
        if (player->coords.z > 204) 
        {
            overworld_restart_coords = (Int3){ 37, 258, 225 };
        }
        else if (player->coords.z > 189)
        {
            overworld_restart_coords = (Int3){ 58, 258, 197 };
            if (game_progress < WORLD_1) game_progress = WORLD_1;
        }
        else if (player->coords.z > 174) 
        {
            overworld_restart_coords = (Int3){ 58, 258, 188 };
        }
        else
        {
            overworld_restart_coords = (Int3){ 58, 258, 170 };
            if (game_progress < WORLD_2) game_progress = WORLD_2;
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

        // create debug texts
        if (do_debug_text)
        {
            // display level name
            createDebugText(world_state.level_name);

            // level origin and dim
            DEBUG_TEXT("level origin: %i, %i, %i; level dim: %i, %i, %i", level_origin.x, level_origin.y, level_origin.z, level_dim.x, level_dim.y, level_dim.z);

            // player info
            DEBUG_TEXT("player info: coords: %i, %i, %i, pos norm: %.2f, %.2f, %.2f, velocity: %.2f, %.2f, %.2f, falling: %i, move dir: %i",
                player->coords.x, player->coords.y, player->coords.z, player->position.x, player->position.y, player->position.z, player->velocity.x, player->velocity.y, player->velocity.z, player->falling, player->moving_direction);

            // pack info
            DEBUG_TEXT("pack info: coords: %i, %i, %i, pos norm: %.2f, %.2f, %.2f, velocity: %.2f, %.2f, %.2f, attached: %i, timer: %i", 
                pack->coords.x, pack->coords.y, pack->coords.z, pack->position.x, pack->position.y, pack->position.z, pack->velocity.x, pack->velocity.y, pack->velocity.z, temp_state.pack_attached, temp_state.pack_turn_state.pack_intermediate_states_timer);

            // boxes
            Entity box1 = world_state.boxes[0];
            Entity box2 = world_state.boxes[1];
            DEBUG_TEXT("box 1: moving dir: %i, on_head: %i; box 2: moving dir: %i, on_head: %i", box1.moving_direction, box1.moving_on_head, box2.moving_direction, box2.moving_on_head);

            // draw selected id info
            if (editor_state.editor_mode == EDITOR_MODE_SELECT || editor_state.editor_mode == EDITOR_MODE_SELECT_WRITE)
            {
                if (editor_state.selected_id > 0)
                {
                    Entity* e = getEntityFromId(editor_state.selected_id);
                    if (e)
                    {
                        DEBUG_TEXT("    selected id: %d, coords: %d, %d, %d, direction: %i, mirror_orientation: %i", editor_state.selected_id, e->coords.x, e->coords.y, e->coords.z, e->direction, e->mirror_orientation);
                        DEBUG_TEXT("    falling: %i, velocity: %f, %f, %f", e->falling, e->velocity.x, e->velocity.y, e->velocity.z);
                        switch (editor_state.writing_field)
                        {
                            case WRITING_FIELD_NONE:        createDebugText("    writing field: no writing field"); break;
                            case WRITING_FIELD_NEXT_LEVEL:  createDebugText("    writing field: next level");       break;
                            case WRITING_FIELD_UNLOCKED_BY: createDebugText("    writing field: unlocked by");      break;
                        }
                        DEBUG_TEXT("    next_level: %s", e->next_level);
                        DEBUG_TEXT("    unlocked_by: %s", e->unlocked_by);
                    }
                    else
                    {
                        createDebugText("    selected entity does not exist");
                    }
                }
                else
                {
                    createDebugText("    no entity selected");
                }
            }
        }

        physics_accumulator -= physics_timestep_multiplier * DEFAULT_PHYSICS_TIMESTEP;
    }

    QueryPerformanceCounter(&t_after_physics);

    // SAVING STUFF (depends on changed state, so after loop)
    {
        // paths for saving data both to source and to inside build
        char folder_path[64];
        buildLevelFolderPath(&folder_path, world_state.level_name, true);
        char relative_folder_path[64];
        buildLevelFolderPath(&relative_folder_path, world_state.level_name, false);

        // only used if saving in overworld
        char overworld_zero_path[64];
        buildLevelFolderPath(&overworld_zero_path, OVERWORLD_ZERO_NAME, true);
        char overworld_zero_relative_path[64];
        buildLevelFolderPath(&overworld_zero_relative_path, OVERWORLD_ZERO_NAME, false);

        // create paths to .level from folder
        char level_path[64];
        snprintf(level_path, sizeof(level_path), "%s/%s", folder_path, LEVEL_BASE_FILE_NAME);
        char relative_level_path[64];
        snprintf(relative_level_path, sizeof(relative_level_path), "%s/%s", relative_folder_path, LEVEL_BASE_FILE_NAME);

        // write camera to file on c press, alternative camera on v press
        if (time_until_allow_meta_input == 0 && editor_state.editor_mode == EDITOR_MODE_PLACE_BREAK && (input->keys_held & KEY_C || input->keys_held & KEY_V))
        {
            char tag[4] = {0};
            bool write_alt_camera = false;
            if (input->keys_held & KEY_C) 
            {
                memcpy(&tag, &MAIN_CAMERA_CHUNK_TAG, sizeof(tag));
                createDebugPopup("main camera saved", POPUP_TYPE_MAIN_CAMERA_SAVE);
            }
            else                    
            {
                memcpy(&tag, &ALT_CAMERA_CHUNK_TAG, sizeof(tag));
                createDebugPopup("alt camera saved", POPUP_TYPE_ALT_CAMERA_SAVE);
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

        // clear alt camera on x
        if (time_until_allow_meta_input == 0 && editor_state.editor_mode != EDITOR_MODE_SELECT_WRITE && input->keys_held & KEY_X) 
        {
            if (saved_alt_camera.fov > 0) // if there is a saved alt camera
            {
                memset(&saved_alt_camera, 0, sizeof(Camera));
                camera = saved_main_camera;
                camera.rotation = buildCameraQuaternion(camera);

                Camera empty_camera = {0};
                {
                    FILE* file = fopen(level_path, "rb+");
                    int32 positions[64] = {0};
                    getCountAndPositionOfChunk(file, ALT_CAMERA_CHUNK_TAG, positions);
                    fseek(file, positions[0], SEEK_SET);
                    writeCameraToFile(file, &empty_camera, true);
                    fclose(file);
                }
                {
                    FILE* file = fopen(relative_level_path, "rb+");
                    int32 positions[64] = {0};
                    getCountAndPositionOfChunk(file, ALT_CAMERA_CHUNK_TAG, positions);
                    fseek(file, positions[0], SEEK_SET);
                    writeCameraToFile(file, &empty_camera, true);
                    fclose(file);
                }
            }
        }

        // write base.level and water.texture to file on i press
        if (time_until_allow_meta_input == 0 && editor_state.editor_mode != EDITOR_MODE_SELECT_WRITE && input->keys_held & KEY_I) 
        {
            writeBaseLevelInfo(folder_path);
            writeBaseLevelInfo(relative_folder_path);
            writeWaterTexture(folder_path);
            writeWaterTexture(relative_folder_path);
            if (in_overworld)
            {
                writeBaseLevelInfo(overworld_zero_path);
                writeBaseLevelInfo(overworld_zero_relative_path);
                writeWaterTexture(overworld_zero_path);
                writeWaterTexture(overworld_zero_relative_path);

                // overwrite overworld_zero's world state with the new saved one
                memcpy(&overworld_zero_state, &world_state, sizeof(WorldState));
            }
            createDebugPopup("level saved", POPUP_TYPE_LEVEL_SAVE);
            writeSolvedLevelsToFile();
        }
    }

    QueryPerformanceCounter(&t_after_saving);

    // update camera for drawing. after loop because depends on in_overworld
    camera_with_ow_offset = camera;
    if (in_overworld)
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

    // final update laser buffer call
    updateLaserBuffer();

    // draw lasers
    FOR(laser_buffer_index, MAX_SOURCE_COUNT * MAX_LASER_TURNS_ALLOWED)
    {
        LaserBuffer lb = temp_state.laser_buffer[laser_buffer_index];
        if (lb.color == COLOR_NONE) continue; // laser buffer is not dense - this is check that there is actually something here

        Vec3 diff = vec3Subtract(lb.end_coords, lb.start_coords);
        Vec3 center = vec3Add(lb.start_coords, vec3ScalarMultiply(diff, 0.5));

        float length = vec3Length(diff);
        Vec3 scale = { LASER_WIDTH, LASER_WIDTH, length };
        Vec4 rotation = directionToRotation(lb.direction, MIRROR_SIDE);

        Vec3 color_without_alpha = {0};
        switch (lb.color)
        {
            case COLOR_RED:     color_without_alpha = (Vec3){ 1.0f, 0.0f, 0.0f }; break;
            case COLOR_BLUE:    color_without_alpha = (Vec3){ 0.0f, 0.0f, 1.0f }; break;
            case COLOR_MAGENTA: color_without_alpha = (Vec3){ 1.0f, 0.0f, 1.0f }; break;
            default: break;
        }
        float alpha = 1.0f;
        Vec4 color_with_alpha = { color_without_alpha.x, color_without_alpha.y, color_without_alpha.z, alpha };

        drawAsset(0, LASER, center, scale, rotation, color_with_alpha, lb.start_clip_plane, lb.end_clip_plane); // the model doesnt matter
    }

    // TODO: store static tiles at level entry (and on editor place/break), loop through that array on all other frames
    // draw models
    for (int tile_index = 0; tile_index < 2 * level_dim.x*level_dim.y*level_dim.z; tile_index += 2)
    {
        TileType draw_tile = world_state.buffer[tile_index];
        if (draw_tile == TILE_TYPE_NONE) continue;
        if (isEntity(draw_tile))
        {
            Entity* e = getEntityAtCoords(bufferIndexToCoords(tile_index));
            if (e->locked) draw_tile = TILE_TYPE_LOCKED_BLOCK;
            if (draw_tile == TILE_TYPE_LOCKED_BLOCK)
            {
                drawAsset(CUBE_3D_LOCKED_BLOCK, CUBE_3D, vec3FromInt3(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToRotation(world_state.buffer[tile_index + 1], MIRROR_SIDE), (Vec4){0}, (Vec4){0}, (Vec4){0});
            }
            if (draw_tile == TILE_TYPE_PLAYER)
            {
                Vec4 player_color = { (float)temp_state.player_hit_by_red, 0.0f, (float)(temp_state.player_hit_by_blue_timer > 0) };
                drawAsset(MODEL_3D_PLAYER, MODEL_3D, player->position, DEFAULT_SCALE, player->rotation, player_color, (Vec4){0}, (Vec4){0});
            }
            else
            {
                drawAsset(getModelId(draw_tile), MODEL_3D, e->position, DEFAULT_SCALE, e->rotation, (Vec4){0}, (Vec4){0}, (Vec4){0});
            }
        }
        else
        {
            if (getCube3DId(draw_tile) == CUBE_3D_WATER) drawAsset(MODEL_3D_WATER, WATER_3D, vec3FromInt3(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToRotation(world_state.buffer[tile_index + 1], MIRROR_SIDE), (Vec4){0}, (Vec4){0}, (Vec4){0});
            drawAsset(getCube3DId(draw_tile), CUBE_3D, vec3FromInt3(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToRotation(world_state.buffer[tile_index + 1], MIRROR_SIDE), (Vec4){0}, (Vec4){0}, (Vec4){0});
        }
    }

    // draw selected entity
    if (editor_state.selected_id >= 0 && (editor_state.editor_mode == EDITOR_MODE_SELECT || editor_state.editor_mode == EDITOR_MODE_SELECT_WRITE))
    {
        SpriteId selected_id = getModelId(getTileTypeFromId(editor_state.selected_id));
        Entity* selected_e = 0;
        if (editor_state.selected_id > 0) selected_e = getEntityFromId(editor_state.selected_id);
        if (selected_e) drawAsset(selected_id, OUTLINE_3D, selected_e->position, DEFAULT_SCALE, selected_e->rotation, (Vec4){0}, (Vec4){0}, (Vec4){0});
    }

    // draw camera boundary lines
    if (draw_level_boundary)
    {
        if (in_overworld)
        {
            // draw camera screen lines NOTE: if/when levels want to have multiple screens, will want to do this for levels also
            int32 x_wall_length = ((level_dim.z - 2) / OVERWORLD_SCREEN_SIZE_Z + 2) * OVERWORLD_SCREEN_SIZE_Z; // constant x: depends on z len
            int32 z_wall_length = ((level_dim.x - 2) / OVERWORLD_SCREEN_SIZE_X + 2) * OVERWORLD_SCREEN_SIZE_X; // constant z: depends on x len

            int32 start_coords_x = OVERWORLD_CAMERA_CENTER_START.x - (OVERWORLD_SCREEN_SIZE_X / 2);
            while (start_coords_x > level_origin.x) start_coords_x -= OVERWORLD_SCREEN_SIZE_X;
            int32 start_coords_z = OVERWORLD_CAMERA_CENTER_START.z - (OVERWORLD_SCREEN_SIZE_Z / 2);
            while (start_coords_z > level_origin.z) start_coords_z -= OVERWORLD_SCREEN_SIZE_Z;
            float y = (float)(level_origin.y + level_dim.y / 2);

            // walls with internal constant x
            Vec3 x_wall_scale = { 0, (float)level_dim.y, 1.0f };
            FOR(x_wall_x_index, z_wall_length / OVERWORLD_SCREEN_SIZE_X + 1)
            {
                float x = (float)(start_coords_x + x_wall_x_index * OVERWORLD_SCREEN_SIZE_X) - 0.5f;
                FOR(x_wall_z_index, x_wall_length)
                {
                    float z = (float)(start_coords_z + x_wall_z_index);
                    drawAsset(SPRITEID_ASSET_COUNT, OUTLINE_3D, (Vec3){ x, y, z }, x_wall_scale, IDENTITY_QUATERNION, (Vec4){0}, (Vec4){0}, (Vec4){0});
                }
            }

            // walls with internal constant z
            Vec3 z_wall_scale = { 1.0f, (float)level_dim.y, 0 };
            FOR(z_wall_z_index, x_wall_length / OVERWORLD_SCREEN_SIZE_Z + 1)
            {
                float z = (float)(start_coords_z + z_wall_z_index * OVERWORLD_SCREEN_SIZE_Z) - 0.5f;
                FOR(z_wall_x_index, z_wall_length)
                {
                    float x = (float)(start_coords_x + z_wall_x_index);
                    drawAsset(SPRITEID_ASSET_COUNT, OUTLINE_3D, (Vec3){ x, y, z }, z_wall_scale, IDENTITY_QUATERNION, (Vec4){0}, (Vec4){0}, (Vec4){0});
                }
            }
        }

        // draw level boundary
        {
            Vec3 level_origin_as_vec = vec3Subtract(vec3FromInt3(level_origin), (Vec3){ 0.5f, 0.5f, 0.5f } );
            Vec3 level_dim_as_vec = vec3FromInt3(level_dim);
            Vec3 x_draw_coords_near = (Vec3){ level_origin_as_vec.x,                            level_origin_as_vec.y + (level_dim_as_vec.y / 2), level_origin_as_vec.z + (level_dim_as_vec.z / 2) };
            Vec3 x_draw_coords_far  = (Vec3){ level_origin_as_vec.x + level_dim_as_vec.x,       level_origin_as_vec.y + (level_dim_as_vec.y / 2), level_origin_as_vec.z + (level_dim_as_vec.z / 2) };
            Vec3 z_draw_coords_near = (Vec3){ level_origin_as_vec.x + (level_dim_as_vec.x / 2), level_origin_as_vec.y + (level_dim_as_vec.y / 2), level_origin_as_vec.z };
            Vec3 z_draw_coords_far  = (Vec3){ level_origin_as_vec.x + (level_dim_as_vec.x / 2), level_origin_as_vec.y + (level_dim_as_vec.y / 2), level_origin_as_vec.z + level_dim_as_vec.z };
            Vec3 x_draw_scale = (Vec3){ 0, level_dim_as_vec.y, level_dim_as_vec.z };
            Vec3 z_draw_scale = (Vec3){ level_dim_as_vec.x, level_dim_as_vec.y, 0 };

            drawAsset(SPRITEID_ASSET_COUNT, OUTLINE_3D, x_draw_coords_near, x_draw_scale, IDENTITY_QUATERNION, (Vec4){0}, (Vec4){0}, (Vec4){0});
            drawAsset(SPRITEID_ASSET_COUNT, OUTLINE_3D, z_draw_coords_near, z_draw_scale, IDENTITY_QUATERNION, (Vec4){0}, (Vec4){0}, (Vec4){0});
            drawAsset(SPRITEID_ASSET_COUNT, OUTLINE_3D, x_draw_coords_far,  x_draw_scale, IDENTITY_QUATERNION, (Vec4){0}, (Vec4){0}, (Vec4){0});
            drawAsset(SPRITEID_ASSET_COUNT, OUTLINE_3D, z_draw_coords_far,  z_draw_scale, IDENTITY_QUATERNION, (Vec4){0}, (Vec4){0}, (Vec4){0});
        }
    }

    // draw outline around trailing hitboxes
    if (draw_trailing_hitboxes)
    {
        FOR(th_index, MAX_TRAILING_HITBOX_COUNT)
        {
            TrailingHitbox th = temp_state.trailing_hitboxes[th_index];
            if (th.frames == 0) continue;
            drawAsset(SPRITEID_ASSET_COUNT, OUTLINE_3D, vec3FromInt3(th.coords), DEFAULT_SCALE, IDENTITY_QUATERNION, (Vec4){0}, (Vec4){0}, (Vec4){0});
        }
    }

    /////////////
    // DRAW 2D //
    /////////////

    {
        Vec4 color_with_alpha = { 0, 0, 0, 1 }; // using alpha as first channel. 2d assets just use sprite atlas, so not using color.

        // crosshair
        if (editor_state.editor_mode != EDITOR_MODE_NONE)
        {
            Vec3 crosshair_scale = { 35.0f, 35.0f, 0.0f };
            Vec3 center_screen = { ((float)game_display.client_width / 2), ((float)game_display.client_height / 2), 0.0f };
            drawAsset(SPRITE_2D_CROSSHAIR, SPRITE_2D, center_screen, crosshair_scale, IDENTITY_QUATERNION, color_with_alpha, (Vec4){0}, (Vec4){0});
        }

        // picked block
        if (editor_state.editor_mode == EDITOR_MODE_PLACE_BREAK)
        {
            Vec3 picked_block_scale = { 200.0f, 200.0f, 0.0f };
            Vec3 picked_block_coords = { game_display.client_width - (picked_block_scale.x / 2) - 20, (picked_block_scale.y / 2) + 50, 0.0f };
            drawAsset(getSprite2DId(editor_state.picked_tile), SPRITE_2D, picked_block_coords, picked_block_scale, IDENTITY_QUATERNION, color_with_alpha, (Vec4){0}, (Vec4){0});
        }

        // handle decrementing timers which should be consistent across physics timesteps
        timer_accumulator += delta_time;
        global_time += (delta_time / physics_timestep_multiplier);
        while (timer_accumulator >= 1.0/60.0)
        {
            FOR(popup_index, MAX_DEBUG_POPUP_TYPE_COUNT) if (debug_popups[popup_index].frames_left > 0) debug_popups[popup_index].frames_left--;
            if (time_until_allow_meta_input > 0) time_until_allow_meta_input--;
            if (time_until_allow_undo_or_restart_input > 0) time_until_allow_undo_or_restart_input--; // doesn't really need to be consistent across timesteps; could be above

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
        if (editor_state.editor_mode == EDITOR_MODE_SELECT || editor_state.editor_mode == EDITOR_MODE_SELECT_WRITE)
        {
            Vec2 center_screen = { (float)game_display.client_width / 2, (float)game_display.client_height / 2 };
            drawText(editor_state.edit_buffer.string, center_screen, DEFAULT_TEXT_SCALE, 1.0f);
        }

        // draw debug popup
        FOR(popup_index, MAX_DEBUG_POPUP_TYPE_COUNT)
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

    QueryPerformanceCounter(&t_after_game);

    // TEMP: profiling
    static int frame_counter = 0; // TODO: local persist 
    bool do_profiling_output = frame_counter++ >= 60;

    if (do_profiling_output)
    {
        double reload_ms    = 1000.0 * (double)(t_after_reload  - t_start)         / (double)frequency;
        double input_ms     = 1000.0 * (double)(t_after_input   - t_after_reload)  / (double)frequency;
        double physics_ms   = 1000.0 * (double)(t_after_physics - t_after_input)   / (double)frequency;
        double saving_ms    = 1000.0 * (double)(t_after_saving  - t_after_physics) / (double)frequency;
        double game_draw_ms = 1000.0 * (double)(t_after_game    - t_after_saving)  / (double)frequency;

        char game[256];
        snprintf(game, sizeof(game), "GAME:\nreload: %.2f ms\ninput: %.2f ms\nphysics: %.2f ms\nsaving: %.2f ms\ngame draw: %.2f ms\n\n", reload_ms, input_ms, physics_ms, saving_ms, game_draw_ms);
        OutputDebugStringA(game);
    }

    vulkanSubmitFrame(draw_commands, draw_command_count, getRendererInfo());
    QueryPerformanceCounter(&t_after_submit);
    vulkanDraw(do_profiling_output);
    QueryPerformanceCounter(&t_after_draw);

    if (do_profiling_output)
    {
        double game_logic_ms = 1000.0 * (double)(t_after_game   - t_start)        / (double)frequency;
        double submit_ms     = 1000.0 * (double)(t_after_submit - t_after_game)   / (double)frequency;
        double draw_ms       = 1000.0 * (double)(t_after_draw   - t_after_submit) / (double)frequency;

        char overview[256];
        snprintf(overview, sizeof(overview), "OVERVIEW:\ngame: %.2f ms; submit: %.2f ms; draw: %.2f ms; total: %.2f ms\n\n", game_logic_ms, submit_ms, draw_ms, game_logic_ms + submit_ms + draw_ms);
        OutputDebugStringA(overview);

        frame_counter = 0;
    }

    return editor_state.editor_mode == EDITOR_MODE_NONE ? GAME_GAMEPLAY : GAME_EDITOR;
}
