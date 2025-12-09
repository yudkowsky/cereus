#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> // TODO(spike): also temporary, for sin/cos
#include <stdio.h> // TODO(spike): "temporary", for fopen 

#define local_persist static
#define global_variable static
#define internal static

#define FOR(i, n) for (int i = 0; i < n; i++)

typedef enum TileType
{
    NONE    	= 0,
    VOID    	= 1,
    GRID    	= 2,
    WALL    	= 3,
    BOX     	= 4,
    PLAYER  	= 5,
    MIRROR  	= 6,
    CRYSTAL 	= 7,
    PACK        = 8,
    PERM_MIRROR = 9,

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

    // for pack
    bool pack_detached;
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
    bool falling;
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
}
WorldState;

typedef struct EditorState
{
    bool editor_mode;
    TileType picked_tile;
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

const double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0;

const float TAU = 6.2831853071f;

const float SENSITIVITY = 0.005f;
const float MOVE_STEP = 0.075f;
const Vec3 DEFAULT_SCALE = { 1.0f,  1.0f,  1.0f  };
const Vec3 PLAYER_SCALE  = { 0.75f, 0.75f, 0.75f };
const Vec3 ORTHOGONAL_LASER_SCALE = { 0.125f, 0.125f, 1.0f   };
const Vec3 DIAGONAL_LASER_SCALE   = { 0.125f, 0.125f, 1.415f };
const float RAYCAST_SEEK_LENGTH = 20.0f;

const int32 EDITOR_INPUT_TIME_UNTIL_ALLOW = 9;
const int32 MOVE_ANIMATION_TIME = 9;
const int32 PUSH_ANIMATION_TIME = 9;
const int32 ROLL_ANIMATION_TIME = 18;
const int32 TURN_ANIMATION_TIME = 9;
const int32 FALL_ANIMATION_TIME = 8; // hard coded (because acceleration in first fall anim must be constant)
const int32 FAILED_ANIMATION_TIME = 8;

const int32 TRAILING_HITBOX_TIME = 4;
const int32 TIME_BEFORE_DIAGONAL_TRAILING_HITBOX = 3;
const int32 TRAILING_HITBOX_PACK_DIAGONAL_TURN_TIME = 4;
const int32 TRAILING_HITBOX_PACK_ORTHOGONAL_TURN_TIME = 2;

const int32 SUCCESSFUL_TP_TIME = 8;
const int32 FAILED_TP_TIME = 8;

const int32 LASER_BUFFER_MOVING_TIME_ALLOWED_FOR_PASSTHROUGH = 5; // actually inverted: this is 8 - x frames

const int32 MAX_ENTITY_INSTANCE_COUNT = 32;
const int32 MAX_ENTITY_PUSH_COUNT = 32;
const int32 MAX_ANIMATION_COUNT = 32;
const int32 MAX_LASER_TRAVEL_DISTANCE = 48;
const int32 MAX_PSEUDO_SOURCE_COUNT = 32;
const int32 MAX_PUSHABLE_STACK_SIZE = 32;
const int32 MAX_TRAILING_HITBOX_COUNT = 64;

const int32 UNDO_BUFFER_SIZE = 256; // remember to modify undo_buffer
 
const Int3 AXIS_X = { 1, 0, 0 };
const Int3 AXIS_Y = { 0, 1, 0 };
const Int3 AXIS_Z = { 0, 0, 1 };
const Vec3 IDENTITY_TRANSLATION = { 0, 0, 0 };
const Vec4 IDENTITY_QUATERNION  = { 0, 0, 0, 1 };

const int32 PLAYER_ID = 1;
const int32 PACK_ID   = 2;
const int32 ID_OFFSET_BOX     	  = 100 * 1;
const int32 ID_OFFSET_MIRROR  	  = 100 * 2;
const int32 ID_OFFSET_CRYSTAL 	  = 100 * 3;
const int32 ID_OFFSET_SOURCE  	  = 100 * 4;
const int32 ID_OFFSET_PERM_MIRROR = 100 * 5;

const char* const void_path        = "data/sprites/void.png";
const char* const grid_path        = "data/sprites/grid.png";
const char* const wall_path        = "data/sprites/wall.png";
const char* const box_path         = "data/sprites/box.png";
const char* const player_path      = "data/sprites/player.png";
const char* const mirror_path      = "data/sprites/mirror.png";
const char* const crystal_path     = "data/sprites/crystal.png";
const char* const pack_path    	   = "data/sprites/pack.png";
const char* const perm_mirror_path = "data/sprites/perm-mirror.png";

const char* const player_ghost_path = "data/sprites/player-ghost.png";
const char* const pack_ghost_path   = "data/sprites/pack-ghost.png";

const char* const red_player_path     = "data/sprites/player-red.png";
const char* const green_player_path   = "data/sprites/player-green.png";
const char* const blue_player_path    = "data/sprites/player-blue.png";
const char* const magenta_player_path = "data/sprites/player-magenta.png";
const char* const yellow_player_path  = "data/sprites/player-yellow.png";
const char* const cyan_player_path    = "data/sprites/player-cyan.png";
const char* const white_player_path   = "data/sprites/player-white.png";

const char* const laser_red_path      = "data/sprites/laser-red.png";
const char* const laser_green_path    = "data/sprites/laser-green.png";
const char* const laser_blue_path     = "data/sprites/laser-blue.png";
const char* const laser_magenta_path  = "data/sprites/laser-magenta.png";
const char* const laser_yellow_path   = "data/sprites/laser-yellow.png";
const char* const laser_cyan_path     = "data/sprites/laser-cyan.png";
const char* const laser_white_path    = "data/sprites/laser-white.png";

const char* const source_red_path     = "data/sprites/source-red.png";
const char* const source_green_path   = "data/sprites/source-green.png";
const char* const source_blue_path    = "data/sprites/source-blue.png";
const char* const source_magenta_path = "data/sprites/source-magenta.png";
const char* const source_yellow_path  = "data/sprites/source-yellow.png";
const char* const source_cyan_path    = "data/sprites/source-cyan.png";
const char* const source_white_path   = "data/sprites/source-white.png";

char* level_path = "w:/cereus/data/levels/testing.txt"; // absolute path required to modify original file. used by default if no command line input
char* system_command_line = "testing";
char level_path_buffer[256] = "w:/cereus/data/levels/";
Int3 level_dim = {0};

Camera camera = {0};
float camera_yaw = 0.0f;
float camera_pitch = 0.0f;

AssetToLoad assets_to_load[256] = {0};

WorldState world_state = {0};
WorldState next_world_state = {0};
WorldState undo_buffer[256] = {0};
int32 undo_buffer_position = 0;

Animation animations[32];
int32 time_until_input = 0;
EditorState editor_state = {0};
LaserBuffer laser_buffer[1024] = {0};

int32 diagonal_turn_hitbox_timer = 0;
Int3 diagonal_turn_hitbox_coords = {0};
TrailingHitbox trailing_hitboxes[64] = {0};

bool do_player_ghost = false;
bool do_pack_ghost = false;
Int3 player_ghost_coords = {0};
Int3 pack_ghost_coords = {0};
Direction player_ghost_direction = NO_DIRECTION;
Direction pack_ghost_direction = NO_DIRECTION;

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

Vec3 intCoordsToNorm(Int3 int_coords) {
    return (Vec3){ (float)int_coords.x, (float)int_coords.y, (float)int_coords.z }; }

Int3 normCoordsToInt(Vec3 norm_coords) {
	return (Int3){ (int32)floorf(norm_coords.x + 0.5f), (int32)floorf(norm_coords.y + 0.5f), (int32)floorf(norm_coords.z + 0.5f) }; }

bool intCoordsWithinLevelBounds(Int3 coords) {
    return (coords.x >= 0 && coords.y >= 0 && coords.z >= 0 && coords.x < level_dim.x && coords.y < level_dim.y && coords.z < level_dim.z); }

bool normCoordsWithinLevelBounds(Vec3 coords) {
    return (coords.x > 0 && coords.y > 0 && coords.z >= 0 && coords.x < level_dim.x && coords.y < level_dim.y && coords.z < level_dim.z); }

bool int3IsEqual(Int3 a, Int3 b) {
    return (a.x == b.x && a.y == b.y && a.z == b.z); }

Vec3 vec3Negate(Vec3 coords) {
    return (Vec3){ -coords.x, -coords.y, -coords.z }; }

Int3 int3Negate(Int3 coords) {
    return (Int3){ -coords.x, -coords.y, -coords.z }; }

bool vec3IsZero(Vec3 position) {
   	return (position.x == 0 && position.y == 0 && position.z == 0); }

Vec3 vec3Add(Vec3 a, Vec3 b) {
	return (Vec3){ a.x+b.x, a.y+b.y, a.z+b.z }; }

Int3 int3Add(Int3 a, Int3 b) {	
	return (Int3){ a.x+b.x, a.y+b.y, a.z+b.z }; }

Vec3 vec3Subtract(Vec3 a, Vec3 b) {
    return (Vec3){ a.x-b.x, a.y-b.y, a.z-b.z }; }

Vec3 vec3ScalarMultiply(Vec3 position, float scalar) {
    return (Vec3){ position.x*scalar, position.y*scalar, position.z*scalar }; }

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
        case BOX:     	  entity_group = next_world_state.boxes;    break;
        case MIRROR:  	  entity_group = next_world_state.mirrors;  break;
        case CRYSTAL: 	  entity_group = next_world_state.crystals; break;
        case PERM_MIRROR: entity_group = next_world_state.perm_mirrors; break;
        case PLAYER: return &next_world_state.player;
        case PACK:	 return &next_world_state.pack;
        default: return 0;
    }
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (int3IsEqual(entity_group[entity_index].coords, coords)) return &entity_group[entity_index];
    }
    return 0;
}

int32 getEntityId(Int3 coords)
{
	Entity *entity = getEntityPointer(coords);
    return entity->id;
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
        default: return 0;
    }
}

int32 getEntityCount(Entity *entity_group)
{
    int32 count = 0;
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
	{
		if (entity_group[entity_index].id == -1) continue;
        count++;
    }
    return count;
}

int32 entityIdOffset(Entity *entity)
{
    if (entity == next_world_state.boxes)    	 return ID_OFFSET_BOX;
    if (entity == next_world_state.mirrors)  	 return ID_OFFSET_MIRROR;
    if (entity == next_world_state.crystals) 	 return ID_OFFSET_CRYSTAL;
    if (entity == next_world_state.sources)  	 return ID_OFFSET_SOURCE;
    if (entity == next_world_state.perm_mirrors) return ID_OFFSET_PERM_MIRROR;
    return 0;
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
            yaw = 0.0f;
            do_yaw = true;
            break;
        case WEST: 
            yaw = 0.25f  * TAU;
            do_yaw = true;
            break;
        case SOUTH: 
            yaw = 0.5f   * TAU;
            do_yaw = true;
            break;
        case EAST:
            yaw = -0.25f * TAU;
            do_yaw = true;
            break;
        case UP:
            roll = 0.25f * TAU;
			do_roll = true;
            break;
        case DOWN:
            roll = -0.25f * TAU;
			do_roll = true;
            break;
        case NORTH_WEST:
			yaw = 0.125f * TAU;
            do_yaw = true;
            break;
        case NORTH_EAST:
            yaw = -0.125f * TAU;
            do_yaw = true;
            break;
        case SOUTH_WEST:
            yaw = 0.375f * TAU;
            do_yaw = true;
            break;
        case SOUTH_EAST:
            yaw = -0.375f * TAU;
            do_yaw = true;
            break;
        case UP_NORTH:
            yaw  = 0.0f;         roll = 0.125f * TAU;
            do_yaw = true;       do_roll = true;
            break;
        case UP_SOUTH:
            yaw  = 0.5f * TAU;   roll = -0.125f * TAU;
            do_yaw = true;       do_roll = true;
            break;
        case UP_WEST:
            yaw  = 0.25f * TAU;  roll = -0.125f * TAU;
            do_yaw = true; 		 do_roll = true;
            roll_z = true;
            break;
        case UP_EAST:
            yaw  = -0.25f * TAU; roll = 0.125f * TAU;
            do_yaw = true; 		 do_roll = true;
            roll_z = true;
            break;
        case DOWN_NORTH:
            yaw  = 0.0f;         roll = -0.125f * TAU;
            do_yaw = true;       do_roll = true;
            break;
        case DOWN_SOUTH:
            yaw  = 0.5f * TAU;   roll = 0.125f * TAU;
            do_yaw = true; 	     do_roll = true;
            break;
        case DOWN_WEST:
            yaw  = 0.25f * TAU;  roll = 0.125f * TAU;
            do_yaw = true;       do_roll = true;
            roll_z = true;
            break;
        case DOWN_EAST:
            yaw  = -0.25f * TAU; roll = -0.125f * TAU;
            do_yaw = true;       do_roll = true;
            roll_z = true;
            break;
        default: return (Vec4){ 0, 0, 0, 0 };
    }

	if (do_yaw && !do_roll) return quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), yaw);
    if (!do_yaw && do_roll) return quaternionFromAxisAngle(intCoordsToNorm(roll_z ? AXIS_Z : AXIS_X), roll);
    if (do_yaw && do_roll)
 	{
        Vec4 quaternion_yaw  = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), yaw);
        Vec4 quaternion_roll = quaternionFromAxisAngle(intCoordsToNorm(roll_z ? AXIS_Z : AXIS_X), roll);
        return quaternionMultiply(quaternion_roll, quaternion_yaw);
    }
    return IDENTITY_QUATERNION;
}

void setEntityInstanceInGroup(Entity* entity_group, Int3 coords, Direction direction, Color color) 
{
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (entity_group[entity_index].id != -1) continue;
        entity_group[entity_index].coords = coords;
        entity_group[entity_index].position_norm = intCoordsToNorm(coords); 
        entity_group[entity_index].direction = direction;
        entity_group[entity_index].rotation_quat = directionToQuaternion(direction, true);
        entity_group[entity_index].id = entity_index + entityIdOffset(entity_group);
        entity_group[entity_index].color = color;
        setTileDirection(direction, coords);
        break;
    }
}

// FILE I/O

void loadFileToBuffer(char* path)
{
    // get level dimensions
    FILE *file = fopen(path, "rb");
	uint8 byte = 0;
    fseek(file, 1, SEEK_CUR); // skip the first byte
	fread(&byte, 1, 1, file);
    level_dim.x = byte;
    fread(&byte, 1, 1, file);
    level_dim.y = byte;
    fread(&byte, 1, 1, file);
    level_dim.z = byte;

    uint8 buffer[32768]; // level_dim.x*level_dim.y*level_dim.z * 2 for color bytes
	fread(&buffer, 1, level_dim.x*level_dim.y*level_dim.z * 2, file);
	fclose(file);
    memcpy(next_world_state.buffer, buffer, level_dim.x*level_dim.y*level_dim.z * 2);
}

void writeBufferToFile(char* path)
{
    FILE *file = fopen(path, "rb+");

    fseek(file, 4, SEEK_CUR);
	fwrite(world_state.buffer, 1, 32768, file);
    fclose(file);
}

// DRAW ASSET

char* getPath(TileType tile)
{
    switch(tile)
    {
        case NONE:        return 0;
        case VOID:        return void_path;
        case GRID:        return grid_path;
        case WALL:        return wall_path;
        case BOX:         return box_path;
        case PLAYER:      return player_path;
        case MIRROR:      return mirror_path;
        case CRYSTAL:     return crystal_path;
        case PACK:    	  return pack_path;
        case PERM_MIRROR: return perm_mirror_path;

        case LASER_RED:     return laser_red_path;
        case LASER_GREEN:	return laser_green_path;
        case LASER_BLUE:	return laser_blue_path;
        case LASER_MAGENTA:	return laser_magenta_path;
        case LASER_YELLOW:	return laser_yellow_path;
        case LASER_CYAN:	return laser_cyan_path;
        case LASER_WHITE:	return laser_white_path;

        case SOURCE_RED:	 return source_red_path;
        case SOURCE_GREEN:	 return source_green_path;
        case SOURCE_BLUE:	 return source_blue_path;
        case SOURCE_MAGENTA: return source_magenta_path;
        case SOURCE_YELLOW:	 return source_yellow_path;
        case SOURCE_CYAN:	 return source_cyan_path;
        case SOURCE_WHITE:	 return source_white_path;
        default: return 0;
    }
}

// assuming one path -> one asset type.
void drawAsset(char* path, AssetType type, Vec3 coords, Vec3 scale, Vec4 rotation)
{
	int32 asset_location = -1;
    for (int32 asset_index = 0; asset_index < 256; asset_index++)
    {
        if (assets_to_load[asset_index].path == path) 
        {
            asset_location = asset_index;
            break;
        }
        if (assets_to_load[asset_index].path == 0)
        {
            asset_location = asset_index;
            assets_to_load[asset_location].path = path;
            assets_to_load[asset_location].type = type;
            break;
        }
    }
    switch (type)
    {
        case SPRITE_2D:
        {
            return;
        }
        case CUBE_3D:
        {
            assets_to_load[asset_location].coords[assets_to_load[asset_location].instance_count]   = coords;
            assets_to_load[asset_location].scale[assets_to_load[asset_location].instance_count]    = scale;
            assets_to_load[asset_location].rotation[assets_to_load[asset_location].instance_count] = rotation;
            assets_to_load[asset_location].instance_count++;
            return;
        }
        case MODEL_3D:
        {
            return;
        }
    }
}

void drawEntityLoop(Entity* entity_group, char* path, AssetType type, Vec3 scale)
{
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (entity_group[entity_index].id == -1) continue;
        drawAsset(path, type, entity_group[entity_index].position_norm, scale, entity_group[entity_index].rotation_quat);
    }
}

// RAYCAST ALGORITHM FOR EDITOR PLACE/DESTROY

RaycastHit raycastHitCube(Vec3 start, Vec3 direction, float max_distance)
{
	RaycastHit output = {0};
    Int3 current_cube = normCoordsToInt(start);
    start.x += 0.5;
    start.y += 0.5;
    start.z += 0.5;

    // TODO(spike): temporary - need to fix so it works from outside bounds
	if (!intCoordsWithinLevelBounds(current_cube)) return output;

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

        if (!intCoordsWithinLevelBounds(current_cube)) break;

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

        case NORTH_WEST: return int3Add(coords, int3Add(int3Negate(AXIS_Z), int3Negate(AXIS_X)));
        case NORTH_EAST: return int3Add(coords, int3Add(int3Negate(AXIS_Z), AXIS_X));
        case SOUTH_WEST: return int3Add(coords, int3Add(AXIS_Z, int3Negate(AXIS_X)));
        case SOUTH_EAST: return int3Add(coords, int3Add(AXIS_Z, AXIS_X));
                               
        case UP_NORTH:   return int3Add(coords, int3Add(int3Negate(AXIS_Z), AXIS_Y));
        case UP_SOUTH:   return int3Add(coords, int3Add(AXIS_Z, AXIS_Y));
        case UP_WEST:	 return int3Add(coords, int3Add(int3Negate(AXIS_X), AXIS_Y));
        case UP_EAST:    return int3Add(coords, int3Add(AXIS_X, AXIS_Y));
                               
        case DOWN_NORTH: return int3Add(coords, int3Add(int3Negate(AXIS_Z), int3Negate(AXIS_Y)));
        case DOWN_SOUTH: return int3Add(coords, int3Add(AXIS_Z, int3Negate(AXIS_Y)));
        case DOWN_WEST:  return int3Add(coords, int3Add(int3Negate(AXIS_X), int3Negate(AXIS_Y)));
        case DOWN_EAST:  return int3Add(coords, int3Add(AXIS_X, int3Negate(AXIS_Y)));

        default: return (Int3){0};
    }
}

Vec3 rollingAxis(Direction direction)
{
	Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 rolling = intCoordsToNorm(getNextCoords(normCoordsToInt(IDENTITY_TRANSLATION), direction));
	return vec3CrossProduct(up, rolling);
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

void zeroAnimations(int32 id)
{
    FOR(animation_index, MAX_ANIMATION_COUNT) if (animations[animation_index].id == id) memset(&animations[animation_index], 0, sizeof(Animation));
}

// automatically queues if given object is already being moved around. assumes object is entity, because requires id - easily fixable if required. assumes max two animations on any given object (max one queued)
void createInterpolationAnimation(Vec3 position_a, Vec3 position_b, Vec3* position_to_change, Vec4 rotation_a, Vec4 rotation_b, Vec4* rotation_to_change, int32 entity_id, int32 animation_frames, bool falling)
{
    int32 next_free_array[2] = {0};
    int32* next_free_output = findNextFreeInAnimations(next_free_array, entity_id);
    int32 animation_index = next_free_output[0];
    int32 queue_time = next_free_output[1];

	animations[animation_index].id = entity_id;
    animations[animation_index].frames_left = animation_frames + queue_time; 
    animations[animation_index].falling = falling;

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

void createRollingAnimation(Vec3 position, Direction direction, Vec3* position_to_change, Vec4 rotation_a, Vec4 rotation_b, Vec4* rotation_to_change, int32 entity_id, int32 animation_frames)
{	
    int32 next_free_array[2] = {0};
    int32* next_free_output = findNextFreeInAnimations(next_free_array, entity_id);
    int32 animation_index = next_free_output[0];
    int32 queue_time = next_free_output[1];

    animations[animation_index].id = entity_id;
    animations[animation_index].frames_left = animation_frames + queue_time; 
    animations[animation_index].rotation_to_change = rotation_to_change;
    animations[animation_index].position_to_change = position_to_change;

    // translation on circle arc
    Vec3 start_translation = {0};
    start_translation.y -= 0.5f;
    switch (direction)
    {
        case NORTH: start_translation.z -= 0.5f; break;
        case WEST:  start_translation.x -= 0.5f; break;
        case SOUTH: start_translation.z += 0.5f; break;
        case EAST:  start_translation.x += 0.5f; break;
        default: return;
    }

    Vec3 pivot_point = vec3Add(position, start_translation);
    Vec3 axis = rollingAxis(direction);
    Vec3 pivot_to_cube_center = vec3Subtract(position, pivot_point);
	float d_theta_per_frame = (TAU*0.25f)/(float)animation_frames;

    for (int frame_index = 0; frame_index < animation_frames; frame_index++)
    {
        // rotation
        float param = (float)(frame_index + 1) / animations[animation_index].frames_left;
        animations[animation_index].rotation[animation_frames-(1+frame_index)] 
        = quaternionNormalize(quaternionAdd(quaternionScalarMultiply(rotation_a, 1.0f - param), quaternionScalarMultiply(rotation_b, param)));
        
        // translation
		float theta = (frame_index+1) * d_theta_per_frame;
        Vec4 roll = quaternionFromAxisAngle(axis, theta);
        Vec3 relative_rotation = vec3RotateByQuaternion(pivot_to_cube_center, roll);
        animations[animation_index].position[animation_frames-(1+frame_index)] = vec3Add(pivot_point, relative_rotation);
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

void doFailedWalkAnimations(Int3 next_player_coords)
{
    createFailedWalkAnimation(intCoordsToNorm(next_world_state.player.coords),
                              intCoordsToNorm(next_player_coords),
                              &next_world_state.player.position_norm, 1);
    if (!next_world_state.pack.pack_detached) createFailedWalkAnimation(intCoordsToNorm(next_world_state.pack.coords),
                      					         intCoordsToNorm(next_world_state.player.coords),
                              					 &next_world_state.pack.position_norm, 2);
}

void createFailedPlayerRotationAnimation(Vec4 start_rotation, Vec4 input_direction_as_quat, Vec4* rotation_to_change, int32 entity_id)
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
    createFailedPlayerRotationAnimation(directionToQuaternion(next_world_state.player.direction, true),
                                        directionToQuaternion(input_direction, true),
                                        &next_world_state.player.rotation_quat, PLAYER_ID);
    createFailedPackRotationAnimation(intCoordsToNorm(next_world_state.player.coords), 
                                      intCoordsToNorm(next_world_state.pack.coords), 
                                      oppositeDirection(input_direction), clockwise, 
                                      &next_world_state.pack.position_norm, &next_world_state.pack.rotation_quat, PACK_ID);
}

/*
void pauseAnimation(int32 id, int32 frame_count)
{
    FOR(animation_index, MAX_ANIMATION_COUNT)
    {
		if (animations[animation_index].id != id || animations[animation_index].frames_left == 0) continue;
        animations[animation_index].frames_left += frame_count;
    }
}
*/

bool entityInMotion(Entity* entity)
{
    if (entity == 0) return false; // if null pointer return not in motion
    if (entity->previously_moving_sideways != 0 || entity->falling_time != 0) return true;
    else return false;
}

bool findFallingInAnimations(int32 id)
{
    FOR(animation_index, MAX_ANIMATION_COUNT)
    {
        if (animations[animation_index].id != id) continue;
        if (animations[animation_index].falling == false) continue;
        else return true;
    }
    return false;
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
    animations[animation_index].falling = true;

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

// PUSH / ROLL ENTITES

int32 findNextFreeInTrailingHitboxes()
{
    FOR(find_hitbox_index, MAX_TRAILING_HITBOX_COUNT)
    {
        if (trailing_hitboxes[find_hitbox_index].frames != 0) continue;
        return find_hitbox_index;
    }
    return 0;
}

void createTrailingHitbox(Int3 coords, int32 frames)
{
    int32 hitbox_index = findNextFreeInTrailingHitboxes();
    trailing_hitboxes[hitbox_index].coords = coords;
    trailing_hitboxes[hitbox_index].frames = frames;
}

bool isPushable(TileType tile)
{
    if (tile == BOX || tile == CRYSTAL || tile == MIRROR || tile == PACK) return true;
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

PushResult canPush(Int3 coords, Direction direction)
{
    Int3 current_coords = coords;
    TileType current_tile;
    for (int push_index = 0; push_index < MAX_ENTITY_PUSH_COUNT; push_index++) 
    {
		Entity* entity = getEntityPointer(current_coords);
    	current_coords = getNextCoords(current_coords, direction);
        current_tile = getTileType(current_coords);

        Int3 coords_ahead = getNextCoords(entity->coords, direction);
        Int3 coords_below = getNextCoords(entity->coords, DOWN);
        Int3 coords_below_and_ahead = getNextCoords(getNextCoords(entity->coords, DOWN), direction);
		if (entityInMotion(entity)) return PAUSE_PUSH;
		if (isPushable(getTileType(coords_ahead)) && entityInMotion(getEntityPointer(coords_ahead))) return PAUSE_PUSH;
        if (isPushable(getTileType(coords_below)) && entityInMotion(getEntityPointer(coords_below))) return PAUSE_PUSH;
        if (isPushable(getTileType(coords_below_and_ahead)) && entityInMotion(getEntityPointer(coords_below_and_ahead))) return PAUSE_PUSH;

        if (!intCoordsWithinLevelBounds(current_coords)) return FAILED_PUSH;
        if (current_tile == NONE) return CAN_PUSH;
        if (isSource(current_tile)) return FAILED_PUSH;
        if (current_tile == MIRROR) return FAILED_PUSH;
        if (current_tile == GRID) return FAILED_PUSH;
        if (current_tile == WALL) return FAILED_PUSH;
        if (current_tile == PERM_MIRROR) return FAILED_PUSH;
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

Push pushOnceWithoutAnimation(Int3 coords, Direction direction)
{
	Push entity_to_push = {0};

    Entity* entity = getEntityPointer(coords);
    entity_to_push.type = getTileType(coords);
    entity_to_push.previous_coords = coords;
    entity_to_push.entity = entity; 
    entity_to_push.new_coords = getNextCoords(coords, direction);

    entity->previously_moving_sideways = PUSH_ANIMATION_TIME;

    setTileType(NONE, entity_to_push.previous_coords);
    setTileDirection(NORTH, entity_to_push.previous_coords);

    setTileType(entity_to_push.type, entity_to_push.new_coords);
    setTileDirection(entity->direction, entity_to_push.new_coords);
    entity->coords = entity_to_push.new_coords;

    return entity_to_push;
}

void pushOnce(Int3 coords, Direction direction)
{
    Push entity_to_push = pushOnceWithoutAnimation(coords, direction);

    int32 id = getEntityId(entity_to_push.new_coords);
    createInterpolationAnimation(intCoordsToNorm(entity_to_push.previous_coords),
                                 intCoordsToNorm(entity_to_push.new_coords),
                                 &entity_to_push.entity->position_norm,
                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                 id, PUSH_ANIMATION_TIME, false); 
    createTrailingHitbox(coords, TRAILING_HITBOX_TIME);
}

// assumes stack is able to be pushed, at least a bit. checks if next is NONE, if so stops. 
void pushAll(Int3 coords, Direction direction, bool animations_on, bool limit_stack_size_to_one)
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
            if (animations_on) pushOnce(current_stack_coords, direction);
            else pushOnceWithoutAnimation(current_stack_coords, direction);
            current_stack_coords = getNextCoords(current_stack_coords, UP);
            if (limit_stack_size_to_one && stack_index == 0) break;
        }
        current_coords = getNextCoords(current_coords, oppositeDirection(direction));
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

void rollWithoutAnimation(Int3 coords, Direction direction)
{
    Entity* entity = getEntityPointer(coords);
	setTileType(NONE, coords);
    setTileDirection(NORTH, coords);
	entity->coords = getNextCoords(coords, direction);
	Direction new_direction = getNextMirrorState(entity->direction, direction);
    entity->direction = new_direction;
    setTileType(MIRROR, entity->coords);
    setTileDirection(new_direction, entity->coords);
}

void roll(Int3 coords, Direction direction)
{
    int32 id = getEntityId(coords);
    Entity* entity = getEntityPointer(coords);
	Vec4 quaternion_transform = quaternionNormalize(quaternionMultiply(quaternionFromAxisAngle(rollingAxis(direction), 0.25f*TAU), directionToQuaternion(entity->direction, true)));
    createRollingAnimation(intCoordsToNorm(coords), direction, &entity->position_norm, 
                           directionToQuaternion(entity->direction, true), quaternion_transform, &entity->rotation_quat,
                           id, ROLL_ANIMATION_TIME);
	rollWithoutAnimation(coords, direction);
    entity->previously_moving_sideways = ROLL_ANIMATION_TIME;
}

// LASERS

bool canMirrorReflect(Direction laser_direction, Direction mirror_direction)
{
    switch (mirror_direction)
    {
        case NORTH: if (laser_direction == WEST || laser_direction == EAST || laser_direction == UP_SOUTH || laser_direction == DOWN_NORTH) return false; break;
        case SOUTH: if (laser_direction == WEST || laser_direction == EAST || laser_direction == UP_NORTH || laser_direction == DOWN_SOUTH) return false; break;
        case WEST:  if (laser_direction == NORTH || laser_direction == SOUTH || laser_direction == UP_EAST || laser_direction == DOWN_WEST) return false; break;
        case EAST:  if (laser_direction == NORTH || laser_direction == SOUTH || laser_direction == UP_WEST || laser_direction == DOWN_EAST) return false; break;
        case UP:    if (laser_direction == UP || laser_direction == DOWN || laser_direction == NORTH_EAST || laser_direction == SOUTH_WEST) return false; break;
        case DOWN:  if (laser_direction == UP || laser_direction == DOWN || laser_direction == NORTH_WEST || laser_direction == SOUTH_EAST) return false; break;
        default: return 0;
    }
    return true;
}

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
        case NORTH: switch (laser_direction)
        {
            case NORTH_WEST: return DOWN_WEST;
            case NORTH_EAST: return DOWN_EAST;
            case SOUTH_WEST: return UP_WEST;
            case SOUTH_EAST: return UP_EAST;

            case UP_NORTH:   return DOWN_SOUTH;
            //case UP_SOUTH:
            case UP_WEST:    return SOUTH_WEST;
            case UP_EAST:    return SOUTH_EAST;

            //case DOWN_NORTH: 
            case DOWN_SOUTH: return UP_NORTH;
            case DOWN_WEST:  return NORTH_WEST;
            case DOWN_EAST:  return NORTH_EAST;

            default: return 0;
        }
        case SOUTH: switch (laser_direction)
        {
            case NORTH_WEST: return UP_WEST;
            case NORTH_EAST: return UP_EAST;
            case SOUTH_WEST: return DOWN_WEST;
            case SOUTH_EAST: return DOWN_EAST;

            //case UP_NORTH:
            case UP_SOUTH:   return DOWN_NORTH;
            case UP_WEST:	 return NORTH_WEST;
            case UP_EAST:    return NORTH_EAST;

            case DOWN_NORTH: return UP_SOUTH;
            //case DOWN_SOUTH: 
            case DOWN_WEST:  return SOUTH_WEST;
            case DOWN_EAST:  return SOUTH_EAST;

            default: return 0;
        }
        case WEST: switch (laser_direction)
        {
            case NORTH_WEST: return DOWN_NORTH;
            case NORTH_EAST: return UP_NORTH;
            case SOUTH_WEST: return DOWN_SOUTH;
            case SOUTH_EAST: return UP_SOUTH;

            case UP_NORTH:   return NORTH_EAST;
            case UP_SOUTH:   return SOUTH_EAST;
            case UP_WEST:    return DOWN_EAST;
            //case UP_EAST:    

            case DOWN_NORTH: return NORTH_WEST;
            case DOWN_SOUTH: return SOUTH_WEST;
            //case DOWN_WEST:
            case DOWN_EAST:  return UP_WEST;

            default: return 0;
        }
        case EAST: switch (laser_direction)
        {
            case NORTH_WEST: return UP_NORTH;
            case NORTH_EAST: return DOWN_NORTH;
            case SOUTH_WEST: return UP_SOUTH;
            case SOUTH_EAST: return DOWN_SOUTH;

            case UP_NORTH:   return NORTH_WEST;
            case UP_SOUTH:   return SOUTH_WEST;
            //case UP_WEST:
            case UP_EAST:    return DOWN_WEST;

            case DOWN_NORTH: return NORTH_EAST;
            case DOWN_SOUTH: return SOUTH_EAST;
            case DOWN_WEST:  return UP_EAST;
            //case DOWN_EAST:

            default: return 0;
        }
        case UP: switch (laser_direction)
        {
            case NORTH: 	 return EAST;
            case SOUTH: 	 return WEST;
            case WEST:  	 return SOUTH;
            case EAST:  	 return NORTH;

            case NORTH_WEST: return SOUTH_EAST;
            //case NORTH_EAST:
            //case SOUTH_WEST:
            case SOUTH_EAST: return NORTH_WEST;

            case UP_NORTH:   return UP_EAST;
            case UP_SOUTH:   return UP_WEST;
            case UP_WEST:    return UP_SOUTH;
            case UP_EAST:    return UP_NORTH;

            case DOWN_NORTH: return DOWN_EAST;
            case DOWN_SOUTH: return DOWN_WEST;
            case DOWN_WEST:  return DOWN_SOUTH;
            case DOWN_EAST:  return DOWN_NORTH;

            default: return 0;
        }
        case DOWN: switch (laser_direction)
        {
            case NORTH: 	 return WEST;
            case SOUTH: 	 return EAST;
            case WEST:  	 return NORTH;
            case EAST:  	 return SOUTH;

            //case NORTH_WEST: 
            case NORTH_EAST: return SOUTH_WEST;
            case SOUTH_WEST: return NORTH_EAST;
            //case SOUTH_EAST:

            case UP_NORTH:   return UP_WEST;
            case UP_SOUTH:	 return UP_EAST;
            case UP_WEST:    return UP_NORTH;
            case UP_EAST:    return UP_SOUTH;

            case DOWN_NORTH: return DOWN_WEST;
            case DOWN_SOUTH: return DOWN_EAST;
            case DOWN_WEST:  return DOWN_NORTH;
            case DOWN_EAST:  return DOWN_SOUTH;

			default: return 0;
        }
        default: return 0;
    }
}

Direction getRedDirectionAtCrystal(Direction input_direction)
{
    switch(input_direction)
    {
        case NORTH:      return NORTH_WEST;
        case NORTH_WEST: return WEST;
        case WEST:		 return SOUTH_WEST;
    	case SOUTH_WEST: return SOUTH;
    	case SOUTH:      return SOUTH_EAST;
        case SOUTH_EAST: return EAST;
        case EAST:       return NORTH_EAST;
        case NORTH_EAST: return NORTH;
        default: return 0;
    }
}

Direction getBlueDirectionAtCrystal(Direction input_direction)
{
    switch(input_direction)
    {
        case NORTH:      return NORTH_EAST;
        case NORTH_WEST: return NORTH;
        case WEST:		 return NORTH_WEST;
    	case SOUTH_WEST: return WEST;
    	case SOUTH:      return SOUTH_WEST;
        case SOUTH_EAST: return SOUTH;
        case EAST:       return SOUTH_EAST;
        case NORTH_EAST: return EAST;
        default: return 0;
    }
}

bool isParallelToXZ(Direction direction)
{
    Int3 test_coords = getNextCoords(normCoordsToInt(IDENTITY_TRANSLATION), direction);
	if (test_coords.y != 0) return false;
	else return true;
}

LaserColor getLaserColor(TileType tile)
{
    LaserColor laser_color = {0};
    if (tile == LASER_RED   || tile == LASER_MAGENTA || tile == LASER_YELLOW  || tile == LASER_WHITE) laser_color.red = true;
    if (tile == LASER_GREEN || tile == LASER_YELLOW  || tile == LASER_CYAN    || tile == LASER_WHITE) laser_color.green = true;
    if (tile == LASER_BLUE  || tile == LASER_CYAN    || tile == LASER_MAGENTA || tile == LASER_WHITE) laser_color.blue = true;
    return laser_color;
}

bool isSourcePrimary(TileType tile)
{
    switch (tile)
    {
        case SOURCE_RED:   return true;
        case SOURCE_GREEN: return true;
        case SOURCE_BLUE:  return true;
        default: return false;
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

void addPrimarySource(Entity* new_source, Entity* original_source, int32 *total_source_count, Color color)
{
    new_source->coords = original_source->coords;
    new_source->direction = original_source->direction;
    new_source->id = 10000 + original_source->id;
    new_source->color = color;
    (*total_source_count)++;
}

bool isDiagonal(Direction direction)
{
    if (direction == NORTH || direction == SOUTH || direction == WEST || direction == EAST || direction == UP || direction == DOWN) return false;
    else return true;
}

bool laserPassthroughAllowed(Entity* entity)
{
    if (entity->previously_moving_sideways > LASER_BUFFER_MOVING_TIME_ALLOWED_FOR_PASSTHROUGH || entity->falling_time > LASER_BUFFER_MOVING_TIME_ALLOWED_FOR_PASSTHROUGH) return true;
    return false;
}

int32 updateLaserBuffer(void)
{
    memset(laser_buffer, 0, sizeof(laser_buffer));
    int32 laser_tile_count = 0;
    int32 total_source_count = getEntityCount(next_world_state.sources);
    Entity sources_as_primary[32]; 
	memcpy(sources_as_primary, next_world_state.sources, sizeof(Entity) * MAX_PSEUDO_SOURCE_COUNT);
        
    // set these to 0 before we start checking
    next_world_state.player.hit_by_red   = false;
    next_world_state.player.hit_by_blue  = false;
    next_world_state.player.green_hit = (GreenHit){0};

    for (int source_index = 0; source_index < MAX_PSEUDO_SOURCE_COUNT; source_index++)
    {
        Entity* entity = &sources_as_primary[source_index];
        if (entity->id == -1) continue;
        Direction current_direction = entity->direction;
        Int3 current_coords = getNextCoords(entity->coords, current_direction);

        switch (entity->color) 
        {
            case MAGENTA: addPrimarySource(&sources_as_primary[total_source_count], entity, &total_source_count, BLUE);  break;
            case YELLOW:  addPrimarySource(&sources_as_primary[total_source_count], entity, &total_source_count, GREEN); break;
            case CYAN:    addPrimarySource(&sources_as_primary[total_source_count], entity, &total_source_count, BLUE); break;
            case WHITE:
            {
                addPrimarySource(&sources_as_primary[total_source_count], entity, &total_source_count, GREEN); 
                addPrimarySource(&sources_as_primary[total_source_count], entity, &total_source_count, BLUE); // creates a duplicate ID here. still unclear if i even want to use these though
                break;
            }
            default: break;
        }

        for (int laser_index = 0; laser_index < MAX_LASER_TRAVEL_DISTANCE; laser_index++)
        {
            FOR(trailing_hitbox_index, MAX_TRAILING_HITBOX_COUNT) 
            {
                if (int3IsEqual(current_coords, trailing_hitboxes[trailing_hitbox_index].coords) && trailing_hitboxes[trailing_hitbox_index].frames > 0)
                {
                    goto laser_instance_stop;
                }
            }

            LaserColor laser_color = colorToLaserColor(entity->color);
            if (!intCoordsWithinLevelBounds(current_coords)) break;

            switch(getTileType(current_coords))
            {
                case PLAYER:
                {
                    Entity* player = &next_world_state.player;
                    if (!laserPassthroughAllowed(player) || ((player->previously_moving_sideways > 0) && player->direction == oppositeDirection(current_direction)))
                    {
                        if (laser_color.red)   next_world_state.player.hit_by_red   = true;
                        if (laser_color.green) 
                        {
                            switch (current_direction)
                            {
                                case NORTH: next_world_state.player.green_hit.north = true; break;
                                case WEST:  next_world_state.player.green_hit.west  = true; break;
                                case SOUTH: next_world_state.player.green_hit.south = true; break;
                                case EAST:  next_world_state.player.green_hit.east  = true; break;
                                case UP:    next_world_state.player.green_hit.up    = true; break;
                                case DOWN:  next_world_state.player.green_hit.down  = true; break;
                                default: break;
                            }
                        }
                        if (laser_color.blue)  next_world_state.player.hit_by_blue  = true;
                        goto laser_instance_stop;
                    }
                    break;
                }
                case PACK:
                {
                    Entity* pack = &next_world_state.pack;
                    if (!laserPassthroughAllowed(pack)) goto laser_instance_stop;
                    break;
                }
                case CRYSTAL:
                {
                    Entity* crystal = getEntityPointer(current_coords);
                    if (!laserPassthroughAllowed(crystal))
                    {
                        if (!isParallelToXZ(current_direction)) goto laser_instance_stop; // let crystal break beam if not coming at angle flat on the y axis
                        if (laser_color.red) current_direction = getRedDirectionAtCrystal(current_direction); 
                        else if (laser_color.green) current_direction = current_direction;
                        else if (laser_color.blue) current_direction = getBlueDirectionAtCrystal(current_direction); 
                    }
                    break;
                }
                case MIRROR:
                {
                    Entity* mirror = getEntityPointer(current_coords);
                    if (!laserPassthroughAllowed(mirror))
                    {
                        bool can_reflect = canMirrorReflect(current_direction, getEntityDirection(current_coords));
                        if (can_reflect) current_direction = getNextLaserDirectionMirror(current_direction, getEntityDirection(current_coords));
                        else goto laser_instance_stop;
                    }
					break;
                }
                case PERM_MIRROR:
                {
                    bool can_reflect = canMirrorReflect(current_direction, getTileDirection(current_coords));
                    if (can_reflect) current_direction = getNextLaserDirectionMirror(current_direction, getTileDirection(current_coords));
                    else goto laser_instance_stop;
                    break;
                }
                case BOX:
                {
                    Entity* box = getEntityPointer(current_coords);
                    if (!laserPassthroughAllowed(box)) goto laser_instance_stop;
                    break;
                }
                case NONE:
                {
                    break;
                }
                default:
                {
                    goto laser_instance_stop;
                }
            }

            if      (laser_color.red)   laser_buffer[laser_tile_count].color.red   = true; 
            else if (laser_color.green) laser_buffer[laser_tile_count].color.green = true; // else here ensures magenta -> red, yellow -> red, cyan -> green for non-primaries;
            else if (laser_color.blue)  laser_buffer[laser_tile_count].color.blue  = true; // the rest are aleady in sources_as_primary, and will be added to laser_buffer at the end of the loop
            laser_buffer[laser_tile_count].direction = current_direction;
            laser_buffer[laser_tile_count].coords    = current_coords;
            laser_tile_count++;

            current_coords = getNextCoords(current_coords, current_direction);
        }
laser_instance_stop: ;
    }

    return laser_tile_count;
}

// UNDO / RESTART

void recordStateForUndo()
{
    undo_buffer[undo_buffer_position] = world_state;
    undo_buffer_position = (undo_buffer_position + 1) % UNDO_BUFFER_SIZE;
}

void resetVisuals(Entity* entity)
{
    entity->position_norm = intCoordsToNorm(entity->coords);
    //entity->rotation_quat = directionToQuaternion(entity->direction, true); // don't seem to need right now, and causes bug with undoing from a turn -> pack direction is flipped (if pack direction is not north after the undo)
}

void resetStandardVisuals()
{
    Entity* entity_group[3] = {next_world_state.boxes, next_world_state.mirrors, next_world_state.crystals};
    FOR(entity_group_index, 3)
    {
        FOR(entity_instance_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* entity = &entity_group[entity_group_index][entity_instance_index];
            if (entity->id == -1) continue;
            resetVisuals(entity);
        }
    }
    resetVisuals(&next_world_state.player);
    resetVisuals(&next_world_state.pack);
}

// FALLING LOGIC

// returns true iff object is able to fall as usual, but object collides with something instead.
bool doFallingEntity(Entity* entity, bool do_animation)
{
    if (entity->id == -1) return false;
	Int3 next_coords = getNextCoords(entity->coords, DOWN);
    if (getTileType(next_coords) != NONE) return true;
    FOR(trailing_hitbox_index, MAX_TRAILING_HITBOX_COUNT) if (int3IsEqual(next_coords, trailing_hitboxes[trailing_hitbox_index].coords) && trailing_hitboxes[trailing_hitbox_index].frames > 0) return true;

    int32 stack_size = getPushableStackSize(entity->coords);
    Int3 current_start_coords = entity->coords;
    Int3 current_end_coords = next_coords; 
    FOR(stack_fall_index, stack_size)
    {
        Entity* entity_in_stack = getEntityPointer(current_start_coords);
        if (entity_in_stack->id == -1) return false; // should never happen, shouldn't have id == -1 in the middle of a stack somewhere
        if (entity_in_stack->previously_moving_sideways > 1) return false; 

        if (entity_in_stack->falling_time == 0)
        {
            if (do_animation) 
            {
                createFirstFallAnimation(intCoordsToNorm(current_start_coords), &entity_in_stack->position_norm, entity_in_stack->id);
                createTrailingHitbox(current_start_coords, TRAILING_HITBOX_TIME + 5); // TODO(spike): temporary solution: lying about how long in motion here, 
                                                                                      // but is only used for laser buffer calculation, which has a set rate for all entities for which it can pass through.
            }
            entity_in_stack->falling_time = FALL_ANIMATION_TIME + 4 + 1; // 12 frames here instead of 8 because of acceleration period
        }
        else
        {
            if (entity_in_stack->falling_time != 1) return false;
            if (do_animation)
            {
            	createInterpolationAnimation(intCoordsToNorm(current_start_coords),
                                             intCoordsToNorm(current_end_coords),
                                             &entity_in_stack->position_norm,
                                             IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                             entity_in_stack->id, FALL_ANIMATION_TIME, true);
                createTrailingHitbox(current_start_coords, TRAILING_HITBOX_TIME);
            }
            entity_in_stack->falling_time = FALL_ANIMATION_TIME + 1;
        }

        setTileType(getTileType(current_start_coords), current_end_coords); 
        setTileType(NONE, current_start_coords);
        entity_in_stack->coords = current_end_coords;
        current_end_coords = current_start_coords;
        current_start_coords = getNextCoords(current_start_coords, UP);

	}
    return false;
}

void doFallingObjects(bool do_animation)
{
 	Entity* object_group_to_fall[3] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.crystals };
	FOR(to_fall_index, 3)
    {
		Entity* entity_group = object_group_to_fall[to_fall_index];
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            doFallingEntity(&entity_group[entity_index], do_animation);
        }
    }
}

void decrementFallingTimers(Entity* entity)
{
    if (entity->previously_moving_sideways > 0) entity->previously_moving_sideways--;
	if (entity->falling_time > 0) entity->falling_time--;
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
        Direction next_direction = NORTH_WEST;
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

        if (!up_or_down) createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0, 
                                     				  directionToQuaternion(current_direction, true), 
                                     				  directionToQuaternion(next_direction, true), 
                                     				  &entity->rotation_quat,
                                     				  id, TURN_ANIMATION_TIME, false); 
        else 
        {
			Vec4 start = entity->rotation_quat;
            float sign = clockwise ? 1.0f : -1.0f;
            Vec4 delta = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), sign * 0.25f * TAU);
            Vec4 end = quaternionNormalize(quaternionMultiply(delta, start));
            createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0,
                    					 start, end, &entity->rotation_quat,
                                         id, TURN_ANIMATION_TIME, false);
        }
        
		setTileDirection(next_direction, current_tile_coords);
		entity->direction = next_direction;
		current_tile_coords = getNextCoords(current_tile_coords, UP);
    }
}

void doHeadMovement(Direction direction, bool animations_on)
{
    Int3 coords_above_player = getNextCoords(next_world_state.player.coords, UP);
    if (!isPushable(getTileType(coords_above_player))) return;
    PushResult push_result = canPushStack(coords_above_player, direction);
    if (push_result == CAN_PUSH) pushAll(coords_above_player, direction, animations_on, false);
}

void doStandardMovement(Direction input_direction, TileType next_tile, Int3 next_player_coords)
{
    doHeadMovement(input_direction, true);

    int32 animation_time = 0;
    if (next_tile == MIRROR) animation_time = ROLL_ANIMATION_TIME;
    else 					 animation_time = PUSH_ANIMATION_TIME;

    next_world_state.player.previously_moving_sideways = PUSH_ANIMATION_TIME + 1;

    createInterpolationAnimation(intCoordsToNorm(next_world_state.player.coords), 
                                 intCoordsToNorm(next_player_coords), 
                                 &next_world_state.player.position_norm,
                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                 PLAYER_ID, animation_time, false); 
    // move pack also
    if (next_world_state.pack.pack_detached) 
    {
        setTileType(NONE, next_world_state.player.coords);
    }
    else 
    {
        next_world_state.pack.previously_moving_sideways = PUSH_ANIMATION_TIME + 1;

        setTileType(NONE, next_world_state.pack.coords);
        setTileType(PACK, next_world_state.player.coords);
        createInterpolationAnimation(intCoordsToNorm(next_world_state.pack.coords),
                                     intCoordsToNorm(next_world_state.player.coords),
                                     &next_world_state.pack.position_norm,
                                     IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                     PACK_ID, animation_time, false);
        createTrailingHitbox(next_world_state.pack.coords, TRAILING_HITBOX_TIME);
        next_world_state.pack.coords = next_world_state.player.coords;
    }
	createTrailingHitbox(next_world_state.player.coords, TRAILING_HITBOX_TIME);
    next_world_state.player.coords = next_player_coords;
    setTileType(PLAYER, next_world_state.player.coords);	

    recordStateForUndo();
}

// GAME

void gameInitialiseState()
{
    loadFileToBuffer(level_path);

    memset(next_world_state.boxes,    	  0, sizeof(next_world_state.boxes)); 
    memset(next_world_state.mirrors,  	  0, sizeof(next_world_state.mirrors));
    memset(next_world_state.crystals, 	  0, sizeof(next_world_state.crystals));
    memset(next_world_state.sources,  	  0, sizeof(next_world_state.sources));
    memset(next_world_state.perm_mirrors, 0, sizeof(next_world_state.perm_mirrors));
	FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        next_world_state.boxes[entity_index].id 	   = -1;
        next_world_state.mirrors[entity_index].id 	   = -1;
        next_world_state.crystals[entity_index].id 	   = -1;
        next_world_state.sources[entity_index].id 	   = -1;
        next_world_state.perm_mirrors[entity_index].id = -1;
    }
    Entity *entity_group = 0;
    for (int buffer_index = 0; buffer_index < 2 * level_dim.x*level_dim.y*level_dim.z; buffer_index += 2)
    {
        TileType buffer_contents = next_world_state.buffer[buffer_index];
        if (buffer_contents == BOX)     	entity_group = next_world_state.boxes;
        if (buffer_contents == MIRROR)  	entity_group = next_world_state.mirrors;
        if (buffer_contents == CRYSTAL) 	entity_group = next_world_state.crystals;
        if (buffer_contents == PERM_MIRROR) entity_group = next_world_state.perm_mirrors;
        if (isSource(buffer_contents))  	entity_group = next_world_state.sources;
        if (entity_group != 0)
        {
            int32 count = getEntityCount(entity_group);
			entity_group[count].coords = bufferIndexToCoords(buffer_index);
            entity_group[count].position_norm = intCoordsToNorm(entity_group[count].coords);
            entity_group[count].direction = next_world_state.buffer[buffer_index + 1]; 
            entity_group[count].rotation_quat = directionToQuaternion(entity_group[count].direction, true);
            entity_group[count].color = getEntityColor(entity_group[count].coords);
            entity_group[count].id = getEntityCount(entity_group) + entityIdOffset(entity_group);
            entity_group= 0;
        }
        else if (next_world_state.buffer[buffer_index] == PLAYER) // special case for player, since there is only one
        {
            next_world_state.player.coords = bufferIndexToCoords(buffer_index);
            next_world_state.player.position_norm = intCoordsToNorm(next_world_state.player.coords);
            next_world_state.player.direction = next_world_state.buffer[buffer_index + 1];
            next_world_state.player.rotation_quat = directionToQuaternion(next_world_state.player.direction, false);
            next_world_state.player.id = PLAYER_ID;
        }
        else if (next_world_state.buffer[buffer_index] == PACK) // likewise special case for pack
        {
            next_world_state.pack.coords = bufferIndexToCoords(buffer_index);
            next_world_state.pack.position_norm = intCoordsToNorm(next_world_state.pack.coords);
            next_world_state.pack.direction = next_world_state.buffer[buffer_index + 1];
            next_world_state.pack.rotation_quat = directionToQuaternion(next_world_state.pack.direction, false);
            next_world_state.pack.id = PACK_ID;
        }
    }

	camera.coords = (Vec3){15, 12, 19};
    camera_yaw = 0; // towards -z; north
    camera_pitch = -TAU * 0.18f; // look down-ish
    Vec4 quaternion_yaw   = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), camera_yaw);
    Vec4 quaternion_pitch = quaternionFromAxisAngle(intCoordsToNorm(AXIS_X), camera_pitch);
    camera.rotation  = quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));
    world_state = next_world_state;
}

void gameInitialise(char* command_line) 
{	
    system_command_line = command_line;
    if (command_line != 0) 
    {
        strcat(level_path_buffer, command_line);
        strcat(level_path_buffer, ".txt");
        level_path = level_path_buffer;
    }

    gameInitialiseState();
}

void gameFrame(double delta_time, TickInput tick_input)
{	
	if (delta_time > 0.1) delta_time = 0.1;
	accumulator += delta_time;

    // modifies globals directly
    if (editor_state.editor_mode)
    {
        camera_yaw += tick_input.mouse_dx * SENSITIVITY;
        if (camera_yaw >  0.5f * TAU) camera_yaw -= TAU; 
        if (camera_yaw < -0.5f * TAU) camera_yaw += TAU; 
        camera_pitch += tick_input.mouse_dy * SENSITIVITY;
        float pitch_limit = 0.25f * TAU;
        if (camera_pitch >  pitch_limit) camera_pitch =  pitch_limit; 
        if (camera_pitch < -pitch_limit) camera_pitch = -pitch_limit; 

        Vec4 quaternion_yaw   = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), camera_yaw);
        Vec4 quaternion_pitch = quaternionFromAxisAngle(intCoordsToNorm(AXIS_X), camera_pitch);
        camera.rotation  = quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));
    }

    while (accumulator >= PHYSICS_INCREMENT)
    {
		next_world_state = world_state;

        if (!editor_state.editor_mode)
        {
			if (time_until_input == 0 && tick_input.e_press) 
            {
                editor_state.editor_mode = true;
                time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
            }
            if (time_until_input == 0 && tick_input.z_press)
            {
                // undo
                int32 next_undo_buffer_position = 0;
                if (undo_buffer_position != 0) next_undo_buffer_position = undo_buffer_position - 1;
                else next_undo_buffer_position = UNDO_BUFFER_SIZE - 1;

                if (undo_buffer[next_undo_buffer_position].player.id != 0) // check that there is anything in the buffer (using something that should never usually happen)
                {
                    next_world_state = undo_buffer[next_undo_buffer_position];
                   	memset(&undo_buffer[undo_buffer_position], 0, sizeof(WorldState));
                    undo_buffer_position = next_undo_buffer_position;
                    memset(animations, 0, sizeof(animations));
                    resetStandardVisuals(); // set position_norm and rotation_quat to coords and direction respectively
                }
                time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
            }
            if (time_until_input == 0 && tick_input.r_press)
            {
                // restart
                recordStateForUndo();
                memset(animations, 0, sizeof(animations));
                Camera temp_camera = camera;
                gameInitialiseState();
                camera = temp_camera;
                time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
            }
            if (time_until_input == 0 && (tick_input.w_press || tick_input.a_press || tick_input.s_press || tick_input.d_press) && next_world_state.player.falling_time == 0)
            {
				// player made input this frame (may be unsuccessful)
                Direction input_direction = 0;
                Int3 next_player_coords = {0};
                if 		(tick_input.w_press) input_direction = NORTH; 
                else if (tick_input.a_press) input_direction = WEST; 
                else if (tick_input.s_press) input_direction = SOUTH; 
                else if (tick_input.d_press) input_direction = EAST; 

                if (input_direction == next_world_state.player.direction)
                {
                    // TODO(spike): move these outside
                    Entity* player = &next_world_state.player;
                    Entity* pack = &next_world_state.pack;

                    if (do_player_ghost)
                    {
                        // seek towards start of laser to get endpoint, and then go to the endpoint
                        // check if endpoint is valid before teleport (i.e, if pack can go there - if over air, teleport anyway, probably?)

						bool obstructed_tp_location = true;
                        if (do_pack_ghost) obstructed_tp_location = false;
                        else if (do_player_ghost && pack->pack_detached) obstructed_tp_location = false;

                        if (!obstructed_tp_location)
                        {
                            setTileType(NONE, player->coords);
                            setTileDirection(NORTH, player->coords);
                            zeroAnimations(PLAYER_ID);
                            player->coords = player_ghost_coords;
                            player->position_norm = intCoordsToNorm(player_ghost_coords);
                            player->direction = player_ghost_direction;
                            player->rotation_quat = directionToQuaternion(player_ghost_direction, true);
							setTileType(PLAYER, player_ghost_coords);
                            setTileDirection(player_ghost_direction, player_ghost_coords);
                            if (!pack->pack_detached)
                            {
                                Int3 pack_coords = getNextCoords(player_ghost_coords, oppositeDirection(pack_ghost_direction));
                                setTileType(NONE, pack->coords);
                                setTileDirection(NORTH, pack->coords);
                                zeroAnimations(PACK_ID);
                                pack->coords = pack_coords; 
                                pack->position_norm = intCoordsToNorm(pack_coords);
                                pack->direction = pack_ghost_direction;
                                pack->rotation_quat = directionToQuaternion(pack_ghost_direction, true);
                                setTileType(PLAYER, pack_coords);
                                setTileDirection(pack_ghost_direction, pack_coords);
                            }
                            time_until_input = SUCCESSFUL_TP_TIME;
                        }
                        else
                        {
                            // tp obstructed
                            time_until_input = FAILED_TP_TIME;
                        }
                    }
                    else
                    {
                        // no green; normal movement attempt
                        bool do_push = false;
                        bool do_roll = false;

                        bool move_player = false;
                        bool do_failed_animations = false;
                        if 		(tick_input.w_press) next_player_coords = int3Add(next_world_state.player.coords, int3Negate(AXIS_Z));
                        else if (tick_input.a_press) next_player_coords = int3Add(next_world_state.player.coords, int3Negate(AXIS_X));
                        else if (tick_input.s_press) next_player_coords = int3Add(next_world_state.player.coords, AXIS_Z);
                        else if (tick_input.d_press) next_player_coords = int3Add(next_world_state.player.coords, AXIS_X);
                        TileType next_tile = getTileType(next_player_coords);
                        if (!isSource(next_tile)) switch (next_tile)
                        {
                            case VOID:
                            case GRID:
                            case WALL:
                            case PERM_MIRROR:
                            {
                                do_failed_animations = true;
                                break;
                            }
                            case BOX:
                            case CRYSTAL:
                            case PACK:
                            {
                                //figure out if push, pause, or fail here.
                            	PushResult push_check = canPushStack(next_player_coords, input_direction);
                                if (push_check == CAN_PUSH) 
                                {
                                    do_push = true;
                                    move_player = true;
                                }
                                else if (push_check == FAILED_PUSH) do_failed_animations = true;
                                break;
                            }
                            case MIRROR:
                            {
                                PushResult push_check = canPush(next_player_coords, input_direction);
                                if (push_check == CAN_PUSH)
                                {
                                    TileType push_tile = getTileType(getNextCoords(next_player_coords, input_direction));
                                    TileType above_tile = getTileType(getNextCoords(next_player_coords, UP));
                                    if (push_tile != NONE || above_tile != NONE) break;
                                    do_roll = true;
                                    move_player = true;
                                }
                                else do_failed_animations = true;
                                break;
                            }
                            default:
                            {
                                Int3 coords_ahead = next_player_coords;
                                Int3 coords_below_and_ahead = getNextCoords(next_player_coords, DOWN);
                                if (isPushable(getTileType(coords_ahead)) && entityInMotion(getEntityPointer(coords_ahead))) move_player = false;
                                else if (isPushable(getTileType(coords_below_and_ahead)) && entityInMotion(getEntityPointer(coords_below_and_ahead))) move_player = false;
                                else move_player = true;
                            }
                        }
						if (move_player)
                        {
                            // don't allow walking off edge
                            Int3 coords_below = getNextCoords(next_player_coords, DOWN);
                            TileType tile_below = getTileType(coords_below);
                            if (tile_below != NONE || next_world_state.player.hit_by_red)
                            {
                                if (do_push) pushAll(next_player_coords, input_direction, true, false);
                                else if (do_roll) roll(next_player_coords, input_direction);

                                doStandardMovement(input_direction, next_tile, next_player_coords);
                                time_until_input = PUSH_ANIMATION_TIME;
                            }
                            else
                            {
                                // leap of faith logic - TODO(spike): probably outdated
                                WorldState world_state_savestate = next_world_state;

                                if (do_push) pushAll(next_player_coords, input_direction, false, false);
                                if (do_roll) rollWithoutAnimation(next_player_coords, input_direction);

                                bool animations_on = false;
                                // TODO(spike): also needs testing
                                if (!next_world_state.player.hit_by_blue)
                                {
                                    doFallingObjects(animations_on);
                                    if (next_world_state.pack.pack_detached) doFallingEntity(&next_world_state.pack, animations_on);
                                }

                                doHeadMovement(input_direction, animations_on);

                                setTileType(NONE, next_world_state.player.coords);
                                next_world_state.player.coords = next_player_coords;
                                setTileType(PLAYER, next_world_state.player.coords);	

                                // TODO(spike): this code probably needs some testing
                                if (!next_world_state.pack.pack_detached)
                                {
                                    setTileType(NONE, next_world_state.pack.coords);
                                    next_world_state.pack.coords = getNextCoords(next_player_coords, oppositeDirection(input_direction));
                                    setTileType(PACK, next_world_state.pack.coords);
                                }

                                updateLaserBuffer();

                                bool leap_of_faith_worked = false;
                                if (next_world_state.player.hit_by_red) leap_of_faith_worked = true;
                                next_world_state = world_state_savestate;
                                if (leap_of_faith_worked)
                                {
                                    if (do_push) pushOnce(next_player_coords, input_direction);
                                    if (do_roll) roll(next_player_coords, input_direction);

                                    doStandardMovement(input_direction, next_tile, next_player_coords);
                                }
                                else doFailedWalkAnimations(next_player_coords);
                                time_until_input = PUSH_ANIMATION_TIME;
                            }
                            if (next_tile == MIRROR) time_until_input = ROLL_ANIMATION_TIME; 
                        }
						else if (do_failed_animations) 
                        {
                            doFailedWalkAnimations(next_player_coords);
                            time_until_input = FAILED_ANIMATION_TIME;
                        }
                    }
                }
                else
                {
                    if (input_direction != oppositeDirection(next_world_state.player.direction)) // check if turning (as opposed to trying to reverse)
                    {
                        // player is turning
                        Direction polarity_direction = NORTH;
                        int32 clockwise = false;
                        int32 clockwise_calculation = next_world_state.player.direction - input_direction;
                        if (clockwise_calculation == -1 || clockwise_calculation == 3) clockwise = true;

                        if (next_world_state.pack.pack_detached)
                        {
                            // if pack detached, always allow turn
                            if (isPushable(getTileType(getNextCoords(next_world_state.player.coords, UP)))) doHeadRotation(clockwise);

                            createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0, 
                                                         directionToQuaternion(next_world_state.player.direction, true), 
                                                         directionToQuaternion(input_direction, true), 
                                                         &next_world_state.player.rotation_quat,
                                                         1, TURN_ANIMATION_TIME, false); 
                            next_world_state.player.direction = input_direction;
                            setTileDirection(next_world_state.player.direction, next_world_state.player.coords);

                            recordStateForUndo();
                        }
                        else
                        {
							if (clockwise) polarity_direction = (input_direction + 1) % 4;
							else 		   polarity_direction = (input_direction + 3) % 4;

                            Int3 orthogonal_coords = getNextCoords(next_world_state.player.coords, oppositeDirection(input_direction));			
                            Int3 diagonal_coords = getNextCoords(orthogonal_coords, polarity_direction);
                            Direction diagonal_push_direction = oppositeDirection(input_direction);	
                            Direction orthogonal_push_direction = oppositeDirection(polarity_direction); 
                            
                            bool pause_turn = false;
                            if (entityInMotion(getEntityPointer(orthogonal_coords))) pause_turn = true;
                            if (entityInMotion(getEntityPointer(diagonal_coords))) pause_turn = true;

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
                                    case CRYSTAL:
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
                                    case CRYSTAL:
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

                                if (allow_turn_orthogonal)
                                {
                                    // actually turning: rotate player
									createTrailingHitbox(next_world_state.pack.coords, TRAILING_HITBOX_PACK_ORTHOGONAL_TURN_TIME);
                                    diagonal_turn_hitbox_timer = TIME_BEFORE_DIAGONAL_TRAILING_HITBOX;
                                    diagonal_turn_hitbox_coords = diagonal_coords;
            						next_world_state.pack.previously_moving_sideways = TURN_ANIMATION_TIME + 3;

                                    if (isPushable(getTileType(getNextCoords(next_world_state.player.coords, UP)))) doHeadRotation(clockwise);

                                    createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0, 
                                                                 directionToQuaternion(next_world_state.player.direction, true), 
                                                                 directionToQuaternion(input_direction, true), 
                                                                 &next_world_state.player.rotation_quat,
                                                                 1, TURN_ANIMATION_TIME, false); 
                                    next_world_state.player.direction = input_direction;
                                    setTileDirection(next_world_state.player.direction, next_world_state.player.coords);

                                    if (push_diagonal)   pushAll(diagonal_coords,   diagonal_push_direction, true, true);
                                    if (push_orthogonal) pushAll(orthogonal_coords, orthogonal_push_direction, true, true);

                                    createPackRotationAnimation(intCoordsToNorm(next_world_state.player.coords), 
                                                                intCoordsToNorm(next_world_state.pack.coords), 
                                                                oppositeDirection(input_direction), clockwise, 
                                                                &next_world_state.pack.position_norm, &next_world_state.pack.rotation_quat, 2);
                                    setTileType(NONE, next_world_state.pack.coords);
                                    setTileDirection(NORTH, next_world_state.pack.coords);
                                    next_world_state.pack.coords = orthogonal_coords;
                                    setTileType(PACK, next_world_state.pack.coords);
                                    next_world_state.pack.direction = oppositeDirection(input_direction);
                                    setTileDirection(input_direction, next_world_state.pack.coords);

                                    recordStateForUndo();
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
            }
        }
        else
        {
            Vec3 right_camera_basis, forward_camera_basis;
            cameraBasisFromYaw(camera_yaw, &right_camera_basis, &forward_camera_basis);

			// WASD: movement
            // E: toggle editor mode
            // J: set void at current coords
            // L: increment tile at cursor
            // LMB: destroy tile at cursor
            // RMB: place tile at cursor
            // MMB: pick tile at cursor

            if (tick_input.w_press) 
            {
                camera.coords.x += forward_camera_basis.x * MOVE_STEP;
                camera.coords.z += forward_camera_basis.z * MOVE_STEP;
            }
            if (tick_input.a_press) 
            {
                camera.coords.x -= right_camera_basis.x * MOVE_STEP;
                camera.coords.z -= right_camera_basis.z * MOVE_STEP;
            }
            if (tick_input.s_press) 
            {
                camera.coords.x -= forward_camera_basis.x * MOVE_STEP;
                camera.coords.z -= forward_camera_basis.z * MOVE_STEP;
            }
            if (tick_input.d_press) 
            {
                camera.coords.x += right_camera_basis.x * MOVE_STEP;
                camera.coords.z += right_camera_basis.z * MOVE_STEP;
            }
            if (tick_input.space_press) camera.coords.y += MOVE_STEP;
            if (tick_input.shift_press) camera.coords.y -= MOVE_STEP;

			if (time_until_input == 0 && tick_input.e_press) 
            {
                editor_state.editor_mode = false;
                time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
            }

			if (time_until_input == 0 && tick_input.j_press)
            {
                if (normCoordsWithinLevelBounds(camera.coords))
                {
					setTileType(VOID, normCoordsToInt(camera.coords));
                    time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
                }
            }

            // inputs that require raycast
			else if (time_until_input == 0 && (tick_input.left_mouse_press || tick_input.right_mouse_press || tick_input.middle_mouse_press || tick_input.r_press))
            {
                Vec3 neg_z_basis = {0, 0, -1};
            	RaycastHit raycast_output = raycastHitCube(camera.coords, vec3RotateByQuaternion(neg_z_basis, camera.rotation), RAYCAST_SEEK_LENGTH);

                if (tick_input.left_mouse_press && raycast_output.hit) 
                {
                    Entity *entity= getEntityPointer(raycast_output.hit_coords);
                    if (entity!= 0)
                    {
                        entity->coords = (Int3){0};
                        entity->position_norm = (Vec3){0};
                        entity->id = -1;
                    }
                    setTileType(NONE, raycast_output.hit_coords);
                    setTileDirection(NORTH, raycast_output.hit_coords);
                }
                else if (tick_input.right_mouse_press && raycast_output.hit) 
                {
                    Entity *entity_group = 0;
                    if (isSource(editor_state.picked_tile)) 
                    {
                        setTileType(editor_state.picked_tile, raycast_output.place_coords); 
                        setEntityInstanceInGroup(next_world_state.sources, raycast_output.place_coords, NORTH, getEntityColor(raycast_output.place_coords)); 
                    }
                    else if (editor_state.picked_tile == PLAYER)
                    {
                        editorPlaceOnlyInstanceOfTile(&next_world_state.player, raycast_output.place_coords, PLAYER, PLAYER_ID);
                    }
                    else if (editor_state.picked_tile == PACK)
                    {
                        editorPlaceOnlyInstanceOfTile(&next_world_state.pack, raycast_output.place_coords, PACK, PACK_ID);
                    }
                    else
                    {
                        switch (editor_state.picked_tile)
                        {
                            case BOX:     	  entity_group = next_world_state.boxes;    	break;
                            case MIRROR:  	  entity_group = next_world_state.mirrors;  	break;
                            case CRYSTAL: 	  entity_group = next_world_state.crystals; 	break;
                            case PERM_MIRROR: entity_group = next_world_state.perm_mirrors; break;
                            default: entity_group = 0;
                        }
                        if (entity_group != 0) setEntityInstanceInGroup(entity_group, raycast_output.place_coords, NORTH, NO_COLOR);
                        setTileType(editor_state.picked_tile, raycast_output.place_coords); 
                    }
                }
                else if (tick_input.r_press && raycast_output.hit)
                {   
                    Direction direction = getTileDirection(raycast_output.hit_coords);
                    if (direction == DOWN) direction = NORTH;
                    else direction++;
                    setTileDirection(direction, raycast_output.hit_coords);
                    Entity *entity = getEntityPointer(raycast_output.hit_coords);
                    if (entity != 0)
                    {
                        entity->direction = direction;
						if (getTileType(entity->coords) == MIRROR || getTileType(entity->coords) == PERM_MIRROR) entity->rotation_quat = directionToQuaternion(direction, true); // unclear why this is required, something to do with my sprite layout
						else entity->rotation_quat = directionToQuaternion(direction, false);
                	}
                }
                else if (tick_input.middle_mouse_press && raycast_output.hit) editor_state.picked_tile = getTileType(raycast_output.hit_coords);

                time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
            }
			else if (time_until_input == 0 && tick_input.l_press)
            {
				editor_state.picked_tile++;
                if 		(editor_state.picked_tile == PERM_MIRROR + 1) editor_state.picked_tile = SOURCE_RED;
                else if (editor_state.picked_tile == SOURCE_WHITE + 1) editor_state.picked_tile = VOID;
                time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
            }
        }

        updateLaserBuffer();

        // falling logic
		if (!next_world_state.player.hit_by_blue) doFallingObjects(true);

        if (!next_world_state.player.hit_by_red)
        {
            if (!next_world_state.pack.pack_detached)
            {
                // not red and pack attached: player always falls. pack only falls if player falls
                if (!doFallingEntity(&next_world_state.player, true))
                {
                    if (doFallingEntity(&next_world_state.pack, true))
                    {
                        // pack wants to fall but cannot: we already know player can fall, so pack will become unattached
                        next_world_state.pack.pack_detached = true;
                    }
                }
            }
            else
            {
                // not red and pack not attached, so pack and player both always fall
                doFallingEntity(&next_world_state.player, true);
				doFallingEntity(&next_world_state.pack, true);
            }
        }
        else
        {
            // red, so pack only falls if is detached from player
            if (next_world_state.pack.pack_detached)
            {
                doFallingEntity(&next_world_state.pack, true);
            }
        }

        // reattach pack
        if (next_world_state.pack.pack_detached)
        {
            if (getTileType(getNextCoords(next_world_state.player.coords, oppositeDirection(next_world_state.player.direction))) == PACK)
            {
                next_world_state.pack.pack_detached = false;
            }
        }

		// render and calculate ghosts
        Entity* player = &next_world_state.player;
        Entity* pack = &next_world_state.pack;
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
        if (facing_green)
        {
            // seek towards start of laser to get endpoint, and then go to the endpoint
            // check if endpoint is valid before teleport (i.e, if pack can go there - if over air, teleport anyway, probably?)
            bool obstructed_tp_location = false;

            Int3 current_coords = player->coords; 
            Direction current_direction = player->direction;
            FOR(seek_index, MAX_LASER_TRAVEL_DISTANCE)
            {
                current_coords = getNextCoords(current_coords, current_direction);
                if (!intCoordsWithinLevelBounds(current_coords)) obstructed_tp_location = true;
                if (getTileType(current_coords) == MIRROR)
                {
                    current_direction = getNextLaserDirectionMirror(current_direction, getTileDirection(current_coords));
                    current_coords = getNextCoords(current_coords, current_direction);
                    continue;
                }
                if (getTileType(current_coords) != NONE) break;
            }
            Int3 tp_coords = getNextCoords(current_coords, oppositeDirection(current_direction));
            player_ghost_coords = tp_coords;
            player_ghost_direction = current_direction;

            if (!pack->pack_detached) 
            {
                TileType to_be_pack_tile = getTileType(getNextCoords(tp_coords, oppositeDirection(current_direction)));
                if (to_be_pack_tile != NONE && to_be_pack_tile != PLAYER) obstructed_tp_location = true;
                pack_ghost_coords = getNextCoords(tp_coords, oppositeDirection(current_direction));
                pack_ghost_direction = current_direction;
            }

            if (!obstructed_tp_location)
            {
                do_player_ghost = true;
                do_pack_ghost = true;
            }
        }
        else
        {
            do_player_ghost = false;
            do_pack_ghost = false;
        }

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

        // decrement falling timers
        Entity* falling_entity_groups[3] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.crystals }; // TODO(spike): i do this array enough that i should probably just make it global
        FOR(falling_object_index, 3)
        {
            Entity* entity_group = falling_entity_groups[falling_object_index];
            FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT) decrementFallingTimers(&entity_group[entity_index]);
        }
        decrementFallingTimers(player);
		decrementFallingTimers(pack);

		// handle timer which adds trailing hitbox in the middle of turning pack hitbox once needed
        if (diagonal_turn_hitbox_timer > 0)
        {
            if (diagonal_turn_hitbox_timer == 1)
            {
                createTrailingHitbox(diagonal_turn_hitbox_coords, TRAILING_HITBOX_PACK_DIAGONAL_TURN_TIME);
            }
			diagonal_turn_hitbox_timer--;
        }

        // final redo of laser buffer, after all logic is complete, for drawing
		int32 laser_tile_count = updateLaserBuffer();

        // decrement trailing hitboxes
        FOR(i, MAX_TRAILING_HITBOX_COUNT) if (trailing_hitboxes[i].frames > 0) trailing_hitboxes[i].frames--;

        // finished updating state
        world_state = next_world_state;

		// DRAW 

        // draw lasers
        for (int laser_index = 0; laser_index < laser_tile_count; laser_index++)
        {
			// check if inside mirror / crystal, and if so skip
			bool do_draw = true;
            Int3 laser_coords = laser_buffer[laser_index].coords;
            TileType laser_in_tile = getTileType(laser_coords);
            if (laser_in_tile == CRYSTAL && !laserPassthroughAllowed(getEntityPointer(laser_coords))) do_draw = false;
            if (laser_in_tile == MIRROR  && !laserPassthroughAllowed(getEntityPointer(laser_coords))) do_draw = false;
            if (laser_in_tile == PERM_MIRROR) do_draw = false;
            if (!do_draw) continue;

            bool comparison_found = false;
            for (int laser_comparison_index = laser_index + 1; laser_comparison_index < laser_tile_count; laser_comparison_index++) // only compares forward
            {
                if (int3IsEqual(laser_buffer[laser_index].coords, laser_buffer[laser_comparison_index].coords) && 
                    (laser_buffer[laser_index].direction == laser_buffer[laser_comparison_index].direction || laser_buffer[laser_index].direction == oppositeDirection(laser_buffer[laser_comparison_index].direction)))
                {
					laser_buffer[laser_comparison_index].color.red   = laser_buffer[laser_index].color.red   | laser_buffer[laser_comparison_index].color.red;
					laser_buffer[laser_comparison_index].color.green = laser_buffer[laser_index].color.green | laser_buffer[laser_comparison_index].color.green;
					laser_buffer[laser_comparison_index].color.blue  = laser_buffer[laser_index].color.blue  | laser_buffer[laser_comparison_index].color.blue;
                    comparison_found = true;
                    break;
                }
            }
            if (comparison_found) 
            {
                comparison_found = false;
                continue;
            }
            LaserColor color = laser_buffer[laser_index].color;
            Vec3 laser_scale = {0};
   			bool is_diagonal = isDiagonal(laser_buffer[laser_index].direction);
            if (is_diagonal) laser_scale = DIAGONAL_LASER_SCALE;
            else 			 laser_scale = ORTHOGONAL_LASER_SCALE;

            if      (color.red && color.green && color.blue) drawAsset(laser_white_path,   CUBE_3D, intCoordsToNorm(laser_buffer[laser_index].coords), laser_scale, directionToQuaternion(laser_buffer[laser_index].direction, false));
            else if (color.red && color.green              ) drawAsset(laser_yellow_path,  CUBE_3D, intCoordsToNorm(laser_buffer[laser_index].coords), laser_scale, directionToQuaternion(laser_buffer[laser_index].direction, false));
            else if (color.red &&                color.blue) drawAsset(laser_magenta_path, CUBE_3D, intCoordsToNorm(laser_buffer[laser_index].coords), laser_scale, directionToQuaternion(laser_buffer[laser_index].direction, false));
            else if (             color.green && color.blue) drawAsset(laser_cyan_path,    CUBE_3D, intCoordsToNorm(laser_buffer[laser_index].coords), laser_scale, directionToQuaternion(laser_buffer[laser_index].direction, false));
            else if (color.red                             ) drawAsset(laser_red_path,     CUBE_3D, intCoordsToNorm(laser_buffer[laser_index].coords), laser_scale, directionToQuaternion(laser_buffer[laser_index].direction, false));
            else if (             color.green              ) drawAsset(laser_green_path,   CUBE_3D, intCoordsToNorm(laser_buffer[laser_index].coords), laser_scale, directionToQuaternion(laser_buffer[laser_index].direction, false));
            else if (                            color.blue) drawAsset(laser_blue_path,    CUBE_3D, intCoordsToNorm(laser_buffer[laser_index].coords), laser_scale, directionToQuaternion(laser_buffer[laser_index].direction, false));
        }
        // clear laser buffer 
        memset(laser_buffer, 0, sizeof(laser_buffer));

        // draw static objects
        for (int tile_index = 0; tile_index < 2 * level_dim.x*level_dim.y*level_dim.z; tile_index += 2)
        {
			TileType tile = world_state.buffer[tile_index];
			if (tile == PLAYER || isSource(tile) || isPushable(tile) || tile == PERM_MIRROR) continue;
			if (tile != NONE)   drawAsset(getPath(tile), CUBE_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToQuaternion(next_world_state.buffer[tile_index + 1], false));
        }

        // draw non-colored entities
        drawEntityLoop(world_state.boxes,    	 box_path,         CUBE_3D, DEFAULT_SCALE);
        drawEntityLoop(world_state.mirrors,  	 mirror_path,      CUBE_3D, DEFAULT_SCALE);
        drawEntityLoop(world_state.crystals, 	 crystal_path,     CUBE_3D, DEFAULT_SCALE);
        drawEntityLoop(world_state.perm_mirrors, perm_mirror_path, CUBE_3D, DEFAULT_SCALE);

        if (world_state.player.id != -1)
        {
            player = &world_state.player;

            // TODO(spike): this is terrible (fix with shaders)
    		bool hit_by_green = false;
            if (player->green_hit.north || player->green_hit.west || player->green_hit.south || player->green_hit.east || player->green_hit.up || player->green_hit.down) hit_by_green = true;
            if      (player->hit_by_red && hit_by_green && player->hit_by_blue) drawAsset(white_player_path,   CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (player->hit_by_red && hit_by_green             		  ) drawAsset(yellow_player_path,  CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (player->hit_by_red &&      	       player->hit_by_blue) drawAsset(magenta_player_path, CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (             		   hit_by_green && player->hit_by_blue) drawAsset(cyan_player_path,    CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (player->hit_by_red                 	  				  ) drawAsset(red_player_path,     CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (             		   hit_by_green             		  ) drawAsset(green_player_path,   CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else if (                            		   player->hit_by_blue) drawAsset(blue_player_path,    CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);
            else drawAsset(player_path, CUBE_3D, player->position_norm, PLAYER_SCALE, player->rotation_quat);

            if (do_player_ghost) drawAsset(player_ghost_path, CUBE_3D, intCoordsToNorm(player_ghost_coords), PLAYER_SCALE, directionToQuaternion(player_ghost_direction, true));
            if (do_pack_ghost)   drawAsset(pack_ghost_path,   CUBE_3D, intCoordsToNorm(pack_ghost_coords),   PLAYER_SCALE, directionToQuaternion(pack_ghost_direction, true));
        }
		if (world_state.pack.id != -1) drawAsset(pack_path, CUBE_3D, world_state.pack.position_norm, PLAYER_SCALE, world_state.pack.rotation_quat);

		// draw sources 
		for (int source_index = 0; source_index < MAX_ENTITY_INSTANCE_COUNT; source_index++)
        {
            if (world_state.sources[source_index].id == -1) continue;
            char* path = getPath(getTileType(world_state.sources[source_index].coords));
            drawAsset(path, CUBE_3D, world_state.sources[source_index].position_norm, DEFAULT_SCALE, world_state.sources[source_index].rotation_quat);
        }

        // write to file
        if (editor_state.editor_mode && tick_input.i_press) writeBufferToFile(level_path);

		if (time_until_input > 0) time_until_input--;

        rendererSubmitFrame(assets_to_load, camera);
        memset(assets_to_load, 0, sizeof(assets_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
