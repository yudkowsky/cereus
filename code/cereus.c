#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> // TODO(spike): also temporary, for sin/cos
#include <stdio.h> // TODO(spike): "temporary", for fopen 

#include <windows.h>
#ifdef VOID
#undef VOID
#endif

#define FOR(i, n) for (int i = 0; i < n; i++)

const int32 SCREEN_WIDTH_PX = 1920; // TODO(spike): get from platform layer
const int32	SCREEN_HEIGHT_PX = 1080;

const float TAU = 6.2831853071f;

const float CAMERA_SENSITIVITY = 0.005f;
const float CAMERA_MOVE_STEP = 0.1f;
const float CAMERA_FOV = 15.0f;
const Vec3 DEFAULT_SCALE = { 1.0f,  1.0f,  1.0f  };
const Vec3 PLAYER_SCALE  = { 0.75f, 0.75f, 0.75f };
const float LASER_WIDTH = 0.25;
const float MAX_RAYCAST_SEEK_LENGTH = 100.0f;

const int32 META_INPUT_TIME_UNTIL_ALLOW = 9;
const int32 MOVE_OR_PUSH_ANIMATION_TIME = 9; // TODO(spike): make this freely editable (want to up this by a few frames to emphasise pushing stacked box mechanics)
const int32 TURN_ANIMATION_TIME = 9; // somewhat hard coded, tied to PUSH_FROM_TURN...
const int32 FALL_ANIMATION_TIME = 8; // hard coded (because acceleration in first fall anim must be constant)
const int32 CLIMB_ANIMATION_TIME = 10;
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
const int32 MAX_TRAILING_HITBOX_COUNT = 16;
const int32 MAX_LEVEL_COUNT = 64;
const int32 MAX_RESET_COUNT = 16;

const Int3 AXIS_X = { 1, 0, 0 };
const Int3 AXIS_Y = { 0, 1, 0 };
const Int3 AXIS_Z = { 0, 0, 1 };
const Vec3 IDENTITY_TRANSLATION = { 0, 0, 0 };
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

const int32 CAMERA_CHUNK_SIZE = 24;
const char CAMERA_CHUNK_TAG[4] = "CMRA";
const int32 WIN_BLOCK_CHUNK_SIZE = 76;
const char WIN_BLOCK_CHUNK_TAG[4] = "WINB";
const int32 LOCKED_INFO_CHUNK_SIZE = 76; // TODO(spike): get rid of this (have dynamic amounts)
const char LOCKED_INFO_CHUNK_TAG[4] = "LOKB";
const int32 RESET_INFO_SINGLE_ENTRY_SIZE = 8;
const char RESET_INFO_CHUNK_TAG[4] = "RESB";

const int32 OVERWORLD_SCREEN_SIZE_X = 15;
const int32 OVERWORLD_SCREEN_SIZE_Z = 15;

const double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0;

const char debug_level_name[64] = "testing";
const char relative_start_level_path_buffer[64] = "data/levels/";
const char source_start_level_path_buffer[64] = "../cereus/data/levels/";
const char solved_level_path[64] = "data/meta/solved-levels.meta";
Int3 level_dim = {0};

Camera camera = {0};
Int3 camera_screen_offset = {0};
const Int3 camera_center_start = { 7, 0, -17 };
bool draw_camera_boundary = false;

AssetToLoad assets_to_load[1024] = {0};

WorldState world_state = {0};
WorldState next_world_state = {0};

UndoBuffer undo_buffer = {0};
bool restart_last_turn = false;
bool pending_undo_record = false;
WorldState pending_undo_snapshot = {0};

Animation animations[32];
int32 time_until_input = 0;
EditorState editor_state = {0};
LaserBuffer laser_buffer[64] = {0};

const Vec2 DEBUG_TEXT_COORDS_START = { 50.0f, 1080.0f - 80.0f };
const float DEBUG_TEXT_Y_DIFF = 40.0f;
Vec2 debug_text_coords = {0}; 

// stuff from worldstate
bool in_overworld = false;
bool pack_detached = false;

bool player_will_fall_next_turn = false;

TrailingHitbox trailing_hitboxes[16];
bool bypass_player_fall;

int32 pack_intermediate_states_timer;
Int3 pack_intermediate_coords;

Int3 pack_hitbox_turning_to_coords;
int32 pack_hitbox_turning_to_timer; // used for blue-not-blue logic

Direction pack_orthogonal_push_direction;
bool do_diagonal_push_on_turn;
bool do_orthogonal_push_on_turn;
bool do_player_and_pack_fall_after_turn;
bool player_hit_by_blue_in_turn;
Int3 entity_to_fall_after_blue_not_blue_turn_coords;
int32 entity_to_fall_after_blue_not_blue_turn_timer;

// ghosts from tp
Int3 player_ghost_coords;
Int3 pack_ghost_coords;
Direction player_ghost_direction;
Direction pack_ghost_direction;

// CAMERA STUFF

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

Vec3 vec3CrossProduct(Vec3 a, Vec3 b)
{
    return (Vec3){ a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}

Vec3 vec3RotateByQuaternion(Vec3 input_vector, Vec4 quaternion)
{
    Vec3 quaternion_vector_part = (Vec3){ quaternion.x, quaternion.y, quaternion.z };
    float quaternion_scalar_part = quaternion.w;
    Vec3 q_cross_v = vec3CrossProduct(quaternion_vector_part, input_vector);
    Vec3 temp_vector = (Vec3){ q_cross_v.x + quaternion_scalar_part * input_vector.x,
    q_cross_v.y + quaternion_scalar_part * input_vector.y,
    q_cross_v.z + quaternion_scalar_part * input_vector.z};
    Vec3 q_cross_t = vec3CrossProduct(quaternion_vector_part, temp_vector);
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

Vec3 vec3Negate(Vec3 coords) 
{
    return (Vec3){ -coords.x, -coords.y, -coords.z }; 
}

Int3 int3Negate(Int3 coords) 
{
    return (Int3){ -coords.x, -coords.y, -coords.z }; 
}

bool vec3IsZero(Vec3 position) 
{
    return (position.x == 0 && position.y == 0 && position.z == 0); 
}

Vec3 vec3Add(Vec3 a, Vec3 b) 
{
    return (Vec3){ a.x+b.x, a.y+b.y, a.z+b.z };
}

Int3 int3Add(Int3 a, Int3 b)
{
    return (Int3){ a.x+b.x, a.y+b.y, a.z+b.z }; 
}

Vec3 vec3Subtract(Vec3 a, Vec3 b) 
{
    return (Vec3){ a.x-b.x, a.y-b.y, a.z-b.z }; 
}

Vec3 vec3Abs(Vec3 a) 
{
    return (Vec3){ (float)fabs(a.x), (float)fabs(a.y), (float)fabs(a.z) }; 
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

float vec3SignedLength(Vec3 v)
{
    float len = vec3Length(v);
    if (vec3IsEqual(vec3Abs(v), v)) return len;
    else return -len;
}

Vec3 vec3Normalize(Vec3 v)
{
    float length_squared = v.x*v.x + v.y*v.y + v.z*v.z;
    if (length_squared <= 1e-8f) return IDENTITY_TRANSLATION; 
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

Direction getTileDirection(Int3 coords) 
{
    return next_world_state.buffer[coordsToBufferIndexDirection(coords)]; 
}

bool isSource(TileType tile) 
{
    return (tile == SOURCE_RED || tile == SOURCE_GREEN || tile == SOURCE_BLUE || tile == SOURCE_MAGENTA || tile == SOURCE_YELLOW || tile == SOURCE_CYAN|| tile == SOURCE_WHITE);
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

Entity* getEntityPointer(Int3 coords)
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
    Entity *entity = getEntityPointer(coords);
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
    Entity *entity = getEntityPointer(coords);
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

        default: return IDENTITY_TRANSLATION;
    }
}

// roll_z only works / is used for 6-dim. otherwise we just decide (it doesn't make sense to ask for diagonals)
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

// FILE I/O

// .level file structure: 
// first 4 bytes:    -,x,y,z of level dimensions
// next x*y*z * 2 (32768 by default) bytes: actual level buffer

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
int32 getCountAndPositionOfChunk(FILE* file, char tag[4], int32 positions[16])
{
	char chunk[4] = {0};
    int32 chunk_size = 0;
    int32 tag_pos = 0;
    int32 count = 0;

    fseek(file, 4 + (level_dim.x*level_dim.y*level_dim.z * 2), SEEK_SET); // go to start of chunking

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

void loadBufferInfo(FILE* file)
{
    // get level dimensions
    fseek(file, 1, SEEK_SET); // skip the first byte
    uint8 x, y, z;
    fread(&x, 1, 1, file);
    fread(&y, 1, 1, file);
    fread(&z, 1, 1, file);
    level_dim.x = x;
    level_dim.y = y;
    level_dim.z = z;

    uint8 buffer[65536]; // just some max level size - not all of this is necessarily copied.
    fread(&buffer, 1, level_dim.x*level_dim.y*level_dim.z * 2, file);

    memcpy(next_world_state.buffer, buffer, level_dim.x*level_dim.y*level_dim.z * 2);
}

Camera loadCameraInfo(FILE* file)
{
    Camera out_camera = {0};

    int32 positions[16] = {0};
    int32 count = getCountAndPositionOfChunk(file, CAMERA_CHUNK_TAG, positions);
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

// TODO(spike): use getCountAndPositionOfChunk
void loadWinBlockPaths(FILE* file)
{
    fseek(file, 4 + (level_dim.x*level_dim.y*level_dim.z * 2), SEEK_SET); // go to start of chunking

    char tag[4] = {0};
    int32 size = 0;

    while (readChunkHeader(file, tag, &size))
    {
        int32 pos = ftell(file);

        if (memcmp(tag, WIN_BLOCK_CHUNK_TAG, 4) == 0 && size == WIN_BLOCK_CHUNK_SIZE)
        {
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
            // continue from end of chunk
            fseek(file, pos + size, SEEK_SET);
        }
        else
        {
            // skip payload of some other chunk
            fseek(file, pos + size, SEEK_SET);
        }
    }
}

// TODO(spike): use getCountAndPositionOfChunk
void loadLockedInfoPaths(FILE* file)
{
    fseek(file, 4 + (level_dim.x*level_dim.y*level_dim.z * 2), SEEK_SET); // go to start of chunking

    char tag[4] = {0};
    int32 size = 0;

    while (readChunkHeader(file, tag, &size))
    {
        int32 pos = ftell(file);

        if (memcmp(tag, LOCKED_INFO_CHUNK_TAG, 4) == 0 && size == LOCKED_INFO_CHUNK_SIZE)
        {
            int32 x, y, z;
            char path[64];
            if (fread(&x, 4, 1, file) != 1) return;
            if (fread(&y, 4, 1, file) != 1) return;
            if (fread(&z, 4, 1, file) != 1) return;
            if (fread(&path, 1, 64, file) != 64) return;
            path[63] = '\0';

            Entity* entity_group[5] = {next_world_state.boxes, next_world_state.mirrors, next_world_state.locked_blocks, next_world_state.glass_blocks, next_world_state.sources};
            FOR(group_index, 5)
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
            // continue from end of chunk
            fseek(file, pos + size, SEEK_SET);
        }
        else
        {
            // skip payload of some other chunk
            fseek(file, pos + size, SEEK_SET);
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
    int32 positions[16] = {0};
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
                Entity* rb = getEntityPointer(rb_coords);
				FOR(reset_entity_index, reset_entity_count)
                {
                    Int3 current_entity_coords = {0};
                    Int3 reset_start_coords = {0};
                    TileType reset_entity_type = 0;
                    Direction reset_entity_direction = NO_DIRECTION;
                    fread(&current_entity_coords.x, 4, 1, file);
                    fread(&current_entity_coords.y, 4, 1, file);
                    fread(&current_entity_coords.z, 4, 1, file);
                    fread(&reset_start_coords.x, 4, 1, file);
                    fread(&reset_start_coords.y, 4, 1, file);
                    fread(&reset_start_coords.z, 4, 1, file);
                    fread(&reset_entity_type, 4, 1, file);
                    fread(&reset_entity_direction, 4, 1, file);

                    Entity* reset_e = getEntityPointer(current_entity_coords);
                    if (reset_e != 0) rb->reset_info[reset_entity_index].id = reset_e->id;
                    else			  rb->reset_info[reset_entity_index].id = -1;
                    rb->reset_info[reset_entity_index].start_coords = reset_start_coords;
                    rb->reset_info[reset_entity_index].start_type = reset_entity_type;
                    rb->reset_info[reset_entity_index].start_direction = reset_entity_direction;
                }
            }
        }
    }
}

void writeBufferToFile(FILE* file)
{
    fwrite(next_world_state.buffer, 1, level_dim.x*level_dim.y*level_dim.z * 2, file);
}

void writeCameraToFile(FILE* file, Camera* in_camera)
{
    fwrite(CAMERA_CHUNK_TAG, 4, 1, file);
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
        Entity* e = getEntityFromId(rb->reset_info[to_reset_index].id);
        if (e != 0)
        {
            // where obj is right now
            fwrite(&e->coords.x, 4, 1, file);
            fwrite(&e->coords.y, 4, 1, file);
            fwrite(&e->coords.z, 4, 1, file);

            if (save_reset_block_state)
            {
                TileType type = getTileType(e->coords);
     
                // where i want obj to be + direction (in this case same because saving this as new reset position)
                fwrite(&e->coords.x, 4, 1, file);
                fwrite(&e->coords.y, 4, 1, file);
                fwrite(&e->coords.z, 4, 1, file);
                fwrite(&type, 4, 1, file);
                fwrite(&e->direction, 4, 1, file);
            }
            else
            {
                // where i want obj to be + direction
                fwrite(&rb->reset_info[to_reset_index].start_coords.x, 4, 1, file);
                fwrite(&rb->reset_info[to_reset_index].start_coords.y, 4, 1, file);
                fwrite(&rb->reset_info[to_reset_index].start_coords.z, 4, 1, file);
                fwrite(&rb->reset_info[to_reset_index].start_type, 4, 1, file);
                fwrite(&rb->reset_info[to_reset_index].start_direction, 4, 1, file);
            }
        }
        else
        {
            Vec3 null_coords = (Vec3){ 0, 1, 0 };
            fwrite(&null_coords.x, 4, 1, file);
            fwrite(&null_coords.y, 4, 1, file);
            fwrite(&null_coords.z, 4, 1, file);
            fwrite(&rb->reset_info[to_reset_index].start_coords.x, 4, 1, file);
            fwrite(&rb->reset_info[to_reset_index].start_coords.y, 4, 1, file);
            fwrite(&rb->reset_info[to_reset_index].start_coords.z, 4, 1, file);
            fwrite(&rb->reset_info[to_reset_index].start_type, 4, 1, file);
            fwrite(&rb->reset_info[to_reset_index].start_direction, 4, 1, file);
        }
    }
}

// doesn't change the camera or reset blocks
bool saveLevelRewrite(char* path, bool save_reset_block_state)
{
    FILE* old_file = fopen(path, "rb+");
    Camera saved_camera = loadCameraInfo(old_file);
    fclose(old_file);

    char temp_path[256] = {0};
    snprintf(temp_path, sizeof(temp_path), "%s.temp", path);

    FILE* file = fopen(temp_path, "wb");

    fseek(file, 1, SEEK_SET);
    uint8 x, y, z;
    x = (uint8)level_dim.x;
    y = (uint8)level_dim.y;
    z = (uint8)level_dim.z;
    fwrite(&x, 1, 1, file);
    fwrite(&y, 1, 1, file);
    fwrite(&z, 1, 1, file);

    writeBufferToFile(file);
    writeCameraToFile(file, &saved_camera);

    FOR(win_block_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        Entity* wb = &next_world_state.win_blocks[win_block_index];
        if (wb->removed) continue;
        if (wb->next_level[0] == '\0') continue;
        writeWinBlockToFile(file, wb);
    }

    Entity* entity_group[5] = {next_world_state.boxes, next_world_state.mirrors, next_world_state.locked_blocks, next_world_state.glass_blocks, next_world_state.sources};
    FOR(group_index, 5)
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

    remove(path);
    if (rename(temp_path, path) != 0) return false;
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
    return (id < SPRITE_2D_COUNT) ? SPRITE_2D : CUBE_3D;
}

SpriteId getSprite2DId(TileType tile)
{
    switch(tile)
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
        case NOT_VOID:     return SPRITE_2D_NOT_VOID;
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
    switch(tile)
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
        case NOT_VOID:     return CUBE_3D_NOT_VOID;
        case WIN_BLOCK:    return CUBE_3D_WIN_BLOCK;
        case LOCKED_BLOCK: return CUBE_3D_LOCKED_BLOCK;
        case RESET_BLOCK:  return CUBE_3D_RESET_BLOCK;
        case LADDER: 	   return CUBE_3D_LADDER;

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

// TODO(spike):
// drawAsset is slow (>1mspt by itself) likely due to cache misses on AssetToDraw (CUBE_3D_*** accessing ~9MB into array)
// when we have actual 3D models, hopefully can cut this size hugely, because we won't have >1000 of the same entity on screen, probably? right now its basically all VOIDs 
void drawAsset(SpriteId id, AssetType type, Vec3 coords, Vec3 scale, Vec4 rotation)
{
    if (id <= 0) return; // should probably just not call like this
    AssetToLoad* a = &assets_to_load[id];
    if (a->instance_count == 0)
    {
        a->type = type;
        a->sprite_id = id;
    }
    int32 index = a->instance_count;
    a->coords[index] = coords;
    a->scale[index] = scale;
    a->rotation[index] = rotation;
    a->instance_count++;
}

// TODO(spike): maybe compact this into above function later
void drawLaser(Vec3 coords, Vec3 scale, Vec4 rotation, Vec3 color)
{
    int32 asset_location = -1;
    FOR(asset_index, 256)
    {
        if (assets_to_load[asset_index].instance_count == 0)
        {
            if (asset_location == -1) asset_location = asset_index;
            continue;
        }
        if (assets_to_load[asset_index].type == LASER)
        {
            asset_location = asset_index;
            break;
        }
    }

    AssetToLoad* a = &assets_to_load[asset_location];
    a->type = LASER;

    int32 index = a->instance_count;
    a->coords[index] = coords;
    a->scale[index] = scale;
    a->rotation[index] = rotation;
    a->color[index] = color;
    a->instance_count++;
}

void drawText(char* string, Vec2 coords, float scale)
{
    float pen_x = coords.x;
    float pen_y = coords.y;
    float aspect = (float)FONT_CELL_WIDTH_PX / (float)FONT_CELL_HEIGHT_PX;
    for (char* pointer = string; *pointer; ++pointer)
    {
        char c = *pointer;
        if (c == '\n')
        {
            pen_x = coords.x;
            pen_y += FONT_CELL_HEIGHT_PX * scale; 
            continue;
        }
        unsigned char uc = (unsigned char)c;
        if (uc < FONT_FIRST_ASCII || uc > FONT_LAST_ASCII) uc = '?';

        SpriteId id = (SpriteId)(SPRITE_2D_FONT_SPACE + ((unsigned char)c - 32));
        Vec3 draw_coords = { pen_x, pen_y, 0};
        Vec3 draw_scale = { scale * aspect, scale, 1};
        drawAsset(id, SPRITE_2D, draw_coords, draw_scale, IDENTITY_QUATERNION);
        pen_x += scale * aspect;
    }
}

void drawDebugText(char* string)
{
    drawText(string, debug_text_coords, DEFAULT_TEXT_SCALE);
    debug_text_coords.y -= DEBUG_TEXT_Y_DIFF;
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

Vec3 rollingAxis(Direction direction)
{
    Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 rolling = intCoordsToNorm(getNextCoords(normCoordsToInt(IDENTITY_TRANSLATION), direction));
    return vec3CrossProduct(up, rolling);
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

void createPackRotationAnimation(Vec3 player_position, Vec3 pack_position, Direction pack_direction, bool clockwise, Vec3* position_to_change, Vec4* rotation_to_change, int32 entity_id)
{
    int32 next_free_array[2] = {0};
    int32* next_free_output = findNextFreeInAnimations(next_free_array, entity_id);
    int32 animation_index = next_free_output[0];
    int32 queue_time = next_free_output[1];

    animations[animation_index].id = entity_id;
    animations[animation_index].frames_left = TURN_ANIMATION_TIME + queue_time; 
    animations[animation_index].rotation_to_change = rotation_to_change;
    animations[animation_index].position_to_change = position_to_change;

    Vec3 pivot_point = player_position;
    Vec3 pivot_to_pack_start = vec3Subtract(pack_position, player_position);
    float d_theta_per_frame = (TAU*0.25f)/(float)TURN_ANIMATION_TIME;
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

    for (int frame_index = 0; frame_index < TURN_ANIMATION_TIME; frame_index++)
    {
        // rotation
        Vec4 quat_prev = directionToQuaternion(oppositeDirection(previous_pack_direction), true);
        Vec4 quat_next = directionToQuaternion(oppositeDirection(pack_direction), true);
        if (quaternionDot(quat_prev, quat_next) < 0.0f) quat_next = quaternionNegate(quat_next); // resolve quat sign issue 
        float param = (float)(frame_index + 1) / (float)(TURN_ANIMATION_TIME);
        animations[animation_index].rotation[TURN_ANIMATION_TIME-(1+frame_index)] 
            = quaternionNormalize(quaternionAdd(quaternionScalarMultiply(quat_prev, 1.0f - param), quaternionScalarMultiply(quat_next, param)));

        // translation
        float theta = angle_sign * (frame_index+1) * d_theta_per_frame;
        Vec4 roll = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), theta);
        Vec3 relative_rotation = vec3RotateByQuaternion(pivot_to_pack_start, roll);
        animations[animation_index].position[TURN_ANIMATION_TIME-(1+frame_index)] = vec3Add(pivot_point, relative_rotation);
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
        createFailedWalkAnimation(intCoordsToNorm(current_coords), intCoordsToNorm(getNextCoords(current_coords, direction)), &getEntityPointer(current_coords)->position_norm, getEntityPointer(current_coords)->id);
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
        Entity* current_entity = getEntityPointer(current_coords);
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

void resetFirstFall(Entity* e)
{
    if (!e->in_motion && getTileType(getNextCoords(e->coords, DOWN)) != NONE) e->first_fall_already_done = false;
}

PushResult canPush(Int3 coords, Direction direction)
{
    Int3 current_coords = coords;
    TileType current_tile = getTileType(current_coords);
    for (int push_index = 0; push_index < MAX_ENTITY_PUSH_COUNT; push_index++) 
    {
        Entity* entity = getEntityPointer(current_coords);
        if (isEntity(current_tile) && entity->locked) return FAILED_PUSH;

        if (entity->in_motion > 0) return PAUSE_PUSH;
        if (isPushable(getTileType(current_coords)) && getTileType(getNextCoords(current_coords, DOWN)) == NONE && !next_world_state.player.hit_by_blue) return PAUSE_PUSH;

        current_coords = getNextCoords(current_coords, direction);
        current_tile = getTileType(current_coords);

        Int3 coords_ahead = getNextCoords(entity->coords, direction);
        if (isPushable(getTileType(coords_ahead)) && getEntityPointer(coords_ahead)->in_motion) return PAUSE_PUSH;
        Int3 coords_below = getNextCoords(entity->coords, DOWN);
        if (isPushable(getTileType(coords_below)) && getEntityPointer(coords_below)->in_motion) return PAUSE_PUSH;
        Int3 coords_below_and_ahead = getNextCoords(getNextCoords(entity->coords, DOWN), direction);
        if (isPushable(getTileType(coords_below_and_ahead)) && getEntityPointer(coords_below_and_ahead)->in_motion) return PAUSE_PUSH;

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

    Entity* entity = getEntityPointer(coords);
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
        Entity* e = getEntityPointer(current_coords);
        setTileType(NONE, current_coords);
        setTileDirection(NORTH, current_coords);
        Int3 coords_above = getNextCoords(current_coords, UP);
        setTileType(tile, coords_above);
        setTileDirection(dir, coords_above);
        e->coords = coords_above;
        createInterpolationAnimation(intCoordsToNorm(current_coords),
                					 intCoordsToNorm(coords_above),
                                     &e->position_norm,
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
    switch (mirror_direction) // N/S/W/E diagonal cases, and U/D all cases
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
    LaserColor laser_color = {0};
    switch (color)
    {
        case RED:     laser_color.red   = true; break;
        case GREEN:   laser_color.green = true; break;
        case BLUE:    laser_color.blue  = true; break;
        case MAGENTA: laser_color.red   = true; laser_color.blue  = true; break;
        case YELLOW:  laser_color.red   = true; laser_color.green = true; break;
        case CYAN:    laser_color.green = true; laser_color.blue  = true; break;
        case WHITE:   laser_color.red   = true; laser_color.green = true; laser_color.blue = true; break;
        default: break;
    }
    return laser_color;
}

int32 findNextFreeInLaserBuffer()
{
    FOR(laser_buffer_index, MAX_PSEUDO_SOURCE_COUNT) if (laser_buffer[laser_buffer_index].color == NO_COLOR) return laser_buffer_index;
    return -1;
}

// TODO(spike): - need guard on offset_magnitude in the mirrors: if too close to edge, don't want to allow reflection
// 				- look if we need to round from position_norm instead of checking if player is turning for some calculations; does this handle first falls vs. other falls correctly..? maybe yes, but should probably fix this anyway
// 				- figure out moving sources and their lasers
// 				- two mirrors moving at once isnt handled.

void updateLaserBuffer(void)
{
    Entity* player = &next_world_state.player;

    memset(laser_buffer, 0, sizeof(laser_buffer));

    player->hit_by_red   = false;
    player->hit_by_blue  = false;
    player->green_hit = (GreenHit){0};

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
			
        }

        int32 skip_mirror_id = 0;
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
                    if (!th_hit && player->in_motion > STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH)
                    {
                        // passthrough
                        current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                        continue;
                    }
                    lb->end_coords = intCoordsToNorm(current_tile_coords);
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
                    if (th_hit) mirror = getEntityPointer(getNextCoords(current_tile_coords, th.moving_direction));
                    else mirror = getEntityPointer(current_tile_coords);

                    if (skip_next_mirror == 0 || skip_mirror_id != mirror->id)
                    {
                        if (mirror->in_motion)
                        {
                            int32 passthrough_comparison = 0;
                            bool player_turning = pack_intermediate_states_timer > 0;
                            if (player_turning) passthrough_comparison = PUSH_FROM_TURN_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH;
                            else passthrough_comparison = STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH; 

                            if (mirror->moving_direction == oppositeDirection(current_direction))
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
                            if (vec3IsZero(vec3Subtract(lb->end_coords, intCoordsToNorm(current_tile_coords)))) reverse_offset = true;

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
                        Entity* e = getEntityPointer(current_tile_coords);
                        if (!th_hit && e->in_motion > STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH)
                        {
                            current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                            continue;
                        }
                    }
                    lb->end_coords = intCoordsToNorm(current_tile_coords);
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

/*
void resetVisuals(Entity* entity)
{
    entity->position_norm = intCoordsToNorm(entity->coords);
    //entity->rotation_quat = directionToQuaternion(entity->direction, true); // don't seem to need right now, and causes bug with undoing from a turn -> pack direction is flipped (if pack direction is not north after the undo)
}

void resetStandardVisuals()
{
    resetVisuals(&next_world_state.player);
    resetVisuals(&next_world_state.pack);
    Entity* entity_group[4] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.glass_blocks, next_world_state.sources };
    FOR(entity_group_index, 4)
    {
        FOR(entity_instance_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* entity = &entity_group[entity_group_index][entity_instance_index];
            if (entity->removed) continue;
            resetVisuals(entity);
        }
    }
}
*/

// FALLING LOGIC

// returns true iff object is able to fall as usual, but object collides with something instead.
bool doFallingEntity(Entity* entity, bool do_animation)
{
    if (entity->removed) return false;
    Int3 next_coords = getNextCoords(entity->coords, DOWN);
    if (!intCoordsWithinLevelBounds(next_coords)) return false;
    if (!(isPushable(getTileType(next_coords)) && getEntityPointer(next_coords)->removed) && getTileType(next_coords) != NONE) return true;
    TrailingHitbox _;
    if (trailingHitboxAtCoords(next_coords, &_) && entity->id != PLAYER_ID) return true;

    int32 stack_size = getPushableStackSize(entity->coords);
    Int3 current_start_coords = entity->coords;
    Int3 current_end_coords = next_coords; 
    FOR(stack_fall_index, stack_size)
    {
        Entity* entity_in_stack = getEntityPointer(current_start_coords);
        if (entity_in_stack->removed) return false; // should never happen, shouldn't have removed entity in the middle of a stack somewhere
        if (entity_in_stack->in_motion) return false; 
        if (entity_in_stack == &next_world_state.pack && !pack_detached && stack_fall_index != 0) return false;
        if (entity_in_stack == &next_world_state.player && !next_world_state.player.hit_by_red) time_until_input = FALL_ANIMATION_TIME;

        // switch on if this is going to be first fall
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

        setTileType(getTileType(current_start_coords), current_end_coords); 
        setTileDirection(entity_in_stack->direction, current_end_coords);
        setTileType(NONE, current_start_coords);
        setTileDirection(NORTH, current_start_coords);
        entity_in_stack->coords = current_end_coords;
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
            if (pack_hitbox_turning_to_timer > 0 && int3IsEqual(pack_hitbox_turning_to_coords, entity->coords)) continue; // blocks blue-not-blue turn orthogonal case from falling immediately
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

void editAppendChar(char c)
{
    EditBuffer* buffer = &editor_state.edit_buffer; 
    if (buffer->length >= 256 - 1) return; // keep space for null terminator
    buffer->string[buffer->length++] = c;
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
        char c = (char)codepoint;
        editAppendChar(c);
    }
    if (input->backspace_pressed_this_frame)
    {
        editBackspace();
    }
}

int32 glyphSprite(char c)
{
    unsigned char uc = (unsigned char)c;
    if (uc < FONT_FIRST_ASCII || uc > FONT_LAST_ASCII) uc = '?';
    return (SpriteId)(uc - FONT_FIRST_ASCII);
}

// GAME INIT

void gameInitializeState()
{
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
    if (!file)
    {
        char msg[512];
        char cwd[MAX_PATH];
        GetCurrentDirectoryA(MAX_PATH, cwd);
        sprintf(msg, "Failed to open:\n%s\n\nCWD: %s", level_path, cwd);
        MessageBoxA(NULL, msg, "Error", MB_OK);
        return;
    }
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
        else if (next_world_state.buffer[buffer_index] == PLAYER) // special case for player, since there is only one
        {
            player->coords = bufferIndexToCoords(buffer_index);
            player->position_norm = intCoordsToNorm(player->coords);
            player->direction = next_world_state.buffer[buffer_index + 1];
            player->rotation_quat = directionToQuaternion(player->direction, false);
            player->id = PLAYER_ID;
        }
        else if (next_world_state.buffer[buffer_index] == PACK) // likewise special case for pack
        {
            pack->coords = bufferIndexToCoords(buffer_index);
            pack->position_norm = intCoordsToNorm(pack->coords);
            pack->direction = next_world_state.buffer[buffer_index + 1];
            pack->rotation_quat = directionToQuaternion(pack->direction, false);
            pack->id = PACK_ID;
        }
    }

    file = fopen(level_path, "rb+");
    camera = loadCameraInfo(file);
    loadWinBlockPaths(file);
    loadLockedInfoPaths(file);
    loadResetBlockInfo(file);
    fclose(file);

    loadSolvedLevelsFromFile();

    camera_screen_offset.x = (int32)(camera.coords.x / OVERWORLD_SCREEN_SIZE_X);
    camera_screen_offset.z = (int32)(camera.coords.z / OVERWORLD_SCREEN_SIZE_Z);

    Vec4 quaternion_yaw   = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), camera.yaw);
    Vec4 quaternion_pitch = quaternionFromAxisAngle(intCoordsToNorm(AXIS_X), camera.pitch);
    camera.rotation  = quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));
    world_state = next_world_state;
}

void gameInitialize(char* level_name) 
{	
    // TODO(spike): panic if cannot open constructed level path (check if can open here before we pass on)
    if (level_name == 0) strcpy(next_world_state.level_name, debug_level_name);
    else strcpy(next_world_state.level_name, level_name);
    gameInitializeState();
}

// UNDO / RESTART

void initUndoBuffer()
{
    memset(&undo_buffer, 0, sizeof(UndoBuffer));
    memset(undo_buffer.level_change_indices, 0xFF, sizeof(undo_buffer.level_change_indices));
}

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

// called after a noraml (non-level-change) action
// diffs world_state vs. next_world_state and stores deltas for every entity that changed
void recordActionForUndo(WorldState* old_state)
{
	// evict oldest action if headers are full
    if (undo_buffer.header_count >= MAX_UNDO_ACTIONS)
    {
        UndoActionHeader* oldest = &undo_buffer.headers[undo_buffer.oldest_action_index];
        undo_buffer.delta_count -= oldest->entity_count;

        // free level change slot if the oldest action had one
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

    uint32 header_index = undo_buffer.header_write_pos;
    uint32 delta_start = undo_buffer.delta_write_pos;
    uint32 entity_count = 0;

    recordEntityDelta(&old_state->player);
    recordEntityDelta(&old_state->pack); // could potentially check on detach here, and then if detach only store if delta check is passed. if we need the deltas.
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
    undo_buffer.level_change_indices[header_index] = 0xFF;
    undo_buffer.header_write_pos = (header_index + 1) % MAX_UNDO_ACTIONS;
    undo_buffer.header_count++;

    restart_last_turn = false;
}

// call before transitioning to a new level. stores a delta for every entity in the current level, plus the level change metadata
void recordLevelChangeForUndo(char* current_level_name, bool level_was_just_solved)
{
	// evict oldest action if headers are full (TODO(spike): wrap into function)
    if (undo_buffer.header_count >= MAX_UNDO_ACTIONS)
    {
        UndoActionHeader* oldest = &undo_buffer.headers[undo_buffer.oldest_action_index];
        undo_buffer.delta_count -= oldest->entity_count;

        // free level change slot if the oldest action had one
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
}

bool performUndo()
{
    if (undo_buffer.header_count == 0) return false;

	// get most recent action header
    uint32 header_index = (undo_buffer.header_write_pos + MAX_UNDO_ACTIONS - 1) % MAX_UNDO_ACTIONS;
    UndoActionHeader* header = &undo_buffer.headers[header_index];

    /*
    // TEMP DEBUG
    char _dbg[64];
    snprintf(_dbg, sizeof(_dbg), "undo: header_count=%d entity_count=%d", undo_buffer.header_count, header->entity_count);
    drawDebugText(_dbg);
    */

    if (header->level_changed)
    {
        uint8 level_change_index = undo_buffer.level_change_indices[header_index];
        UndoLevelChange* level_change = &undo_buffer.level_changes[level_change_index];

        // reinitialize previous
        gameInitialize(level_change->from_level);

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

        /*
        // TEMP DEBUG
        char dbg[256];
        snprintf(dbg, sizeof(dbg), "undo delta: id=%d coords=(%d,%d,%d) dir=%d removed=%d", 
                 delta->id, delta->old_coords.x, delta->old_coords.y, delta->old_coords.z, 
                 delta->old_direction, delta->was_removed);
        drawDebugText(dbg);
        // END TEMP DEBUG
        */

        Entity* e = getEntityFromId(delta->id);
        if (e)
        {
            e->coords = delta->old_coords;
            e->position_norm = intCoordsToNorm(e->coords);
            e->direction = delta->old_direction;
            e->rotation_quat = directionToQuaternion(e->direction, true);
            e->removed = delta->was_removed;

            if (!delta->was_removed)
            {
            	TileType type = getTileTypeFromId(delta->id);
                setTileType(type, delta->old_coords);
                setTileDirection(delta->old_direction, delta->old_coords);
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

    // clear animations + trailing hitboxes
    memset(animations, 0, sizeof(animations));
    memset(trailing_hitboxes, 0, sizeof(trailing_hitboxes));
	// sync worldstate
    world_state = next_world_state;

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

void doHeadRotation(bool clockwise)
{
    int32 stack_size = getPushableStackSize(getNextCoords(next_world_state.player.coords, UP));

    Int3 current_tile_coords = getNextCoords(next_world_state.player.coords, UP);
    FOR(stack_rotate_index, stack_size)
    {
        Entity* entity = getEntityPointer(current_tile_coords);
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
        if (!up_or_down) createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0, 
                                                     directionToQuaternion(current_direction, true), 
                                                     directionToQuaternion(next_direction, true), 
                                                     &entity->rotation_quat,
                                                     id, TURN_ANIMATION_TIME);
        else 
        {
            Vec4 start = entity->rotation_quat;
            float sign = clockwise ? 1.0f : -1.0f;
            Vec4 delta = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), sign * 0.25f * TAU);
            Vec4 end = quaternionNormalize(quaternionMultiply(delta, start));
            createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0,
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

    createInterpolationAnimation(intCoordsToNorm(player->coords), 
                                 intCoordsToNorm(next_player_coords), 
                                 &player->position_norm,
                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                 PLAYER_ID, animation_time);

    int32 trailing_hitbox_time = TRAILING_HITBOX_TIME;
    createTrailingHitbox(player->coords, input_direction, NO_DIRECTION, trailing_hitbox_time, PLAYER);

    // move pack also maybe
    if (pack_detached) 
    {
        setTileType(NONE, player->coords);
        setTileDirection(NORTH, player->coords);
    }
    else 
    {
        setTileType(NONE, pack->coords);
        setTileDirection(NORTH, pack->coords);
        setTileType(PACK, player->coords);
        createInterpolationAnimation(intCoordsToNorm(pack->coords),
                                     intCoordsToNorm(player->coords),
                                     &pack->position_norm,
                                     IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                     PACK_ID, animation_time);

        createTrailingHitbox(pack->coords, input_direction, NO_DIRECTION, trailing_hitbox_time, PACK);

        pack->coords = player->coords;
        setTileDirection(pack->direction, pack->coords);

        pack->moving_direction = input_direction;
    }

    player->coords = next_player_coords;
    setTileType(PLAYER, player->coords);	
    setTileDirection(player->direction, player->coords);

    player->moving_direction = input_direction;

    changeMoving(player);
    changeMoving(pack);

    if (record_for_undo)
    {
        pending_undo_record = true;
        pending_undo_snapshot = world_state;
    }
}

void updatePackDetached()
{
    TileType tile_behind_player = getTileType(getNextCoords(next_world_state.player.coords, oppositeDirection(next_world_state.player.direction)));
    if (tile_behind_player == PACK) pack_detached = false;
    else pack_detached = true;
}

// EDITOR

/*
    contains two modes, place/break or select. is called if either one happens.

    WASD, SPACE, SHIFT: camera movement

    J: FOV toggle (30 <-> 60)
    C: save camera
    I: save world state

    1: place/break mode
    LMB: break
    RMB: place
    MMB: select block
    R: rotate block

    2: select mode
    LMB: selet block instance
*/

void editorMode(TickInput *tick_input)
{
    Entity* player = &next_world_state.player;
    Entity* pack = &next_world_state.pack;

    Vec3 right_camera_basis, forward_camera_basis;
    cameraBasisFromYaw(camera.yaw, &right_camera_basis, &forward_camera_basis);

    if (editor_state.editor_mode == PLACE_BREAK || editor_state.editor_mode == SELECT)
    {
        if (tick_input->w_press) 
        {
            camera.coords.x += forward_camera_basis.x * CAMERA_MOVE_STEP;
            camera.coords.z += forward_camera_basis.z * CAMERA_MOVE_STEP;
        }
        if (tick_input->a_press) 
        {
            camera.coords.x -= right_camera_basis.x * CAMERA_MOVE_STEP;
            camera.coords.z -= right_camera_basis.z * CAMERA_MOVE_STEP;
        }
        if (tick_input->s_press) 
        {
            camera.coords.x -= forward_camera_basis.x * CAMERA_MOVE_STEP;
            camera.coords.z -= forward_camera_basis.z * CAMERA_MOVE_STEP;
        }
        if (tick_input->d_press) 
        {
            camera.coords.x += right_camera_basis.x * CAMERA_MOVE_STEP;
            camera.coords.z += right_camera_basis.z * CAMERA_MOVE_STEP;
        }
        if (tick_input->space_press) camera.coords.y += CAMERA_MOVE_STEP;
        if (tick_input->shift_press) camera.coords.y -= CAMERA_MOVE_STEP;
    }

    if (time_until_input != 0) return; 

    if (editor_state.editor_mode == PLACE_BREAK)
    {
        if (tick_input->left_mouse_press || tick_input->right_mouse_press || tick_input->middle_mouse_press || tick_input->r_press || tick_input->f_press || tick_input->h_press || tick_input->g_press)
        {
            Vec3 neg_z_basis = {0, 0, -1};
            RaycastHit raycast_output = raycastHitCube(camera.coords, vec3RotateByQuaternion(neg_z_basis, camera.rotation), MAX_RAYCAST_SEEK_LENGTH);

            if ((tick_input->left_mouse_press || tick_input->f_press) && raycast_output.hit) 
            {
                Entity *entity= getEntityPointer(raycast_output.hit_coords);
                if (entity != 0)
                {
                    entity->coords = (Int3){0};
                    entity->position_norm = (Vec3){0};
                    entity->removed = true;

                    // TODO(spike): if deleting entity, go through reset blocks and remove from reset block
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

                    if (editor_state.picked_tile != VOID && editor_state.picked_tile != NOT_VOID && editor_state.picked_tile != GRID) 
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
                if (isEntity(tile) && tile != VOID && tile != NOT_VOID)
                {
                    Direction direction = getTileDirection(raycast_output.hit_coords);
                    if (direction == DOWN) direction = NORTH;
                    else direction++;
                    setTileDirection(direction, raycast_output.hit_coords);
                    Entity *entity = getEntityPointer(raycast_output.hit_coords);
                    if (entity != 0)
                    {
                        entity->direction = direction;
                        if (getTileType(entity->coords) == MIRROR) entity->rotation_quat = directionToQuaternion(direction, true); // unclear why this is required, something to do with my sprite layout
                        else entity->rotation_quat = directionToQuaternion(direction, false);
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

            time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
        }
        if (tick_input->l_press)
        {
            editor_state.picked_tile++;
            if (editor_state.picked_tile == LADDER + 1) editor_state.picked_tile = VOID;
            time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
        }
        if (tick_input->j_press)
        {
            if (editor_state.do_wide_camera) editor_state.do_wide_camera = false;
            else editor_state.do_wide_camera = true;
            time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
        }
        if (tick_input->m_press)
        {
            clearSolvedLevels();
            time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
        }
    }

    if (editor_state.editor_mode == SELECT)
    {
        if (tick_input->left_mouse_press)
        {
            Vec3 neg_z_basis = {0, 0, -1};
            RaycastHit raycast_output = raycastHitCube(camera.coords, vec3RotateByQuaternion(neg_z_basis, camera.rotation), MAX_RAYCAST_SEEK_LENGTH);

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
            RaycastHit raycast_output = raycastHitCube(camera.coords, vec3RotateByQuaternion(neg_z_basis, camera.rotation), MAX_RAYCAST_SEEK_LENGTH);
            Entity* rb = 0;
            if (editor_state.selected_id > 0) rb = getEntityFromId(editor_state.selected_id);
            if (rb != 0 && getTileType(rb->coords) == RESET_BLOCK)
            {
                Entity* new_e = getEntityPointer(raycast_output.hit_coords);
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
                    time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
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
                    time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
                    gameInitialize(wb->next_level);
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
    accumulator += delta_time;

    // fix camera once per present frame
    if (editor_state.editor_mode != NO_MODE)
    {
        camera.yaw += tick_input.mouse_dx * CAMERA_SENSITIVITY;
        if (camera.yaw >  0.5f * TAU) camera.yaw -= TAU; 
        if (camera.yaw < -0.5f * TAU) camera.yaw += TAU; 
        camera.pitch += tick_input.mouse_dy * CAMERA_SENSITIVITY;
        float pitch_limit = 0.25f * TAU;
        if (camera.pitch >  pitch_limit) camera.pitch =  pitch_limit; 
        if (camera.pitch < -pitch_limit) camera.pitch = -pitch_limit; 

        Vec4 quaternion_yaw   = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), camera.yaw);
        Vec4 quaternion_pitch = quaternionFromAxisAngle(intCoordsToNorm(AXIS_X), camera.pitch);
        camera.rotation  = quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));
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

    while (accumulator >= PHYSICS_INCREMENT)
   	{
		next_world_state = world_state;
        Entity* player = &next_world_state.player;
        Entity* pack = &next_world_state.pack;

        debug_text_coords = DEBUG_TEXT_COORDS_START;

        // mode toggle
        if (editor_state.editor_mode != SELECT_WRITE)
    	{
            if (tick_input.zero_press) editor_state.editor_mode = NO_MODE;
            if (tick_input.one_press)  editor_state.editor_mode = PLACE_BREAK;
            if (tick_input.two_press)  editor_state.editor_mode = SELECT;
        }

        if (editor_state.editor_mode == NO_MODE)
        {
            if (time_until_input == 0 && tick_input.z_press)
            {
                if (performUndo())
                {
                    updatePackDetached();
                    //resetStandardVisuals();
                }
                time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
            }
            if (time_until_input == 0 && tick_input.r_press)
            {
                // restart
                if (!restart_last_turn) recordActionForUndo(&world_state);
                memset(animations, 0, sizeof(animations));
                gameInitializeState();
                time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
                restart_last_turn = true;
            }
			if (time_until_input == 0 && tick_input.escape_press && !in_overworld)
            {
                // leave current level if not in overworld. for now get first win block and go to their next location
                char save_solved_levels[64][64] = {0};
                memcpy(save_solved_levels, next_world_state.solved_levels, sizeof(save_solved_levels));
                levelChangePrep("overworld");
                time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
                gameInitialize("overworld");
                memcpy(next_world_state.solved_levels, save_solved_levels, sizeof(save_solved_levels));
                writeSolvedLevelsToFile();
            }

            if (time_until_input == 0 && (tick_input.w_press || tick_input.a_press || tick_input.s_press || tick_input.d_press) && player->in_motion == 0)
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
                        // check if endpoint is valid before teleport (i.e, if pack can go there - if over air, teleport anyway, probably?)

                        bool allow_tp = false;
                        TileType player_ghost_tile = getTileType(player_ghost_coords);
                        TileType pack_ghost_tile = getTileType(pack_ghost_coords);
                        if ((player_ghost_tile == NONE || player_ghost_tile == PLAYER) && (pack_ghost_tile == NONE || pack_ghost_tile == PLAYER || pack_ghost_tile == PACK)) allow_tp = true;

                        if (allow_tp)
                        {
                            if (!int3IsEqual(player_ghost_coords, player->coords))
                            {
                                pending_undo_record = true;
                                pending_undo_snapshot = world_state;

                                setTileType(NONE, player->coords);
                                setTileDirection(NORTH, player->coords);
                                zeroAnimations(PLAYER_ID);
                                player->coords = player_ghost_coords;
                                player->position_norm = intCoordsToNorm(player_ghost_coords);
                                player->direction = player_ghost_direction;
                                player->rotation_quat = directionToQuaternion(player_ghost_direction, true);
                                setTileType(PLAYER, player_ghost_coords);
                                setTileDirection(player_ghost_direction, player_ghost_coords);
                                if (!pack_detached)
                                {
                                    Int3 pack_coords = getNextCoords(player_ghost_coords, oppositeDirection(pack_ghost_direction));
                                    setTileType(NONE, pack->coords);
                                    setTileDirection(NORTH, pack->coords);
                                    zeroAnimations(PACK_ID);
                                    pack->coords = pack_coords; 
                                    pack->position_norm = intCoordsToNorm(pack_coords);
                                    pack->direction = pack_ghost_direction;
                                    pack->rotation_quat = directionToQuaternion(pack_ghost_direction, true);
                                    setTileType(PACK, pack_coords);
                                    setTileDirection(pack_ghost_direction, pack_coords);
                                }
                            }
                            // tp sends player ontop of themselves - should count as a successful tp, but no point changing state, and don't samve to undo buffer.
                            time_until_input = SUCCESSFUL_TP_TIME;
                        }
                        else
                        {
                            // tp obstructed
                            time_until_input = FAILED_TP_TIME;
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
                                if (isPushable(getTileType(coords_ahead)) && getEntityPointer(coords_ahead)->in_motion) move_player = false;
                                else if (isPushable(getTileType(coords_below_and_ahead)) && getEntityPointer(coords_below_and_ahead)->moving_direction != NO_DIRECTION) move_player = false;
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
                            Int3 coords_below = getNextCoords(next_player_coords, DOWN);
                            TileType tile_below = getTileType(coords_below);
                            if ((tile_below != NONE || player->hit_by_red) && (!isEntity(tile_below) || getEntityPointer(coords_below)->moving_direction == NO_DIRECTION))
                            {
                                if (do_push) pushAll(next_player_coords, input_direction, animation_time, true, false);
                                doStandardMovement(input_direction, next_player_coords, animation_time, true);
                                time_until_input = animation_time;
                            }
                            else
                            {
                                // leap of faith logic
                                WorldState world_state_savestate = next_world_state;

                                if (do_push) pushAll(next_player_coords, input_direction, 0, false, false);

                                bool animations_on = false;

                                if (!player->hit_by_blue)
                                {
                                    doFallingObjects(animations_on);
                                    if (pack_detached) doFallingEntity(pack, animations_on);
                                    doHeadMovement(input_direction, animations_on, 1);
                                }

                                setTileType(NONE, player->coords);
                                player->coords = next_player_coords;
                                setTileType(PLAYER, player->coords);	

                                if (!pack_detached)
                                {
                                    setTileType(NONE, pack->coords);
                                    pack->coords = getNextCoords(next_player_coords, oppositeDirection(input_direction));
                                    setTileType(PACK, pack->coords);
                                }

                                updateLaserBuffer();

                                bool leap_of_faith_worked = false;
                                if (player->hit_by_red) leap_of_faith_worked = true;
                                next_world_state = world_state_savestate;
                                if (leap_of_faith_worked)
                                {
                                    if (do_push) pushAll(next_player_coords, input_direction, animation_time, true, false);
                                    doStandardMovement(input_direction, next_player_coords, animation_time, true);
                                    bypass_player_fall = true; 
                                    time_until_input = MOVE_OR_PUSH_ANIMATION_TIME;
                                }
                                else 
                                {
                                    doFailedWalkAnimations(player->direction);
                                    time_until_input = FAILED_ANIMATION_TIME;
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
                                setTileType(NONE, player->coords);
                                setTileDirection(NORTH, player->coords);
                                player->coords = coords_above;
                                setTileType(PLAYER, player->coords);
                                setTileDirection(player->direction, player->coords);
			
                                createInterpolationAnimation(intCoordsToNorm(getNextCoords(player->coords, DOWN)),
                                                             intCoordsToNorm(player->coords),
                                                             &player->position_norm,
                                                             IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                             PLAYER_ID, CLIMB_ANIMATION_TIME);

                                player->in_motion = CLIMB_ANIMATION_TIME;
                                player->moving_direction = UP;

                                if (!pack_detached)
                                {
                                    setTileType(NONE, pack->coords);
                                    setTileDirection(NORTH, pack->coords);
                                    pack->coords = getNextCoords(pack->coords, UP);
                                    setTileType(PACK, pack->coords);
                                    setTileDirection(pack->direction, pack->coords);

                                    createInterpolationAnimation(intCoordsToNorm(getNextCoords(pack->coords, DOWN)),
                                                                 intCoordsToNorm(pack->coords),
                                                                 &pack->position_norm,
                                                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                                 PACK_ID, CLIMB_ANIMATION_TIME);

                                    pack->in_motion = CLIMB_ANIMATION_TIME;
                                    pack->moving_direction = UP;
                                }

                                pending_undo_record = true;
                                pending_undo_snapshot = world_state;

                                time_until_input = CLIMB_ANIMATION_TIME + MOVE_OR_PUSH_ANIMATION_TIME;
                            }
                            else
                            {
                                //doFailedClimbUpAnimation();
                                //time_until_input = FAILED_CLIMB_TIME;
                            }
                        }
						else if (do_failed_animations) 
                        {
                            doFailedWalkAnimations(player->direction);
                            time_until_input = FAILED_ANIMATION_TIME;
                        }
                    }
                }
                else if (input_direction != oppositeDirection(player->direction)) // check if turning (as opposed to trying to reverse)
                {
                    // player is turning

                    if (player->hit_by_red || getTileType(getNextCoords(player->coords, DOWN)) != NONE)
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

                            createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0, 
                                                         directionToQuaternion(player->direction, true), 
                                                         directionToQuaternion(input_direction, true), 
                                                         &player->rotation_quat,
                                                         1, TURN_ANIMATION_TIME); 
                            player->direction = input_direction;
                            setTileDirection(player->direction, player->coords);
                            player->moving_direction = NO_DIRECTION;

                            pending_undo_record = true;
                            pending_undo_snapshot = world_state;
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
                            else if (isEntity(getTileType(orthogonal_coords)) && getEntityPointer(orthogonal_coords)->in_motion) pause_turn = true;
                            else if (isEntity(getTileType(diagonal_coords))   && getEntityPointer(diagonal_coords)->in_motion)   pause_turn = true;

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
                                            recordActionForUndo(&world_state);
                                            pushAll(diagonal_coords, oppositeDirection(input_direction), PUSH_FROM_TURN_ANIMATION_TIME, true, false);
                                        }
                                        doFailedTurnAnimations(input_direction, clockwise);
                                    }
                                    else
                                    {
                                        pending_undo_record = true;
                                        pending_undo_snapshot = world_state;

                                        createTrailingHitbox(pack->coords, input_direction, NO_DIRECTION, FIRST_TRAILING_PACK_TURN_HITBOX_TIME, PACK);

                                        if (isPushable(getTileType(getNextCoords(player->coords, UP)))) 
                                        {
                                            if (!player->hit_by_blue) doHeadRotation(clockwise);
                                        }

                                        createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0, 
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
                                                                    &pack->position_norm, &pack->rotation_quat, PACK_ID);

                                        if (push_diagonal)   do_diagonal_push_on_turn = true;
                                        if (push_orthogonal) do_orthogonal_push_on_turn = true;

                                        pack_intermediate_states_timer = TIME_BEFORE_ORTHOGONAL_PUSH_STARTS_IN_TURN + PACK_TIME_IN_INTERMEDIATE_STATE + 1;
                                        pack_intermediate_coords = diagonal_coords;
                                        pack_orthogonal_push_direction = orthogonal_push_direction;
                                        pack_hitbox_turning_to_timer = TURN_ANIMATION_TIME + TIME_BEFORE_ORTHOGONAL_PUSH_STARTS_IN_TURN;
                                        pack_hitbox_turning_to_coords = orthogonal_coords;
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
                    time_until_input = TURN_ANIMATION_TIME;
        		}
                else if (input_direction == oppositeDirection(player->direction)) // TODO(spike): CONTINUE (don't allow climb on all sides) 
                {
                    // backwards movement: allow only when climbing down a ladder. right now just move, and let player fall (functionally the same, but animation is goofy)
                    Direction backwards_direction = oppositeDirection(player->direction);
                    Int3 coords_below = getNextCoords(player->coords, DOWN);
					if (getTileType(coords_below) == LADDER && getTileDirection(coords_below) == input_direction && (pack_detached || (!pack_detached && getTileType(getNextCoords(pack->coords, DOWN)) == NONE)))
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
		
                            if (!pack_detached)
                            {
                                setTileType(NONE, pack->coords);
                                setTileDirection(NORTH, pack->coords);
                                pack->coords = getNextCoords(pack->coords, backwards_direction);
                                setTileType(PACK, pack->coords);
                                setTileDirection(pack->direction, pack->coords);

                                createInterpolationAnimation(intCoordsToNorm(getNextCoords(pack->coords, player->direction)),
                                                             intCoordsToNorm(pack->coords),
                                                             &pack->position_norm,
                                                             IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                             PACK_ID, MOVE_OR_PUSH_ANIMATION_TIME);

                                pack->in_motion = MOVE_OR_PUSH_ANIMATION_TIME;
                                pack->moving_direction = backwards_direction;
                            }
                            setTileType(NONE, player->coords);
                            setTileDirection(NORTH, player->coords);
                            player->coords = getNextCoords(player->coords, backwards_direction);
                            setTileType(PLAYER, player->coords);
                            setTileDirection(player->direction, player->coords);

                            createInterpolationAnimation(intCoordsToNorm(getNextCoords(player->coords, player->direction)),
                                                         intCoordsToNorm(player->coords),
                                                         &player->position_norm,
                                                         IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                         PLAYER_ID, MOVE_OR_PUSH_ANIMATION_TIME);

                            player->in_motion = MOVE_OR_PUSH_ANIMATION_TIME;
                            player->moving_direction = backwards_direction;

                            player->first_fall_already_done = true;
                            if (!pack_detached) pack->first_fall_already_done = true;

                            pending_undo_record = true;
                            pending_undo_snapshot = world_state;

                            time_until_input = MOVE_OR_PUSH_ANIMATION_TIME;
                        }
                        else
                        {
                            doFailedWalkAnimations(oppositeDirection(player->direction));
                            time_until_input = FAILED_ANIMATION_TIME;
                        }
                    }
					else
                    {
                        doFailedWalkAnimations(oppositeDirection(player->direction));
                        time_until_input = FAILED_ANIMATION_TIME;
                    }
                }
            }
        }
        else
        {
            editorMode(&tick_input);
        }

        // handle pack turning sequence
        if (pack_intermediate_states_timer > 0)
        {
            if (pack_intermediate_states_timer == 7)
            {
                createTrailingHitbox(pack->coords, pack_orthogonal_push_direction, NO_DIRECTION, 4, PACK);
				if (do_diagonal_push_on_turn) pushAll(pack_intermediate_coords, oppositeDirection(player->direction), PUSH_FROM_TURN_ANIMATION_TIME, true, false); // CHANGE THIS IF WANT PACK TO SWEEP
            }
            else if (pack_intermediate_states_timer == 5)
            {
                setTileType(NONE, pack->coords);
                setTileDirection(NORTH, pack->coords);
                pack->coords = pack_intermediate_coords;
                pack->direction = oppositeDirection(player->direction);
                setTileType(PACK, pack->coords);
                setTileDirection(pack->direction, pack->coords);
            }
            else if (pack_intermediate_states_timer == 4)
            {
                if (do_orthogonal_push_on_turn) pushAll(pack_hitbox_turning_to_coords, pack_orthogonal_push_direction, PUSH_FROM_TURN_ANIMATION_TIME, true, false); // CHANGE THIS IF WANT PACK TO SWEEP

                setTileType(NONE, pack->coords);
                setTileDirection(NORTH, pack->coords);
				pack->coords = pack_hitbox_turning_to_coords;
                setTileType(PACK, pack->coords);
                setTileDirection(pack->direction, pack->coords);
                createTrailingHitbox(pack_intermediate_coords, pack->direction, NO_DIRECTION, 3, PACK);
            }
            else if (pack_intermediate_states_timer == 1)
            { 
                if (pending_undo_record)
                {
                    pending_undo_record = false;
                    recordActionForUndo(&pending_undo_snapshot);
                }
                if (do_player_and_pack_fall_after_turn)
                {
                    doFallingEntity(player, true);
                    doFallingEntity(pack, true);
                    do_player_and_pack_fall_after_turn = false;
                }
                player_hit_by_blue_in_turn = false;
            }
            pack_intermediate_states_timer--;
        }

		updateLaserBuffer();

        // falling logic
		if (!player->hit_by_blue) doFallingObjects(true);

        if (pack_intermediate_states_timer == 0)
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
        else
        {
            // in the middle of a turn (where pack is attached)

            if (getTileType(getNextCoords(player->coords, DOWN)) == NONE && !player->hit_by_red)
            {
                // in middle of turn, which means was on ground or red, and now no longer on ground AND not red, so must have stopped being red in the middle of the turn.
                do_player_and_pack_fall_after_turn = true;
            }

            if (isPushable(getTileType(pack_hitbox_turning_to_coords)))
            {
				if (player->hit_by_blue) player_hit_by_blue_in_turn = true;
                if (!player->hit_by_blue && player_hit_by_blue_in_turn && pack_intermediate_states_timer > 0)
                {
                    if (getTileType(getNextCoords(pack_hitbox_turning_to_coords, DOWN)) == NONE)
                    {
                        entity_to_fall_after_blue_not_blue_turn_timer = pack_intermediate_states_timer + 4; // this number is magic (sorry); it is the frame count that makes the entity fall as soon as possible, i.e., at the same time as the player (if magenta-not-magenta)
                        entity_to_fall_after_blue_not_blue_turn_coords = getNextCoords(pack_hitbox_turning_to_coords, pack_orthogonal_push_direction);
                        player_hit_by_blue_in_turn = false;
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
                time_until_input = animation_time;
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
                    setTileType(NONE, player->coords);
                    setTileDirection(NORTH, player->coords);
                    player->coords = coords_above;
                    setTileType(PLAYER, player->coords);
                    setTileDirection(player->direction, player->coords);

                    createInterpolationAnimation(intCoordsToNorm(getNextCoords(player->coords, DOWN)),
                                                 intCoordsToNorm(player->coords),
                                                 &player->position_norm,
                                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                 PLAYER_ID, CLIMB_ANIMATION_TIME);

                    player->in_motion = CLIMB_ANIMATION_TIME;
                    player->moving_direction = UP;

                    if (!pack_detached)
                    {
                        setTileType(NONE, pack->coords);
                        setTileDirection(NORTH, pack->coords);
                        pack->coords = getNextCoords(pack->coords, UP);
                        setTileType(PACK, pack->coords);
                        setTileDirection(pack->direction, pack->coords);

                        createInterpolationAnimation(intCoordsToNorm(getNextCoords(pack->coords, DOWN)),
                                                     intCoordsToNorm(pack->coords),
                                                     &pack->position_norm,
                                                     IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                                     PACK_ID, CLIMB_ANIMATION_TIME);

                        pack->in_motion = CLIMB_ANIMATION_TIME;
                        pack->moving_direction = UP;
                    }
                    time_until_input = CLIMB_ANIMATION_TIME + MOVE_OR_PUSH_ANIMATION_TIME;
                }
            }
            else
            {
                // can't move or climb more
                doFailedWalkAnimations(player->direction);
                player->in_motion = FAILED_ANIMATION_TIME;
                player->moving_direction = NO_DIRECTION;
                pack->in_motion = FAILED_ANIMATION_TIME;
                pack->moving_direction = NO_DIRECTION;
                time_until_input = FAILED_ANIMATION_TIME;
            }
        }

        if (entity_to_fall_after_blue_not_blue_turn_timer > 0)
        {
            if (entity_to_fall_after_blue_not_blue_turn_timer == 1) 
            {
                if (isPushable(getTileType(entity_to_fall_after_blue_not_blue_turn_coords))) doFallingEntity(getEntityPointer(entity_to_fall_after_blue_not_blue_turn_coords), true);
            }
            entity_to_fall_after_blue_not_blue_turn_timer--;
        }

        // pack detach logic
        TileType tile_behind_player = getTileType(getNextCoords(player->coords, oppositeDirection(player->direction)));
        if (!pack_detached && pack_intermediate_states_timer == 0 && tile_behind_player != PACK) pack_detached = true;
        else if (pack_detached && tile_behind_player == PACK) pack_detached = false;

        // do animations
		for (int animation_index = 0; animation_index < MAX_ANIMATION_COUNT; animation_index++)
        {
			if (animations[animation_index].frames_left == 0) continue;
			if (animations[animation_index].position_to_change != 0) 
            {
                Vec3 next_pos = animations[animation_index].position[animations[animation_index].frames_left-1];
                if (!vec3IsZero(next_pos)) *animations[animation_index].position_to_change = next_pos; 
            }
			if (animations[animation_index].rotation_to_change != 0) 
            {
				*animations[animation_index].rotation_to_change = animations[animation_index].rotation[animations[animation_index].frames_left-1];
            }
            animations[animation_index].frames_left--;
        }

        // decrement in_motion / moving_direction and reset first_fall_already_done
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

		// handle turning hitboxes
        if (pack_hitbox_turning_to_timer > 0) pack_hitbox_turning_to_timer--;

        // decrement trailing hitboxes 
        FOR(i, MAX_TRAILING_HITBOX_COUNT) if (trailing_hitboxes[i].frames > 0) trailing_hitboxes[i].frames--;
        if (bypass_player_fall) bypass_player_fall = false;

        // decide which ghosts to render, if ghosts should be rendered
        bool do_player_ghost = false;
        bool do_pack_ghost = false;
		if (calculateGhosts())
        {
            do_player_ghost = true;
            if (!pack_detached) do_pack_ghost = true;
        }

        // delete player / pack if above void
        if (!player->hit_by_red)
        {
            if ((getTileType(getNextCoords(player->coords, DOWN)) == VOID || getTileType(getNextCoords(player->coords, DOWN)) == NOT_VOID) && !presentInAnimations(PLAYER_ID)) 
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
                if ((getTileType(getNextCoords(pack->coords, DOWN)) == VOID || getTileType(getNextCoords(pack->coords, DOWN)) == NOT_VOID) && !presentInAnimations(PACK_ID)) 
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
                if ((getTileType(getNextCoords(pack->coords, DOWN)) == VOID || getTileType(getNextCoords(pack->coords, DOWN)) == NOT_VOID) && !presentInAnimations(PACK_ID))
                {
                    setTileType(NONE, pack->coords);
                    setTileDirection(NORTH, pack->coords);
                    pack->removed = true;
                }
            }
        }

        // win block logic
        if ((getTileType(getNextCoords(player->coords, DOWN)) == WIN_BLOCK && !presentInAnimations(PLAYER_ID)) && (tick_input.q_press && time_until_input == 0))
        {
            Entity* wb = getEntityPointer(getNextCoords(player->coords, DOWN));
            if (!pack_detached && !wb->locked)
            {
                if (wb->next_level[0] != 0)
                {
                    if (in_overworld) 
                    {
                        char level_path[64] = {0};
                        buildLevelPathFromName(next_world_state.level_name, &level_path, false);
                        saveLevelRewrite(level_path, false);
                    }
                    levelChangePrep(wb->next_level);
                    time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
                    gameInitialize(wb->next_level);
                }
            }
            else
            {
                // don't allow because pack is not attached, or win block is locked!
            }
        }
        if ((getTileType(getNextCoords(player->coords, DOWN)) == WIN_BLOCK && !presentInAnimations(PLAYER_ID)) && (tick_input.f_press && time_until_input == 0))
        {
            Entity* wb = getEntityPointer(getNextCoords(player->coords, DOWN));
			if (findInSolvedLevels(wb->next_level) == -1)
            {
                int32 next_free = nextFreeInSolvedLevels(&next_world_state.solved_levels);
                strcpy(next_world_state.solved_levels[next_free], wb->next_level);
            }
            writeSolvedLevelsToFile();
            time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
        }

        // reset block logic
        if ((getTileType(getNextCoords(player->coords, DOWN)) == RESET_BLOCK && !presentInAnimations(PLAYER_ID)) && (tick_input.q_press && time_until_input == 0))
        {
            Entity* rb = getEntityPointer(getNextCoords(player->coords, DOWN));
            FOR(to_reset_index, MAX_RESET_COUNT)
            {
                ResetInfo ri = rb->reset_info[to_reset_index];
                if (ri.id == -1) continue;

                Entity* reset_e = getEntityFromId(ri.id);
                if (reset_e != 0)
                {
                    if (!reset_e->removed)
                    {
                        setTileType(NONE, reset_e->coords);
                        setTileDirection(NORTH, reset_e->coords);
                    }

                    reset_e->coords = ri.start_coords;
                    reset_e->position_norm = intCoordsToNorm(reset_e->coords);
                    reset_e->direction = ri.start_direction;
                    reset_e->rotation_quat = directionToQuaternion(ri.start_direction, true);
                    reset_e->removed = false;

                    TileType type = getTileTypeFromId(ri.id);
                    setTileType(type, ri.start_coords);
                    setTileDirection(ri.start_direction, ri.start_coords);
                }
            }

            pending_undo_record = true;
            pending_undo_snapshot = world_state;

            time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
        }

		// figure out if entities should be locked / unlocked
        if (editor_state.editor_mode == NO_MODE)
        {
            Entity* entity_group[4] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.win_blocks, next_world_state.sources };
            FOR(group_index, 4)
            {
                FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
                {
                    Entity* e = &entity_group[group_index][entity_index];
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
                }
                else if (find_result == -1 && lb->removed)
                {
                    lb->removed = false;
                    setTileType(LOCKED_BLOCK, lb->coords);
                    setTileDirection(NORTH, lb->coords);
                }
            }
        }

        // final redo of laser buffer, after all logic is complete, for drawing
		updateLaserBuffer();
        
		// adjust overworld camera based on position
        if (in_overworld && player->id == PLAYER_ID)
        {
            int32 screen_offset_x = 0;
            int32 dx = player->coords.x - camera_center_start.x;
            if 		(dx > 0) screen_offset_x = (dx + (int32)(OVERWORLD_SCREEN_SIZE_X / 2)) / OVERWORLD_SCREEN_SIZE_X;
            else if (dx < 0) screen_offset_x = (dx - (int32)(OVERWORLD_SCREEN_SIZE_X / 2)) / OVERWORLD_SCREEN_SIZE_X;
            if (screen_offset_x != camera_screen_offset.x) 
            {
                int32 delta = screen_offset_x - camera_screen_offset.x;
                camera_screen_offset.x = screen_offset_x; 
                camera.coords.x += delta * OVERWORLD_SCREEN_SIZE_X;
            }

            int32 screen_offset_z = 0;
            int32 dz = player->coords.z - camera_center_start.z;
            if 		(dz > 0) screen_offset_z = (dz + (int32)(OVERWORLD_SCREEN_SIZE_Z / 2)) / OVERWORLD_SCREEN_SIZE_Z;
            else if (dz < 0) screen_offset_z = (dz - (int32)(OVERWORLD_SCREEN_SIZE_Z / 2)) / OVERWORLD_SCREEN_SIZE_Z;
            if (screen_offset_z != camera_screen_offset.z) 
            {
                int32 delta = screen_offset_z - camera_screen_offset.z;
                camera_screen_offset.z = screen_offset_z; 
                camera.coords.z += delta * OVERWORLD_SCREEN_SIZE_Z;
            }
        }

        if (pending_undo_record)
        {
            pending_undo_record = false;
            recordActionForUndo(&pending_undo_snapshot);
        }

        // finished updating state
        world_state = next_world_state;

		// DRAW 3D

        // draw lasers
		FOR(laser_buffer_index, 64)
        {
            LaserBuffer lb = laser_buffer[laser_buffer_index];
            Vec3 laser_rgb = {0};
            switch (lb.color)
            {
                case RED:   laser_rgb = (Vec3){ 1.0f, 0.0f, 0.0f }; break;
                case GREEN: laser_rgb = (Vec3){ 0.0f, 1.0f, 0.0f }; break;
                case BLUE:  laser_rgb = (Vec3){ 0.0f, 0.0f, 1.0f }; break;
                default: continue;
            }

			Vec3 diff = vec3Subtract(lb.end_coords, lb.start_coords);
            Vec3 center = vec3Add(lb.start_coords, vec3ScalarMultiply(diff, 0.5));

			float length = vec3Length(diff);
            Vec3 scale = { LASER_WIDTH, LASER_WIDTH, length };
        	Vec4 rotation = directionToQuaternion(lb.direction, false);

            drawLaser(center, scale, rotation, laser_rgb);
        }

        /*
        FOR(lb_index, 64)
        {
            LaserBuffer lb = laser_buffer[lb_index];
            if (vec3IsEqual(lb.start_coords, IDENTITY_TRANSLATION)) continue;
            char lb_text[256] = {0};
            snprintf(lb_text, sizeof(lb_text), "lb start coords: %.2f, %.2f, %.2f, lb end coords: %.2f, %.2f, %.2f", lb.start_coords.x, lb.start_coords.y, lb.start_coords.z, lb.end_coords.x, lb.end_coords.y, lb.end_coords.z);
            drawDebugText(lb_text);
        }
        */

        // clear laser buffer 
        memset(laser_buffer, 0, sizeof(laser_buffer));

        // draw most things (not player, pack, or sources)
        for (int tile_index = 0; tile_index < 2 * level_dim.x*level_dim.y*level_dim.z; tile_index += 2)
        {
			TileType draw_tile = world_state.buffer[tile_index];
			if (draw_tile == NONE || draw_tile == PLAYER || isSource(draw_tile) || draw_tile == PACK) continue;
			if (isEntity(draw_tile))
            {
                Entity* e = getEntityPointer(bufferIndexToCoords(tile_index));
                if (e->locked) draw_tile = LOCKED_BLOCK;

                //if (getCube3DId(draw_tile) == CUBE_3D_MIRROR) continue;

                drawAsset(getCube3DId(draw_tile), CUBE_3D, e->position_norm, DEFAULT_SCALE, e->rotation_quat); 
            }
			else
            {
                drawAsset(getCube3DId(draw_tile), CUBE_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToQuaternion(next_world_state.buffer[tile_index + 1], false));
            }
        }

        if (!world_state.player.removed)
        {
            player = &world_state.player;

            // TODO(spike): this is terrible (fix with shaders)
    		bool hit_by_green = false;
            if (player->green_hit.north || player->green_hit.west || player->green_hit.south || player->green_hit.east || player->green_hit.up || player->green_hit.down) hit_by_green = true;
            if      (player->hit_by_red && hit_by_green && player->hit_by_blue) drawAsset(CUBE_3D_PLAYER_WHITE,   CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (player->hit_by_red && hit_by_green             		  ) drawAsset(CUBE_3D_PLAYER_YELLOW,  CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (player->hit_by_red &&      	       player->hit_by_blue) drawAsset(CUBE_3D_PLAYER_MAGENTA, CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (             		   hit_by_green && player->hit_by_blue) drawAsset(CUBE_3D_PLAYER_CYAN,    CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (player->hit_by_red                 	  				  ) drawAsset(CUBE_3D_PLAYER_RED,     CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (             		   hit_by_green             		  ) drawAsset(CUBE_3D_PLAYER_GREEN,   CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (                            		   player->hit_by_blue) drawAsset(CUBE_3D_PLAYER_BLUE,    CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else drawAsset(CUBE_3D_PLAYER, CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);

            if (do_player_ghost) drawAsset(CUBE_3D_PLAYER_GHOST, CUBE_3D, intCoordsToNorm(player_ghost_coords), PLAYER_SCALE, directionToQuaternion(player_ghost_direction, true));
            if (do_pack_ghost)   drawAsset(CUBE_3D_PACK_GHOST,   CUBE_3D, intCoordsToNorm(pack_ghost_coords),   PLAYER_SCALE, directionToQuaternion(pack_ghost_direction, true));
        }
		if (!world_state.pack.removed) drawAsset(CUBE_3D_PACK, CUBE_3D, world_state.pack.position_norm, PLAYER_SCALE, world_state.pack.rotation_quat);

		// draw sources 
		for (int source_index = 0; source_index < MAX_ENTITY_INSTANCE_COUNT; source_index++)
        {
            if (world_state.sources[source_index].removed) continue;
            int32 id = 0;
            if (world_state.sources[source_index].locked) id = CUBE_3D_LOCKED_BLOCK;
            else id = getCube3DId(getTileType(world_state.sources[source_index].coords));
            drawAsset(id, CUBE_3D, world_state.sources[source_index].position_norm, DEFAULT_SCALE, world_state.sources[source_index].rotation_quat);
        }

		// DRAW 2D
        
        // display level name
		drawDebugText(next_world_state.level_name);

        /*
		char pack_text[256] = {0};
        snprintf(pack_text, sizeof(pack_text), "pack info: coords: %d, %d, %d, detached: %d", pack->coords.x, pack->coords.y, pack->coords.z, pack_detached);
        drawDebugText(pack_text);

        char player_text[256] = {0};
        snprintf(player_text, sizeof(player_text), "player info: coords: %d, %d, %d", player->coords.x, player->coords.y, player->coords.z);
        drawDebugText(player_text);
        */

        // box in_motion info
        /*
        Entity box = next_world_state.boxes[0];
		char box_moving_text[256] = {0};
        snprintf(box_moving_text, sizeof(box_moving_text), "box moving: %d", box.in_motion);
		drawDebugText(box_moving_text);

		char box_dir_text[256] = {0};
        snprintf(box_dir_text, sizeof(box_dir_text), "box moving: %d", box.moving_direction);
		drawDebugText(box_dir_text);
        */

        /*
        // camera pos info
        char camera_text[256] = {0};
        snprintf(camera_text, sizeof(camera_text), "camera pos: %f, %f, %f", camera.coords.x, camera.coords.y, camera.coords.z);
        drawDebugText(camera_text);
        */

		if (editor_state.editor_mode != NO_MODE)
        {
            // crosshair
            Vec3 crosshair_scale = { 35.0f, 35.0f, 0.0f };
            Vec3 center_screen = { ((float)SCREEN_WIDTH_PX / 2) - 5, ((float)SCREEN_HEIGHT_PX / 2) - 18, 0.0f }; // weird numbers are just adjustment because raycast starts slightly offset 
                                                                                                        		 // i think this is due to windowed mode, but could be issue with raycast.
        	drawAsset(SPRITE_2D_CROSSHAIR, SPRITE_2D, center_screen, crosshair_scale, IDENTITY_QUATERNION);

            // picked block
            Vec3 picked_block_scale = { 200.0f, 200.0f, 0.0f };
            Vec3 picked_block_coords = { SCREEN_WIDTH_PX - (picked_block_scale.x / 2) - 20, (picked_block_scale.y / 2) + 50, 0.0f };
            drawAsset(getSprite2DId(editor_state.picked_tile), SPRITE_2D, picked_block_coords, picked_block_scale, IDENTITY_QUATERNION);

            if (editor_state.selected_id >= 0 && (editor_state.editor_mode == SELECT || editor_state.editor_mode == SELECT_WRITE))
            {
                drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, intCoordsToNorm(editor_state.selected_coords), DEFAULT_SCALE, IDENTITY_QUATERNION);

                if ((editor_state.selected_id / ID_OFFSET_RESET_BLOCK) * ID_OFFSET_RESET_BLOCK)
                {
                    Entity* rb = getEntityFromId(editor_state.selected_id);
                    FOR(to_reset_index, MAX_RESET_COUNT)
                    {
                        ResetInfo ri = rb->reset_info[to_reset_index];
                        if (ri.id == -1) continue;
                        Entity* e = 0; 
                        if (ri.id != -2) e = getEntityFromId(ri.id);
                        Vec3 draw_coords = {0};
                        if (e != 0) draw_coords = e->position_norm;
                        else draw_coords = intCoordsToNorm(ri.start_coords);
                        drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, draw_coords, DEFAULT_SCALE, IDENTITY_QUATERNION);
                    }
                }
            }
        }

        /*
        // temp draw outline around trailing hitboxes
        FOR(th_index, MAX_TRAILING_HITBOX_COUNT)
        {
            TrailingHitbox th = next_world_state.trailing_hitboxes[th_index];
            if (th.frames == 0 || th.hit_direction != NO_DIRECTION) continue;
            drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, intCoordsToNorm(th.coords), DEFAULT_SCALE, IDENTITY_QUATERNION);
        }
        */

		// draw selected id info
        if (editor_state.editor_mode == SELECT || editor_state.editor_mode == SELECT_WRITE)
        {
            Vec2 center_screen = { (float)SCREEN_WIDTH_PX / 2, (float)SCREEN_HEIGHT_PX / 2 };
            drawText(editor_state.edit_buffer.string, center_screen, DEFAULT_TEXT_SCALE);

            if (editor_state.selected_id > 0)
            {
                Entity* e = getEntityFromId(editor_state.selected_id);
                if (e != 0) // TODO(spike): this guard is somewhat bad solution; i still persist selected_id even if that id doesn't exist anymore. this prevents crash, but later: on entity delete check if matches against id, and if so remove from editor_state.
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
                    drawDebugText(writing_field_text);

                    char next_level_text[256] = {0};
                    snprintf(next_level_text, sizeof(next_level_text), "next_level: %s", e->next_level);
                    drawDebugText(next_level_text);

                    char unlocked_by_text[256] = {0};
                    snprintf(unlocked_by_text, sizeof(unlocked_by_text), "unlocked_by: %s", e->unlocked_by);
                    drawDebugText(unlocked_by_text);

                    if (getTileType(e->coords) == RESET_BLOCK)
                    {
                        FOR(reset_index, MAX_RESET_COUNT)
						{
                            if (e->reset_info[reset_index].id == -1) continue;
							char reset_id_text[256] = {0};
                            snprintf(reset_id_text, sizeof(reset_id_text), "id of nr. %d reset: %d", reset_index, e->reset_info[reset_index].id);
                            drawDebugText(reset_id_text);
                        }
                    }
                }
                else
                {
                    drawDebugText("selected entity deleted");
                }
            }
            else
            {
                drawDebugText("no entity selected");
            }
        }

        // draw camera boundary lines
		if (time_until_input == 0 && tick_input.t_press && !(editor_state.editor_mode == SELECT_WRITE))
        {
            draw_camera_boundary = (draw_camera_boundary) ? false : true;
			time_until_input = META_INPUT_TIME_UNTIL_ALLOW;
        }
        if (draw_camera_boundary && in_overworld)
        {
        	int32 x_draw_offset = 0;
            int32 z_draw_offset = 0;

            Vec3 x_wall_scale = { (float)OVERWORLD_SCREEN_SIZE_X, 5, 0.01f };
            Vec3 z_wall_scale = { 0.01f, 5, (float)OVERWORLD_SCREEN_SIZE_Z };

            FOR(z_index, 5)
            {
				FOR(x_index, 5)
                {
                    Vec3 x_draw_coords = (Vec3){ (float)(x_draw_offset + camera_center_start.x), 3, (float)(z_draw_offset + camera_center_start.z) + ((float)OVERWORLD_SCREEN_SIZE_Z / 2) }; 
                    Vec3 z_draw_coords = (Vec3){ (float)(x_draw_offset + camera_center_start.x) - ((float)OVERWORLD_SCREEN_SIZE_X / 2), 3, (float)(z_draw_offset + camera_center_start.z) }; 
                    drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, x_draw_coords, x_wall_scale, IDENTITY_QUATERNION);
                    drawAsset(OUTLINE_DRAW_ID, OUTLINE_3D, z_draw_coords, z_wall_scale, IDENTITY_QUATERNION);
					x_draw_offset += OVERWORLD_SCREEN_SIZE_X;
                }
                x_draw_offset = 0;
                z_draw_offset += OVERWORLD_SCREEN_SIZE_Z;
            }
        }

        // decide which camera to use
        if (editor_state.do_wide_camera) camera.fov = 60.0f;
        else camera.fov = CAMERA_FOV;

        // write level to file on i press
        char level_path[64];
        buildLevelPathFromName(world_state.level_name, &level_path, true);
        char relative_level_path[64];
        buildLevelPathFromName(world_state.level_name, &relative_level_path, false);
        if (time_until_input == 0 && (editor_state.editor_mode == PLACE_BREAK || editor_state.editor_mode == SELECT) && tick_input.i_press) 
        {
            saveLevelRewrite(level_path, true);
            saveLevelRewrite(relative_level_path, true);
            writeSolvedLevelsToFile();
        }

        // write camera to file on c press
        if (time_until_input == 0 && (editor_state.editor_mode == PLACE_BREAK || editor_state.editor_mode == SELECT) && tick_input.c_press) 
        {
            {
                FILE* file = fopen(level_path, "rb+");
                int32 positions[16] = {0};
                int32 count = getCountAndPositionOfChunk(file, CAMERA_CHUNK_TAG, positions);

                if (count > 0)
                {
                    fseek(file, positions[0], SEEK_SET);
                    writeCameraToFile(file, &camera);
                }
                else
                {
                    fseek(file, 0, SEEK_END);
                    writeCameraToFile(file, &camera);
                }
                fclose(file);
            }

            {
                FILE* file = fopen(relative_level_path, "rb+");
                int32 positions[16] = {0};
                int32 count = getCountAndPositionOfChunk(file, CAMERA_CHUNK_TAG, positions);

                if (count > 0)
                {
                    fseek(file, positions[0], SEEK_SET);
                    writeCameraToFile(file, &camera);
                }
                else
                {
                    fseek(file, 0, SEEK_END);
                    writeCameraToFile(file, &camera);
                }
                fclose(file);
            }
        }

		if (time_until_input > 0) time_until_input--;

        accumulator -= PHYSICS_INCREMENT;

        rendererSubmitFrame(assets_to_load, camera);
        FOR(i, 1024) assets_to_load[i].instance_count = 0;
	}

    rendererDraw();
}
