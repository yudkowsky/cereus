#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> // TODO(spike): also temporary, for sin/cos
#include <stdio.h> // TODO(spike): "temporary", for fopen 

#define FOR(i, n) for (int i = 0; i < n; i++)

const int32 SCREEN_WIDTH_PX = 1920; // TODO(spike): get from platform layer
const int32	SCREEN_HEIGHT_PX = 1080;

const float TAU = 6.2831853071f;

const float CAMERA_SENSITIVITY = 0.005f;
const float CAMERA_MOVE_STEP = 0.075f;
const float CAMERA_FOV = 25.0f;
const Vec3 DEFAULT_SCALE = { 1.0f,  1.0f,  1.0f  };
const Vec3 PLAYER_SCALE  = { 0.75f, 0.75f, 0.75f };
const float LASER_WIDTH = 0.125f;
const float MAX_RAYCAST_SEEK_LENGTH = 50.0f;

const int32 EDITOR_INPUT_TIME_UNTIL_ALLOW = 9;
const int32 MOVE_OR_PUSH_ANIMATION_TIME = 9; // TODO(spike): make this freely editable (want to up this by a few frames to emphasise pushing stacked box mechanics)
const int32 TURN_ANIMATION_TIME = 9; // somewhat hard coded, tied to PUSH_FROM_TURN...
const int32 FALL_ANIMATION_TIME = 8; // hard coded (because acceleration in first fall anim must be constant)
const int32 PUSH_FROM_TURN_ANIMATION_TIME = 6;
const int32 FAILED_ANIMATION_TIME = 8;
const int32 STANDARD_IN_MOTION_TIME = 7;
const int32 STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH = 5;

const int32 TRAILING_HITBOX_TIME = 5;
const int32 FIRST_TRAILING_PACK_TURN_HITBOX_TIME = 2;
const int32 TIME_BEFORE_ORTHOGONAL_PUSH_STARTS_IN_TURN = 2;
const int32 PACK_TIME_IN_INTERMEDIATE_STATE = 4;

const int32 SUCCESSFUL_TP_TIME = 8;
const int32 FAILED_TP_TIME = 8;

const int32 MAX_ENTITY_INSTANCE_COUNT = 128;
const int32 MAX_ENTITY_PUSH_COUNT = 32;
const int32 MAX_ANIMATION_COUNT = 32;
const int32 MAX_LASER_TRAVEL_DISTANCE = 48;
const int32 MAX_LASER_TURNS_ALLOWED = 16;
const int32 MAX_PSEUDO_SOURCE_COUNT = 32;
const int32 MAX_PUSHABLE_STACK_SIZE = 32;
const int32 MAX_TRAILING_HITBOX_COUNT = 64;
const int32 MAX_LEVEL_COUNT = 64;

const int32 UNDO_BUFFER_SIZE = 256; // remember to modify undo_buffer

const Int3 AXIS_X = { 1, 0, 0 };
const Int3 AXIS_Y = { 0, 1, 0 };
const Int3 AXIS_Z = { 0, 0, 1 };
const Vec3 IDENTITY_TRANSLATION = { 0, 0, 0 };
const Vec4 IDENTITY_QUATERNION  = { 0, 0, 0, 1 };

const int32 PLAYER_ID = 1;
const int32 PACK_ID   = 2;
const int32 ID_OFFSET_BOX     	   = 100 * 1;
const int32 ID_OFFSET_MIRROR  	   = 100 * 2;
const int32 ID_OFFSET_CRYSTAL 	   = 100 * 3;
const int32 ID_OFFSET_SOURCE  	   = 100 * 4;
const int32 ID_OFFSET_PERM_MIRROR  = 100 * 5;
const int32 ID_OFFSET_WIN_BLOCK    = 100 * 6;
const int32 ID_OFFSET_LOCKED_BLOCK = 100 * 7;

const int32 FONT_FIRST_ASCII = 32;
const int32 FONT_LAST_ASCII = 126;
const int32 FONT_CELL_WIDTH_PX = 6;
const int32 FONT_CELL_HEIGHT_PX = 10;
const float DEFAULT_TEXT_SCALE = 30.0f;

const int32 CAMERA_CHUNK_SIZE = 24;
const char CAMERA_CHUNK_TAG[4] = "CMRA";
const int32 WIN_BLOCK_CHUNK_SIZE = 76;
const char WIN_BLOCK_CHUNK_TAG[4] = "WINB";
const int32 LOCKED_INFO_CHUNK_SIZE = 76;
const char LOCKED_INFO_CHUNK_TAG[4] = "LKIN";

const int32 OVERWORLD_SCREEN_SIZE_X = 15;
const int32 OVERWORLD_SCREEN_SIZE_Z = 15;

const double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0;

const char debug_level_name[64] = "red-mirror-i";
const char start_level_path_buffer[64] = "w:/cereus/data/levels/";
Int3 level_dim = {0};

Camera camera = {0};
Int3 camera_screen_offset = {0};
const Int3 camera_center_start = { 7, 0, -2 };
bool draw_camera_boundary = false;

AssetToLoad assets_to_load[1024] = {0};

WorldState world_state = {0};
WorldState next_world_state = {0};

WorldState undo_buffer[256] = {0};
int32 undo_buffer_position = 0;

Animation animations[32];
int32 time_until_input = 0;
EditorState editor_state = {0};
LaserBuffer laser_buffer[64] = {0};

const Vec2 DEBUG_TEXT_COORDS_START = { 50.0f, 1080.0f - 80.0f };
const float DEBUG_TEXT_Y_DIFF = 40.0f;
Vec2 debug_text_coords = {0}; 

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

Vec3 vec3Add(Vec3 a, Vec3 b) {
    return (Vec3){ a.x+b.x, a.y+b.y, a.z+b.z }; }

Int3 int3Add(Int3 a, Int3 b)
{
    return (Int3){ a.x+b.x, a.y+b.y, a.z+b.z }; 
}

Vec3 vec3Subtract(Vec3 a, Vec3 b) 
{
    return (Vec3){ a.x-b.x, a.y-b.y, a.z-b.z }; 
}

Vec3 vec3Multiply(Vec3 v, float s)
{
    return (Vec3){ v.x * s, v.y * s, v.z * s}; 
}

Vec3 vec3Abs(Vec3 a) 
{
    return (Vec3){ (float)fabs(a.x), (float)fabs(a.y), (float)fabs(a.z) }; 
}

Vec3 vec3ScalarMultiply(Vec3 position, float scalar) 
{
    return (Vec3){ position.x*scalar, position.y*scalar, position.z*scalar }; 
}

/*
   int32 roundFloatToInt(float f)
   {
   float d = fmod(f, 1);
   if (d >= 0.5f) return (int32)f + 1;
   else return (int32)f;
   }

   Int3 roundToInt3(Vec3 coords)
   {
   return (Int3){ roundFloatToInt(coords.x), roundFloatToInt(coords.y), roundFloatToInt(coords.z) };
   }
   */

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
        case BOX:     	   entity_group = next_world_state.boxes;    	  break;
        case MIRROR:  	   entity_group = next_world_state.mirrors;  	  break;
        case CRYSTAL: 	   entity_group = next_world_state.crystals; 	  break;
        case PERM_MIRROR:  entity_group = next_world_state.perm_mirrors;  break;
        case WIN_BLOCK:    entity_group = next_world_state.win_blocks;    break;
        case LOCKED_BLOCK: entity_group = next_world_state.locked_blocks; break;
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

Entity* getEntityFromId(int32 id)
{
    if (id == PLAYER_ID) return &next_world_state.player;
    else if (id == PACK_ID) return &next_world_state.pack;
    else 
    {
        Entity* entity_group = 0;
        int32 switch_value =  ((id / 100) * 100);
        if 		(switch_value == ID_OFFSET_BOX)    		 entity_group = next_world_state.boxes; 
        else if (switch_value == ID_OFFSET_MIRROR) 		 entity_group = next_world_state.mirrors;
        else if (switch_value == ID_OFFSET_CRYSTAL) 	 entity_group = next_world_state.crystals;
        else if (switch_value == ID_OFFSET_SOURCE) 		 entity_group = next_world_state.sources;
        else if (switch_value == ID_OFFSET_PERM_MIRROR)  entity_group = next_world_state.perm_mirrors;
        else if (switch_value == ID_OFFSET_WIN_BLOCK) 	 entity_group = next_world_state.win_blocks;
        else if (switch_value == ID_OFFSET_LOCKED_BLOCK) entity_group = next_world_state.locked_blocks;

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

        case NORTH_WEST: return SOUTH_EAST;
        case SOUTH_WEST: return NORTH_EAST;
        case SOUTH_EAST: return NORTH_WEST;
        case NORTH_EAST: return SOUTH_WEST;

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

// assumes NWSE
Direction getMiddleDirection(Direction direction_1, Direction direction_2)
{
    switch (direction_1)
    {
        case NORTH: switch (direction_2)
        {
            case WEST: return NORTH_WEST;
            case EAST: return NORTH_EAST;
            default: break;
        }
        case WEST: switch (direction_2)
        {
        	case NORTH: return NORTH_WEST;
        	case SOUTH: return SOUTH_WEST;
        	default: break;
        }
        case SOUTH: switch (direction_2)
        {
            case WEST: return SOUTH_WEST;
            case EAST: return SOUTH_EAST;
            default: break;
        }
        case EAST: switch (direction_2)
        {
            case NORTH: return NORTH_EAST;
            case SOUTH: return SOUTH_EAST;
            default: break;
        }
        default: return NO_DIRECTION;
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
    if 		(entity == next_world_state.boxes)    	   return ID_OFFSET_BOX;
    else if (entity == next_world_state.mirrors)  	   return ID_OFFSET_MIRROR;
    else if (entity == next_world_state.crystals) 	   return ID_OFFSET_CRYSTAL;
    else if (entity == next_world_state.sources)  	   return ID_OFFSET_SOURCE;
    else if (entity == next_world_state.perm_mirrors)  return ID_OFFSET_PERM_MIRROR;
    else if (entity == next_world_state.win_blocks)    return ID_OFFSET_WIN_BLOCK;
    else if (entity == next_world_state.locked_blocks) return ID_OFFSET_LOCKED_BLOCK;
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

// .level file structure: 
// first 4 bytes:    -,x,y,z of level dimensions
// next x*y*z * 2 (32768 by default) bytes: actual level buffer
// then chunking starts: 4 bytes for tag (e.g. CMRA), 4 bytes for int32 size of chunk (e.g 24), and then data for that chunk.

// solved-levels.meta structure:
// SLVL, 4 bytes for int32 size of chunk (so far always 64), and then the name of that level.

void buildLevelPathFromName(char level_name[64], char (*level_path)[64])
{
    snprintf(*level_path, sizeof(*level_path), "%s%s.level", start_level_path_buffer, level_name);
}

/*
   void getLevelNameFromPath(char* level_path, char* output)
   {
   char* slash = strrchr(level_path, '/'); // gets last / or .
   char* dot   = strrchr(level_path, '.');
   if (slash && dot && dot > slash)
   {
   size_t length = dot - slash - 1;
   strncpy(output, slash + 1, length);
   output[length] = '\0';
   }
   }
   */

// find the location of specific chunk
int32 findChunkOrEOF(FILE* file, char tag[4], bool* found)
{
    char chunk[4] = {0};
    int32 chunk_size = 0; // used to seek to the next chunk if this one is full
    int32 tag_pos = 0;

    fseek(file, 4 + (level_dim.x*level_dim.y*level_dim.z * 2), SEEK_SET); // go to start of chunking

    while (true) 
    {
        tag_pos = ftell(file);

        // check what this chunk says
        if (fread(chunk, 4, 1, file) != 1) 
        {
            // eof without chunk tag -> append to eof
            clearerr(file);
            *found = false;
            return tag_pos;
        }
        if (memcmp(chunk, tag, 4) == 0)
        {
            // camera chunk found -> overwrite
            *found = true;
            return tag_pos + 4;
        }

        // that chunk wasn't useful; jump ahead by chunk_size after reading it
        if (fread(&chunk_size, 4, 1, file) != 1)
        {
            // truncated (chunk tag found without accompanying int32 - should never happen)
            return -1; 
        }
        fseek(file, chunk_size, SEEK_CUR);
    }
}

bool readChunkHeader(FILE* file, char out_tag[4], int32 *out_size)
{
    if (fread(out_tag, 4, 1, file) != 1) return false; // EOF
    if (fread(out_size, 4, 1, file) != 1) return false; // truncated
    return true;
}

// doesn't load win block info, only loads buffer + camera
void loadFileToState(char* path)
{
    // get level dimensions
    FILE *file = fopen(path, "rb");

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

    bool found = false;
    int32 seek_to = findChunkOrEOF(file, CAMERA_CHUNK_TAG, &found);
    if (seek_to >= 0 && found == true)
    {
        fseek(file, seek_to, SEEK_SET); // go to after chunk marker
        fseek(file, 4, SEEK_CUR); // ...but still want to skip over chunk size
        fread(&camera.coords.x, 4, 1, file);
        fread(&camera.coords.y, 4, 1, file);
        fread(&camera.coords.z, 4, 1, file);
        fread(&camera.fov, 4, 1, file);
        fread(&camera.yaw, 4, 1, file);
        fread(&camera.pitch, 4, 1, file);
    }

    fclose(file);
    memcpy(next_world_state.buffer, buffer, level_dim.x*level_dim.y*level_dim.z * 2);
}

void writeBufferToFile(FILE* file)
{
    fseek(file, 4, SEEK_SET);
    fwrite(next_world_state.buffer, 1, level_dim.x*level_dim.y*level_dim.z * 2, file);
}

Camera getCurrentCameraInFile(FILE* file)
{
    Camera out_camera = {0};

    bool found = false;
    int32 seek_to = findChunkOrEOF(file, CAMERA_CHUNK_TAG, &found);
    if (seek_to >= 0 && found == true)
    {
        fseek(file, seek_to, SEEK_SET); // go to after chunk marker
        fseek(file, 4, SEEK_CUR); // ...but still want to skip over chunk size
        fread(&out_camera.coords.x, 4, 1, file);
        fread(&out_camera.coords.y, 4, 1, file);
        fread(&out_camera.coords.z, 4, 1, file);
        fread(&out_camera.fov, 4, 1, file);
        fread(&out_camera.yaw, 4, 1, file);
        fread(&out_camera.pitch, 4, 1, file);
    }
    return out_camera;
}

void writeCameraToFile(FILE* file, Camera* in_camera)
{
    bool found = false; // unused, because if not found just write to EOF anyway
    int32 seek_to = findChunkOrEOF(file, CAMERA_CHUNK_TAG, &found);
    fseek(file, seek_to, SEEK_SET);
    if (!found)
    {
        fwrite(CAMERA_CHUNK_TAG, 4, 1, file);
    }
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

int32 findWinBlockPath(FILE* file, int32 x, int32 y, int32 z)
{
    fseek(file, 4 + (level_dim.x*level_dim.y*level_dim.z * 2), SEEK_SET); // go to start of chunking

    char tag[4] = {0};
    int32 size = 0;

    while (readChunkHeader(file, tag, &size))
    {
        int32 pos = ftell(file);

        if (memcmp(tag, WIN_BLOCK_CHUNK_TAG, 4) == 0 && size == (int32)(WIN_BLOCK_CHUNK_SIZE))
        {
            int32 comp_x, comp_y, comp_z;
            char path[64] = {0};
            if (fread(&comp_x, 4, 1, file) != 1) return -1;
            if (fread(&comp_y, 4, 1, file) != 1) return -1;
            if (fread(&comp_z, 4, 1, file) != 1) return -1;
            if (fread(path, 1, 64, file) != 64) return -1;

            if (comp_x == x && comp_y == y && comp_z == z)
            {
                return (int32)(pos + 12);
            }
        }
        fseek(file, pos + size, SEEK_SET);
    }

    return -1; // nothing found at these coords
}

// TODO(spike): these two below functions could be combined for only one sweep through the file
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

            Entity* entity_group[3] = {next_world_state.boxes, next_world_state.mirrors, next_world_state.locked_blocks, /*next_world_state.crystals, next_world_state.sources*/};
            FOR(group_index, 3)
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

bool saveLevelRewrite(char* path)
{
    FILE* old_file = fopen(path, "rb+");
    Camera saved_camera = getCurrentCameraInFile(old_file);
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

    fwrite(next_world_state.buffer, 1, level_dim.x*level_dim.y*level_dim.z * 2, file);

    writeCameraToFile(file, &saved_camera);

    FOR(win_block_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        Entity* wb = &next_world_state.win_blocks[win_block_index];
        if (wb->id == -1) continue;
        if (wb->next_level[0] == '\0') continue;
        writeWinBlockToFile(file, wb);
    }

    Entity* entity_group[3] = {next_world_state.boxes, next_world_state.mirrors, next_world_state.locked_blocks, /*next_world_state.crystals, next_world_state.sources*/};
    FOR(group_index, 3)
    {
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            Entity* e = &entity_group[group_index][entity_index];
            if (e->id == -1) continue;
            if (e->unlocked_by[0] == '\0') continue;
            writeLockedInfoToFile(file, e);
        }
    }

    fclose(file);

    remove(path);
    if (rename(temp_path, path) != 0) return false;
    return true;
}

int32 findInSolvedLevels(char level_name[64], char (*solved_levels)[64][64])
{
    if (level_name[0] == '\0') return INT32_MAX; // if NULL string passed, return large number
    FOR(solved_level_index, MAX_LEVEL_COUNT) 
    {
        if (strcmp((*solved_levels)[solved_level_index], level_name) == 0) 
        {
            return solved_level_index;
        }
    }
    return -1;
}

int32 findNextFreeInSolvedLevels(char (*solved_levels)[64][64])
{
    FOR(solved_level_index, MAX_LEVEL_COUNT) if ((*solved_levels)[solved_level_index][0] == 0) return solved_level_index;
    return -1;
}

void writeSolvedLevelsToFile()
{

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
        case CRYSTAL:      return SPRITE_2D_CRYSTAL;
        case PACK:    	   return SPRITE_2D_PACK;
        case PERM_MIRROR:  return SPRITE_2D_PERM_MIRROR;
        case NOT_VOID:     return SPRITE_2D_NOT_VOID;
        case WIN_BLOCK:    return SPRITE_2D_WIN_BLOCK;
        case LOCKED_BLOCK: return SPRITE_2D_LOCKED_BLOCK;

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
        case CRYSTAL:      return CUBE_3D_CRYSTAL;
        case PACK:    	   return CUBE_3D_PACK;
        case PERM_MIRROR:  return CUBE_3D_PERM_MIRROR;
        case NOT_VOID:     return CUBE_3D_NOT_VOID;
        case WIN_BLOCK:    return CUBE_3D_WIN_BLOCK;
        case LOCKED_BLOCK: return CUBE_3D_LOCKED_BLOCK;

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

// assuming one path -> one asset type.
void drawAsset(SpriteId id, AssetType type, Vec3 coords, Vec3 scale, Vec4 rotation)
{
    int32 asset_location = -1;
    for (int32 asset_index = 0; asset_index < 256; asset_index++)
    {
        if (assets_to_load[asset_index].instance_count == 0)
        {
            if (asset_location == -1) asset_location = asset_index;
            continue;
        }
        if (assets_to_load[asset_index].sprite_id == id && assets_to_load[asset_index].type == type)
        {
            asset_location = asset_index;
            break;
        }
    }
    AssetToLoad* a = &assets_to_load[asset_location];

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

// RAYCAST ALGORITHM FOR EDITOR PLACE/DESTROY

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

// only checks tile types - doesn't do what canPush does
bool isPushable(TileType tile)
{
    if (tile == BOX || tile == CRYSTAL || tile == MIRROR || tile == PACK || isSource(tile)) return true;
    else return false;
}

bool isEntity(TileType tile)
{
    if (tile == BOX || tile == CRYSTAL || tile == MIRROR || tile == PERM_MIRROR || tile == PACK || tile == PLAYER || tile == WIN_BLOCK || tile == LOCKED_BLOCK || isSource(tile)) return true;
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
        if (next_world_state.trailing_hitboxes[find_hitbox_index].frames != 0) continue;
        return find_hitbox_index;
    }
    return 0;
}

void createTrailingHitbox(Int3 coords, Direction direction, int32 frames, TileType type)
{
    int32 hitbox_index = findNextFreeInTrailingHitboxes();
    next_world_state.trailing_hitboxes[hitbox_index].coords = coords;
    next_world_state.trailing_hitboxes[hitbox_index].direction = direction;
    next_world_state.trailing_hitboxes[hitbox_index].frames = frames;
    next_world_state.trailing_hitboxes[hitbox_index].type = type;
}

bool trailingHitboxAtCoords(Int3 coords, TrailingHitbox* trailing_hitbox)
{
    FOR(trailing_hitbox_index, MAX_TRAILING_HITBOX_COUNT) 
    {
        TrailingHitbox th = next_world_state.trailing_hitboxes[trailing_hitbox_index];
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

void doFailedWalkAnimations()
{
    int32 stack_size = getPushableStackSize(next_world_state.player.coords); // counts player as member of stack
    Int3 current_coords = next_world_state.player.coords;
    FOR(stack_index, stack_size) 
    {
        createFailedWalkAnimation(intCoordsToNorm(current_coords), intCoordsToNorm(getNextCoords(current_coords, next_world_state.player.direction)), &getEntityPointer(current_coords)->position_norm, getEntityPointer(current_coords)->id);
        current_coords = getNextCoords(current_coords, UP);
    }
    if (!next_world_state.pack_detached) createFailedWalkAnimation(intCoordsToNorm(next_world_state.pack.coords), intCoordsToNorm(next_world_state.player.coords), &next_world_state.pack.position_norm, PACK_ID);

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
    if (!e->in_motion) if (getTileType(getNextCoords(e->coords, DOWN)) != NONE) e->first_fall_already_done = false;
}

PushResult canPush(Int3 coords, Direction direction)
{
    Int3 current_coords = coords;
    TileType current_tile = getTileType(current_coords);
    for (int push_index = 0; push_index < MAX_ENTITY_PUSH_COUNT; push_index++) 
    {
        Entity* entity = getEntityPointer(current_coords);
        if (isEntity(current_tile) && entity->locked) return FAILED_PUSH;

        if (entity->in_motion) return PAUSE_PUSH;

        // TODO(spike): need to introduce PAUSE_PUSH if entity is going to fall next frame. (this is what below is maybe doing?)
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
    int32 trailing_hitbox_time = TRAILING_HITBOX_TIME;
    createTrailingHitbox(coords, direction, trailing_hitbox_time, trailing_hitbox_type);
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

            default: return NO_DIRECTION;
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

            default: return NO_DIRECTION;
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
 
            default: return NO_DIRECTION;
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

            default: return NO_DIRECTION;
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

         	default: return NO_DIRECTION;
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
 
            default: return NO_DIRECTION;
        }
		default: return NO_DIRECTION;
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
        default: return NO_DIRECTION;
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
        default: return NO_DIRECTION;
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

int32 findNextFreeInLaserBuffer()
{
    FOR(laser_buffer_index, MAX_PSEUDO_SOURCE_COUNT) if (laser_buffer[laser_buffer_index].color == NO_COLOR) return laser_buffer_index;
    return -1;
}

void updateLaserBuffer(void)
{
    Entity* player = &next_world_state.player;

    memset(laser_buffer, 0, sizeof(laser_buffer));

    player->hit_by_red   = false;
    player->hit_by_blue  = false;
    player->green_hit = (GreenHit){0};

    FOR(source_index, MAX_PSEUDO_SOURCE_COUNT)
    {
        Entity* source = &next_world_state.sources[source_index]; // TODO(spike): will not work with multicolored sources
        if (source->id == -1) continue;
        Direction current_direction = source->direction;
        Int3 current_tile_coords = source->coords;
        Vec3 offset = {0};

        int32 skip_next_mirror = 0;

        FOR(laser_turn_index, MAX_LASER_TURNS_ALLOWED)
        {
            int32 laser_buffer_index = findNextFreeInLaserBuffer();
            LaserBuffer* lb = &laser_buffer[laser_buffer_index];
            bool no_more_turns = true; 

            lb->start_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
            lb->direction = current_direction;
            lb->color = source->color;
            current_tile_coords = getNextCoords(current_tile_coords, current_direction);

            FOR(laser_tile_index, MAX_LASER_TRAVEL_DISTANCE)
            {
                no_more_turns = true;

                if (!intCoordsWithinLevelBounds(current_tile_coords))
                {
                    lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                    break;
                }

                if ((next_world_state.pack_hitbox_turning_from_timer > 0) 
                && (int3IsEqual(current_tile_coords, next_world_state.pack_hitbox_turning_from_coords) && (next_world_state.pack_hitbox_turning_from_direction == current_direction)) 
                || ((next_world_state.pack_hitbox_turning_to_timer > 0) 
                && int3IsEqual(current_tile_coords, next_world_state.pack_hitbox_turning_to_coords)   && (next_world_state.pack_hitbox_turning_to_direction   == current_direction))) 
                {
                    Vec3 end_coords = vec3Multiply(vec3Add(intCoordsToNorm(current_tile_coords), intCoordsToNorm(getNextCoords(current_tile_coords, oppositeDirection(current_direction)))), 0.5);
                    lb->end_coords = vec3Add(end_coords, offset); 
                    break;
                }

                TrailingHitbox th = {0};
                bool th_hit = false;
                if (trailingHitboxAtCoords(current_tile_coords, &th) && th.frames > 0) th_hit = true;
                else memset(&th, 0, sizeof(TrailingHitbox));

                if (getTileType(current_tile_coords) == PLAYER || (th_hit && th.type == PLAYER))
                {
                    bool skip_check = false;
                    if (player->moving_direction == current_direction || player->moving_direction == oppositeDirection(current_direction)) 
                    {
                        lb->end_coords = vec3Add(player->position_norm, offset); 
                        skip_check = true;
                    }
                    if (skip_check || ((player->in_motion < STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH || player->moving_direction == NO_DIRECTION) && (th_hit == false || th.type == PLAYER)))
                    {	
                        if (!skip_check)
                        {
                            lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                        }
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
                }

                if (getTileType(current_tile_coords) == CRYSTAL || (th_hit && th.type == CRYSTAL))
                {
                    Entity* crystal = {0};
                    if (th_hit) crystal = getEntityPointer(getNextCoords(current_tile_coords, th.direction));
                    else crystal = getEntityPointer(current_tile_coords);

                    if (crystal->in_motion && crystal->moving_direction == oppositeDirection(current_direction)) 
                    {
                        Vec3 to_vector = directionToVector(crystal->moving_direction);
                        float dir_offset = (float)(MOVE_OR_PUSH_ANIMATION_TIME - crystal->in_motion) / (float)(MOVE_OR_PUSH_ANIMATION_TIME);
                        offset = vec3Add(vec3Negate(to_vector), vec3Multiply(to_vector, dir_offset));
                        lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                    }
                    else if (!th_hit && !(crystal->in_motion < STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH)) 
                    {
                        current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                        continue;
                    }
                    else
                    {
                        lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                    }

                    if 		(lb->color == RED) 	current_direction = getRedDirectionAtCrystal(current_direction);
                    else if (lb->color == BLUE) current_direction = getBlueDirectionAtCrystal(current_direction);
                    no_more_turns = false;
                    break;
                }

                if ((getTileType(current_tile_coords) == MIRROR || (th_hit && th.type == MIRROR)) && !skip_next_mirror)
                {
                    Entity* mirror = {0};
                    if (th_hit) mirror = getEntityPointer(getNextCoords(current_tile_coords, th.direction));
                    else mirror = getEntityPointer(current_tile_coords);

                    if (!th_hit)
                    {
                        // hit mirror tile
                        if (mirror->in_motion && mirror->moving_direction == oppositeDirection(current_direction))
                        {
                            Vec3 to_vector = directionToVector(mirror->moving_direction);
                            float dir_offset = (float)(MOVE_OR_PUSH_ANIMATION_TIME - mirror->in_motion) / (float)(MOVE_OR_PUSH_ANIMATION_TIME);
                            offset = vec3Add(vec3Negate(to_vector), vec3Multiply(to_vector, dir_offset));
                            lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                        }
                        else if (mirror->in_motion && mirror->moving_direction == getNextLaserDirectionMirror(current_direction, mirror->direction))
                        {
                            if (mirror->in_motion < STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH)
                            {
                                Vec3 to_vector = directionToVector(oppositeDirection(current_direction));
                                float dir_offset = (float)(MOVE_OR_PUSH_ANIMATION_TIME - mirror->in_motion) / (float)(MOVE_OR_PUSH_ANIMATION_TIME);
                                offset = vec3Add(vec3Negate(to_vector), vec3Multiply(to_vector, dir_offset));
                                lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                            }
                            else
                            {
                                current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                                continue;
                            }
                        }
                        else if (mirror->in_motion && mirror->moving_direction == oppositeDirection(getNextLaserDirectionMirror(current_direction, mirror->direction)))
                        {
                            if (mirror->in_motion < STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH)
                            {
                                Vec3 to_vector = directionToVector(current_direction);
                                float dir_offset = (float)(MOVE_OR_PUSH_ANIMATION_TIME - mirror->in_motion) / (float)(MOVE_OR_PUSH_ANIMATION_TIME);
                                offset = vec3Add(vec3Negate(to_vector), vec3Multiply(to_vector, dir_offset));
                                lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                            }
                            else
                            {
                                current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                                continue;
                            }
                        }
                        else
                        {
                            lb->end_coords = intCoordsToNorm(current_tile_coords);
                        }
                    }
                    else
                    {
                        // TODO(spike): need to figure out if pushed by pack or player, and if so change the equivalent of MOVE_OR_PUSH_ANIMATION_TIME that we're checking against

                        // trailing hitbox hit
                        if (mirror->moving_direction == getNextLaserDirectionMirror(current_direction, mirror->direction))
                        {
                            Vec3 to_vector = directionToVector(current_direction);
                            float dir_offset = (float)(mirror->in_motion) / (float)(MOVE_OR_PUSH_ANIMATION_TIME);
                            offset = vec3Add(vec3Negate(to_vector), vec3Multiply(to_vector, dir_offset));
                            lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                            skip_next_mirror = 2;
                        }
                        else if (mirror->moving_direction == oppositeDirection(getNextLaserDirectionMirror(current_direction, mirror->direction)))
                        {
                            Vec3 to_vector = directionToVector(oppositeDirection(current_direction));
                            float dir_offset = (float)(mirror->in_motion) / (float)(MOVE_OR_PUSH_ANIMATION_TIME);
                            offset = vec3Add(vec3Negate(to_vector), vec3Multiply(to_vector, dir_offset));
                            lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                            skip_next_mirror = 2;
                        }
                        else
                        {
                            // get here with pack swing from top when laser is pointing straight down onto mirror
                            lb->end_coords = intCoordsToNorm(current_tile_coords);
                            break;
                        }
                    }

                    current_direction = getNextLaserDirectionMirror(current_direction, mirror->direction);
                    no_more_turns = false;
                    break;
                }
                if (getTileType(current_tile_coords) != NONE || (th_hit))
                {
                    if (isEntity(getTileType(current_tile_coords)))
                    {
                        if (!th_hit) 
                        {
                            Entity* e = getEntityPointer(current_tile_coords);
                            if (e->in_motion < STANDARD_IN_MOTION_TIME_FOR_LASER_PASSTHROUGH)
                            {
                                lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                                break;
                            }
                            else
                            {
                                current_tile_coords = getNextCoords(current_tile_coords, current_direction);
                                continue;
                            }
                        }
                        else
                        {
                            lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                            break;
                        }
                    }
                    else
                    {
                        lb->end_coords = vec3Add(intCoordsToNorm(current_tile_coords), offset);
                        break;
                    }
                }

                current_tile_coords = getNextCoords(current_tile_coords, current_direction);
            }

            if (no_more_turns) break;
        }
    }
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
    Entity* entity_group[4] = {next_world_state.boxes, next_world_state.mirrors, next_world_state.crystals, next_world_state.sources };
    FOR(entity_group_index, 4)
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
    if (!(isPushable(getTileType(next_coords)) && getEntityPointer(next_coords)->id == -1) && getTileType(next_coords) != NONE) return true;
    TrailingHitbox _;
    if (trailingHitboxAtCoords(next_coords, &_) && entity->id != PLAYER_ID) return true;

    int32 stack_size = getPushableStackSize(entity->coords);
    Int3 current_start_coords = entity->coords;
    Int3 current_end_coords = next_coords; 
    FOR(stack_fall_index, stack_size)
    {
        Entity* entity_in_stack = getEntityPointer(current_start_coords);
        if (entity_in_stack->id == -1) return false; // should never happen, shouldn't have id == -1 in the middle of a stack somewhere
        if (entity_in_stack->in_motion) return false; 
        if (entity_in_stack == &next_world_state.pack && !next_world_state.pack_detached) return false;
        if (entity_in_stack == &next_world_state.player && !next_world_state.player.hit_by_red) time_until_input = FALL_ANIMATION_TIME;

        // switch on if this is going to be first fall
        if (!entity_in_stack->first_fall_already_done)
        {
            if (do_animation) 
            {
                createFirstFallAnimation(intCoordsToNorm(current_start_coords), &entity_in_stack->position_norm, entity_in_stack->id);
                createTrailingHitbox(current_start_coords, DOWN, TRAILING_HITBOX_TIME + 4, getTileType(entity_in_stack->coords)); // it takes 4 extra frames to get to the point where it's cutting off the below laser (and thus not cutting off above, i guess)
            }
            entity_in_stack->first_fall_already_done = true;
            entity_in_stack->in_motion = STANDARD_IN_MOTION_TIME + 4;
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
                createTrailingHitbox(current_start_coords, DOWN, TRAILING_HITBOX_TIME, getTileType(entity_in_stack->coords));
            }
            entity_in_stack->first_fall_already_done = true;
            entity_in_stack->in_motion = STANDARD_IN_MOTION_TIME;
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
    Entity* object_group_to_fall[4] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.crystals, next_world_state.sources };
    FOR(to_fall_index, 4)
    {
        Entity* entity_group = object_group_to_fall[to_fall_index];
        FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
        {
            if (next_world_state.pack_hitbox_turning_to_timer > 0 && int3IsEqual(next_world_state.pack_hitbox_turning_to_coords, entity_group[entity_index].coords)) continue; // blocks blue-not-blue turn orthogonal case from falling immediately
            doFallingEntity(&entity_group[entity_index], do_animation);
        }
    }
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
    PushResult push_result = canPushStack(coords_above_player, direction);
    if (push_result == CAN_PUSH) pushAll(coords_above_player, direction, animation_time, animations_on, false);
}

void doStandardMovement(Direction input_direction, Int3 next_player_coords, int32 animation_time)
{
    Entity* player = &next_world_state.player;
    Entity* pack = &next_world_state.pack;

    doHeadMovement(input_direction, true, animation_time);

    createInterpolationAnimation(intCoordsToNorm(player->coords), 
                                 intCoordsToNorm(next_player_coords), 
                                 &player->position_norm,
                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0,
                                 PLAYER_ID, animation_time);

    int32 trailing_hitbox_time = TRAILING_HITBOX_TIME;
    createTrailingHitbox(player->coords, input_direction, trailing_hitbox_time, PLAYER);

    // move pack also maybe
    if (next_world_state.pack_detached) 
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

        createTrailingHitbox(pack->coords, input_direction, trailing_hitbox_time, PACK);

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

    recordStateForUndo();
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
        if (current_tile == MIRROR || current_tile == PERM_MIRROR)
        {
            current_direction = getNextLaserDirectionMirror(current_direction, getTileDirection(current_coords));
            continue;
        }
        if (current_tile != NONE) break;
    }
    next_world_state.player_ghost_coords = getNextCoords(current_coords, oppositeDirection(current_direction));
    next_world_state.player_ghost_direction = current_direction;
    if (!next_world_state.pack_detached) 
    {
        next_world_state.pack_ghost_coords = getNextCoords(next_world_state.player_ghost_coords, oppositeDirection(current_direction));
        next_world_state.pack_ghost_direction = current_direction;
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

        if (tick_input->j_press && time_until_input == 0)
        {
            if (editor_state.do_wide_camera) editor_state.do_wide_camera = false;
            else editor_state.do_wide_camera = true;
            time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
        }
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
                    entity->id = -1;
                }
                setTileType(NONE, raycast_output.hit_coords);
                setTileDirection(NORTH, raycast_output.hit_coords);
            }
            else if ((tick_input->right_mouse_press || tick_input->h_press) && raycast_output.hit) 
            {
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
                        // TODO(spike): make this switch statement a function, so don't have to keep changing all of them every time there is a new entity added
                        case BOX:     	   entity_group = next_world_state.boxes;    	  break;
                        case MIRROR:  	   entity_group = next_world_state.mirrors;  	  break;
                        case CRYSTAL: 	   entity_group = next_world_state.crystals; 	  break;
                        case PERM_MIRROR:  entity_group = next_world_state.perm_mirrors;  break;
                        case WIN_BLOCK:    entity_group = next_world_state.win_blocks;    break;
                        case LOCKED_BLOCK: entity_group = next_world_state.locked_blocks; break;
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
            else if ((tick_input->middle_mouse_press || tick_input->g_press) && raycast_output.hit) editor_state.picked_tile = getTileType(raycast_output.hit_coords);

            time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
        }
        else if (time_until_input == 0 && tick_input->l_press)
        {
            editor_state.picked_tile++;
            if (editor_state.picked_tile == LOCKED_BLOCK + 1) editor_state.picked_tile = VOID;
            time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
        }
    }

    if (editor_state.editor_mode == SELECT_WRITE)
    {
        char (*writing_to_field)[64] = 0;
        Entity* e = getEntityFromId(editor_state.selected_id);
        if 		(editor_state.writing_field == WRITING_FIELD_NEXT_LEVEL)  writing_to_field = &e->next_level;
        else if (editor_state.writing_field == WRITING_FIELD_UNLOCKED_BY) writing_to_field = &e->unlocked_by;

        if (tick_input->enter_pressed_this_frame)
        {
            memset(*writing_to_field, '0', sizeof(*writing_to_field));
            memcpy(*writing_to_field, editor_state.edit_buffer.string, sizeof(*writing_to_field) - 1);
            editor_state.editor_mode = SELECT;
            editor_state.selected_id = 0;
            editor_state.writing_field = NO_WRITING_FIELD;
        }
        else if (tick_input->escape_press)
        {
            editor_state.editor_mode = SELECT;
            editor_state.selected_id = 0;
            editor_state.writing_field = NO_WRITING_FIELD;
        }

        updateTextInput(tick_input);
    }
    else // TODO(spike): maybe only need to memset this when exiting select write, or when changing modes
    {
        memset(&editor_state.edit_buffer, 0, sizeof(editor_state.edit_buffer));
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

        if (editor_state.selected_id > 0)
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
        }
    }
}

// GAME

void gameInitialiseState()
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

    if (strcmp(next_world_state.level_name, "overworld") == 0) next_world_state.in_overworld = true;
    else next_world_state.in_overworld = false;

    // build level_path from level_name
    char level_path[64] = {0};
    buildLevelPathFromName(next_world_state.level_name, &level_path);
    loadFileToState(level_path);

    memset(next_world_state.boxes,    	   0, sizeof(next_world_state.boxes)); 
    memset(next_world_state.mirrors,  	   0, sizeof(next_world_state.mirrors));
    memset(next_world_state.crystals, 	   0, sizeof(next_world_state.crystals));
    memset(next_world_state.sources,  	   0, sizeof(next_world_state.sources));
    memset(next_world_state.perm_mirrors,  0, sizeof(next_world_state.perm_mirrors));
    memset(next_world_state.win_blocks,    0, sizeof(next_world_state.win_blocks));
    memset(next_world_state.locked_blocks, 0, sizeof(next_world_state.locked_blocks));
    FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
    {
        next_world_state.boxes[entity_index].id 	    = -1;
        next_world_state.mirrors[entity_index].id 	    = -1;
        next_world_state.crystals[entity_index].id 	    = -1;
        next_world_state.sources[entity_index].id 	    = -1;
        next_world_state.perm_mirrors[entity_index].id  = -1;
        next_world_state.win_blocks[entity_index].id    = -1;
        next_world_state.locked_blocks[entity_index].id = -1;
    }
    Entity *entity_group = 0;
    for (int buffer_index = 0; buffer_index < 2 * level_dim.x*level_dim.y*level_dim.z; buffer_index += 2)
    {
        TileType buffer_contents = next_world_state.buffer[buffer_index];
        if 	    (buffer_contents == BOX)     	  entity_group = next_world_state.boxes;
        else if (buffer_contents == MIRROR)  	  entity_group = next_world_state.mirrors;
        else if (buffer_contents == CRYSTAL) 	  entity_group = next_world_state.crystals;
        else if (buffer_contents == PERM_MIRROR)  entity_group = next_world_state.perm_mirrors;
        else if (buffer_contents == WIN_BLOCK)    entity_group = next_world_state.win_blocks;
        else if (buffer_contents == LOCKED_BLOCK) entity_group = next_world_state.locked_blocks;
        else if (isSource(buffer_contents))  	  entity_group = next_world_state.sources;
        if (entity_group != 0)
        {
            int32 count = getEntityCount(entity_group);
            entity_group[count].coords = bufferIndexToCoords(buffer_index);
            entity_group[count].position_norm = intCoordsToNorm(entity_group[count].coords);
            entity_group[count].direction = next_world_state.buffer[buffer_index + 1]; 
            entity_group[count].rotation_quat = directionToQuaternion(entity_group[count].direction, true);
            entity_group[count].color = getEntityColor(entity_group[count].coords);
            entity_group[count].id = getEntityCount(entity_group) + entityIdOffset(entity_group);
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

    FILE* file = fopen(level_path, "rb+");
    loadWinBlockPaths(file);
    loadLockedInfoPaths(file);
    camera = getCurrentCameraInFile(file);
    fclose(file);

    camera_screen_offset.x = (int32)(camera.coords.x / OVERWORLD_SCREEN_SIZE_X);
    camera_screen_offset.z = (int32)(camera.coords.z / OVERWORLD_SCREEN_SIZE_Z);

    Vec4 quaternion_yaw   = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), camera.yaw);
    Vec4 quaternion_pitch = quaternionFromAxisAngle(intCoordsToNorm(AXIS_X), camera.pitch);
    camera.rotation  = quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));
    world_state = next_world_state;
}

void gameInitialise(char* level_name) 
{	
    // TODO(spike): panic if cannot open constructed level path (check if can open here before we pass on)
    if (level_name == 0) strcpy(next_world_state.level_name, debug_level_name);
    else strcpy(next_world_state.level_name, level_name);
    gameInitialiseState();
}

void gameFrame(double delta_time, TickInput tick_input)
{	
    if (delta_time > 0.1) delta_time = 0.1;
    accumulator += delta_time;

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
                // undo
                int32 next_undo_buffer_position = 0;
                if (undo_buffer_position != 0) next_undo_buffer_position = undo_buffer_position - 1;
                else next_undo_buffer_position = UNDO_BUFFER_SIZE - 1;

                if (undo_buffer[next_undo_buffer_position].player.id != 0) // check that there is anything in the buffer (using something that should never usually happen)
                {
                    char previous_level_name[256] = {0};
                    strcpy(previous_level_name, next_world_state.level_name); 

                    next_world_state = undo_buffer[next_undo_buffer_position];
                   	memset(&undo_buffer[undo_buffer_position], 0, sizeof(WorldState));
                    undo_buffer_position = next_undo_buffer_position;
                    memset(animations, 0, sizeof(animations));
                    resetStandardVisuals(); // set position_norm and rotation_quat to coords and direction respectively
                    
                    if (strcmp(next_world_state.level_name, previous_level_name) != 0) gameInitialise(next_world_state.level_name);
                }
                time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
            }
            if (time_until_input == 0 && tick_input.r_press)
            {
                // restart
                recordStateForUndo();
                memset(animations, 0, sizeof(animations));
                gameInitialiseState();
                time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
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
                        TileType player_ghost_tile = getTileType(next_world_state.player_ghost_coords);
                        TileType pack_ghost_tile = getTileType(next_world_state.pack_ghost_coords);
                        if ((player_ghost_tile == NONE || player_ghost_tile == PLAYER) && (pack_ghost_tile == NONE || pack_ghost_tile == PLAYER || pack_ghost_tile == PACK)) allow_tp = true;

                        if (allow_tp)
                        {
                            if (!int3IsEqual(next_world_state.player_ghost_coords, player->coords))
                            {
								recordStateForUndo();

                                setTileType(NONE, player->coords);
                                setTileDirection(NORTH, player->coords);
                                zeroAnimations(PLAYER_ID);
                                player->coords = next_world_state.player_ghost_coords;
                                player->position_norm = intCoordsToNorm(next_world_state.player_ghost_coords);
                                player->direction = next_world_state.player_ghost_direction;
                                player->rotation_quat = directionToQuaternion(next_world_state.player_ghost_direction, true);
                                setTileType(PLAYER, next_world_state.player_ghost_coords);
                                setTileDirection(next_world_state.player_ghost_direction, next_world_state.player_ghost_coords);
                                if (!next_world_state.pack_detached)
                                {
                                    Int3 pack_coords = getNextCoords(next_world_state.player_ghost_coords, oppositeDirection(next_world_state.pack_ghost_direction));
                                    setTileType(NONE, pack->coords);
                                    setTileDirection(NORTH, pack->coords);
                                    zeroAnimations(PACK_ID);
                                    pack->coords = pack_coords; 
                                    pack->position_norm = intCoordsToNorm(pack_coords);
                                    pack->direction = next_world_state.pack_ghost_direction;
                                    pack->rotation_quat = directionToQuaternion(next_world_state.pack_ghost_direction, true);
                                    setTileType(PACK, pack_coords);
                                    setTileDirection(next_world_state.pack_ghost_direction, pack_coords);
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
                    }
                    else
                    {
                        // no ghosts, but still need to check if player is green at all 
                        bool do_push = false;
                        bool move_player = false;
                        bool do_failed_animations = false;
                        int32 animation_time = 0;
                        if 		(tick_input.w_press) next_player_coords = int3Add(player->coords, int3Negate(AXIS_Z));
                        else if (tick_input.a_press) next_player_coords = int3Add(player->coords, int3Negate(AXIS_X));
                        else if (tick_input.s_press) next_player_coords = int3Add(player->coords, AXIS_Z);
                        else if (tick_input.d_press) next_player_coords = int3Add(player->coords, AXIS_X);
                        TileType next_tile = getTileType(next_player_coords);
                        if (!next_world_state.player_will_fall_next_turn) switch (next_tile)
                        {
                            case NONE:
                            {
                                Int3 coords_ahead = next_player_coords;
                                Int3 coords_below_and_ahead = getNextCoords(next_player_coords, DOWN);
                                if (isPushable(getTileType(coords_ahead)) && getEntityPointer(coords_ahead)->in_motion) move_player = false;
                                else if (isPushable(getTileType(coords_below_and_ahead)) && getEntityPointer(coords_below_and_ahead)->in_motion) move_player = false;
                                else
                                {
                                    move_player = true;
                                    animation_time = MOVE_OR_PUSH_ANIMATION_TIME;
                                }
                                break;
                            }
                            case BOX:
                            case CRYSTAL:
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
                                //figure out if push, pause, or fail here.
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
                            if (tile_below != NONE || player->hit_by_red)
                            {
                                if (do_push) pushAll(next_player_coords, input_direction, animation_time, true, false);
                                doStandardMovement(input_direction, next_player_coords, animation_time);
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
                                    if (next_world_state.pack_detached) doFallingEntity(pack, animations_on);
                                }

                                doHeadMovement(input_direction, animations_on, 1);

                                setTileType(NONE, player->coords);
                                player->coords = next_player_coords;
                                setTileType(PLAYER, player->coords);	

                                if (!next_world_state.pack_detached)
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
                                    doStandardMovement(input_direction, next_player_coords, animation_time);
                                    next_world_state.bypass_player_fall = true; 
                                    time_until_input = MOVE_OR_PUSH_ANIMATION_TIME;
                                }
                                else 
                                {
                                    doFailedWalkAnimations();
                                    time_until_input = FAILED_ANIMATION_TIME;
                                    updateLaserBuffer();
                                }

                            }
                        }
						else if (do_failed_animations) 
                        {
                            doFailedWalkAnimations();
                            time_until_input = FAILED_ANIMATION_TIME;
                        }
                    }
                }
                else
                {
                    if (input_direction != oppositeDirection(player->direction)) // check if turning (as opposed to trying to reverse)
                    {
                        // player is turning
                        Direction polarity_direction = NORTH;
                        int32 clockwise = false;
                        int32 clockwise_calculation = player->direction - input_direction;
                        if (clockwise_calculation == -1 || clockwise_calculation == 3) clockwise = true;

                        if (next_world_state.pack_detached)
                        {
                            // if pack detached, always allow turn
                            if (isPushable(getTileType(getNextCoords(player->coords, UP)))) doHeadRotation(clockwise);

                            createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0, 
                                                         directionToQuaternion(player->direction, true), 
                                                         directionToQuaternion(input_direction, true), 
                                                         &player->rotation_quat,
                                                         1, TURN_ANIMATION_TIME); 
                            player->direction = input_direction;
                            setTileDirection(player->direction, player->coords);
                            player->moving_direction = NO_DIRECTION;

                            recordStateForUndo();
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
                                    case CRYSTAL:
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
                                    case CRYSTAL:
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

                                if (allow_turn_orthogonal)
                                {
                                    // actually turning: rotate player
                                    // first, handle various trailing / phantom hitboxes
									createTrailingHitbox(pack->coords, input_direction, FIRST_TRAILING_PACK_TURN_HITBOX_TIME, PACK);

                                    Direction to_dir = getMiddleDirection(diagonal_push_direction, orthogonal_push_direction);
									next_world_state.pack_hitbox_turning_from_timer = TURN_ANIMATION_TIME;
                                    next_world_state.pack_hitbox_turning_from_coords = pack->coords;
                                    next_world_state.pack_hitbox_turning_from_direction = oppositeDirection(to_dir);
                                    next_world_state.pack_hitbox_turning_to_timer = TURN_ANIMATION_TIME + TIME_BEFORE_ORTHOGONAL_PUSH_STARTS_IN_TURN;
                                    next_world_state.pack_hitbox_turning_to_coords = orthogonal_coords;
                                    next_world_state.pack_hitbox_turning_to_direction = to_dir;

                                    if (isPushable(getTileType(getNextCoords(player->coords, UP)))) doHeadRotation(clockwise);

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

                                    if (push_diagonal)   next_world_state.do_diagonal_push_on_turn = true;
                                    if (push_orthogonal) next_world_state.do_orthogonal_push_on_turn = true;

                                    next_world_state.pack_intermediate_states_timer = TIME_BEFORE_ORTHOGONAL_PUSH_STARTS_IN_TURN + PACK_TIME_IN_INTERMEDIATE_STATE + 1; // + 1 because we stop when timer hits 1 (and then reset to 0)
                                    next_world_state.pack_intermediate_coords = diagonal_coords;
                                    next_world_state.pack_orthogonal_push_direction = orthogonal_push_direction;

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
            editorMode(&tick_input);
        }

        // handle pack turning sequence TODO(spike): name all of these numbers better
        if (next_world_state.pack_intermediate_states_timer > 0)
        {
            if (next_world_state.pack_intermediate_states_timer == 7)
            {
				if (next_world_state.do_diagonal_push_on_turn) pushAll(next_world_state.pack_intermediate_coords, oppositeDirection(player->direction), PUSH_FROM_TURN_ANIMATION_TIME, true, true);
                createTrailingHitbox(pack->coords, pack->direction, 4, PACK);
            }
            else if (next_world_state.pack_intermediate_states_timer == 7 - TIME_BEFORE_ORTHOGONAL_PUSH_STARTS_IN_TURN)
            {
                setTileType(NONE, pack->coords);
                setTileDirection(NORTH, pack->coords);
                pack->coords = next_world_state.pack_intermediate_coords;
                pack->direction = oppositeDirection(player->direction);
                setTileType(PACK, pack->coords);
                setTileDirection(pack->direction, pack->coords);
                createTrailingHitbox(next_world_state.pack_intermediate_coords, pack->direction, 4, PACK);
            }
            else if (next_world_state.pack_intermediate_states_timer == 4)
            {
                if (next_world_state.do_orthogonal_push_on_turn) pushAll(next_world_state.pack_hitbox_turning_to_coords, next_world_state.pack_orthogonal_push_direction, PUSH_FROM_TURN_ANIMATION_TIME, true, true);
            }
            else if (next_world_state.pack_intermediate_states_timer == 1)
            { 
                setTileType(NONE, pack->coords);
                setTileDirection(NORTH, pack->coords);
				pack->coords = next_world_state.pack_hitbox_turning_to_coords;
                setTileType(PACK, pack->coords);
                setTileDirection(pack->direction, pack->coords);

                if (next_world_state.do_player_and_pack_fall_after_turn)
                {
                    doFallingEntity(player, true);
                    doFallingEntity(pack, true);
                    next_world_state.do_player_and_pack_fall_after_turn = false;
                }
                next_world_state.player_hit_by_blue_in_turn = false;
            }
            next_world_state.pack_intermediate_states_timer--;
        }

        if (player->in_motion > 0)
        {
            updateLaserBuffer();
        }
        else
        {
            updateLaserBuffer();
        }

        // falling logic
		if (!player->hit_by_blue) doFallingObjects(true); // built in guard here against pushable at location of pack_hitbox_turning_to_timer;

        if (next_world_state.pack_intermediate_states_timer == 0)
        {
            if (!player->hit_by_red)
            {
                if (!next_world_state.pack_detached)
                {
                    if (getTileType(getNextCoords(player->coords, DOWN)) == NONE) next_world_state.player_will_fall_next_turn = true; 
                    else next_world_state.player_will_fall_next_turn = false;

                    // not red and pack attached: player always falls, if no bypass. pack only falls if player falls
                    if (!next_world_state.bypass_player_fall && !doFallingEntity(player, true))
                    {
                        if (doFallingEntity(pack, true))
                        {
                            // pack wants to fall but cannot: we already know player can fall, so pack will become unattached // TODO(spike): is this required with the new check below?
                            next_world_state.pack_detached = true;
                        }
                    }
                }
                else
                {
                    if (getTileType(getNextCoords(player->coords, DOWN)) == NONE) next_world_state.player_will_fall_next_turn = true;
                    else next_world_state.player_will_fall_next_turn = false;
                    // not red and pack not attached, so pack and player both always fall
                    if (!next_world_state.bypass_player_fall) doFallingEntity(player, true);
                    doFallingEntity(pack, true);
                }
            }
            else
            {
                next_world_state.player_will_fall_next_turn = false;
                // red, so pack only falls if is detached from player
                if (next_world_state.pack_detached)
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
                next_world_state.do_player_and_pack_fall_after_turn = true;
            }

            if (isPushable(getTileType(next_world_state.pack_hitbox_turning_to_coords)))
            {
				if (player->hit_by_blue) next_world_state.player_hit_by_blue_in_turn = true;
                if (!player->hit_by_blue && next_world_state.player_hit_by_blue_in_turn && next_world_state.pack_intermediate_states_timer > 0)
                {
                    next_world_state.entity_to_fall_after_blue_not_blue_turn_timer = next_world_state.pack_intermediate_states_timer + 3; // this number is magic (sorry); it is the frame count that makes the entity fall as soon as possible, i.e., at the same time as the player (if magenta-not-magenta)
                    next_world_state.entity_to_fall_after_blue_not_blue_turn_coords = getNextCoords(next_world_state.pack_hitbox_turning_to_coords, next_world_state.pack_orthogonal_push_direction);
                    next_world_state.player_hit_by_blue_in_turn = false;
                }
            }
        }

        if (next_world_state.entity_to_fall_after_blue_not_blue_turn_timer > 0)
        {
            if (next_world_state.entity_to_fall_after_blue_not_blue_turn_timer == 1) 
            {
                if (isPushable(getTileType(next_world_state.entity_to_fall_after_blue_not_blue_turn_coords))) doFallingEntity(getEntityPointer(next_world_state.entity_to_fall_after_blue_not_blue_turn_coords), true);
            }
            next_world_state.entity_to_fall_after_blue_not_blue_turn_timer--;
        }

        // detach or reattach pack
        TileType tile_behind_player = getTileType(getNextCoords(player->coords, oppositeDirection(player->direction)));
        if (!next_world_state.pack_detached && next_world_state.pack_intermediate_states_timer == 0 && tile_behind_player != PACK) next_world_state.pack_detached = true;
        if (next_world_state.pack_detached && tile_behind_player == PACK) next_world_state.pack_detached = false;

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
        Entity* falling_entity_groups[4] = { next_world_state.boxes, next_world_state.mirrors, next_world_state.crystals, next_world_state.sources };
        FOR(falling_object_index, 4)
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
        if (next_world_state.pack_hitbox_turning_from_timer > 0) next_world_state.pack_hitbox_turning_from_timer--;
        if (next_world_state.pack_hitbox_turning_to_timer   > 0) next_world_state.pack_hitbox_turning_to_timer--;

        // don't allow inputs if player is moving (this is for meta commands - some redundancy here: pauses already happen if player not allowed to move yet otherwise)
        if (player->in_motion) time_until_input = 1; // TODO(spike): is this needed? it's at least somewhat redundant

        // decide which ghosts to render, if ghosts should be rendered
        bool do_player_ghost = false;
        bool do_pack_ghost = false;
		if (calculateGhosts())
        {
            do_player_ghost = true;
            if (!next_world_state.pack_detached) do_pack_ghost = true;
        }

        // decrement trailing hitboxes 
        FOR(i, MAX_TRAILING_HITBOX_COUNT) if (next_world_state.trailing_hitboxes[i].frames > 0) next_world_state.trailing_hitboxes[i].frames--;
        if (next_world_state.bypass_player_fall) next_world_state.bypass_player_fall = false;

		// delete objects if above void
        if (!player->hit_by_blue) // TODO(spike): maybe just wrap this into the falling logic?
        {
            Entity* entity_group[4] = {next_world_state.boxes, next_world_state.mirrors, next_world_state.crystals, next_world_state.sources };
            FOR(entity_group_index, 4)
            {
                FOR(entity_instance_index, MAX_ENTITY_INSTANCE_COUNT)
                {
                    Entity* entity = &entity_group[entity_group_index][entity_instance_index];
                    if (entity->id == -1) continue;
                    if (getTileType(getNextCoords(entity->coords, DOWN)) == VOID && !entity->in_motion) // TODO(spike): still some bug when pushing walking onto a place where another 
                                                                                                             // entity has fallen and disappeared causing player to be able to walk over void for a few frames
                    {
                        setTileType(NONE, entity->coords);
                        entity->id = -1;
                    }
                }
            }
        }

        // delete player / pack if above void
        if (!player->hit_by_red)
        {
            if ((getTileType(getNextCoords(player->coords, DOWN)) == VOID || getTileType(getNextCoords(player->coords, DOWN)) == NOT_VOID) && !presentInAnimations(PLAYER_ID)) player->id = -1;
        }
        if (next_world_state.pack_detached)
        {
            if (!player->hit_by_blue)
            {
                if ((getTileType(getNextCoords(pack->coords, DOWN)) == VOID   || getTileType(getNextCoords(pack->coords, DOWN)) == NOT_VOID  ) && !presentInAnimations(PACK_ID))   pack->id = -1;
            }
        }
        else
        {
            if (!player->hit_by_red)
            {
                if ((getTileType(getNextCoords(pack->coords, DOWN)) == VOID   || getTileType(getNextCoords(pack->coords, DOWN)) == NOT_VOID  ) && !presentInAnimations(PACK_ID))   pack->id = -1;
            }
        }

        // win block logic
        if ((getTileType(getNextCoords(player->coords, DOWN)) == WIN_BLOCK && !presentInAnimations(PLAYER_ID)) && (tick_input.q_press && time_until_input == 0))
        {
            if (next_world_state.in_overworld) 
            {
                char level_path[64] = {0};
                buildLevelPathFromName(next_world_state.level_name, &level_path);
                saveLevelRewrite(level_path);
            }

            Entity* wb = getEntityPointer(getNextCoords(player->coords, DOWN));
            if (wb->next_level[0] != 0)
            {
                if (strcmp(wb->next_level, "overworld") == 0) next_world_state.in_overworld = true;
                recordStateForUndo();
                memset(animations, 0, sizeof(animations));

                if (findInSolvedLevels(next_world_state.level_name, &next_world_state.solved_levels) == -1)
                {
                    int32 next_free = findNextFreeInSolvedLevels(&next_world_state.solved_levels);
                    strcpy(next_world_state.solved_levels[next_free], next_world_state.level_name);
            	}

                time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;

                gameInitialise(wb->next_level);
            }
        }

		// figure out if entities should be locked / unlocked (should probably try to update this more intelligently later?)
        if (editor_state.editor_mode == NO_MODE)
        {
            Entity* entity_group[2] = {next_world_state.boxes, next_world_state.mirrors, /*next_world_state.crystals, next_world_state.sources*/};

            FOR(group_index, 2)
            {
                FOR(entity_index, MAX_ENTITY_INSTANCE_COUNT)
                {
                    Entity* e = &entity_group[group_index][entity_index];
                    if (e->id == -1) continue;
                    if (findInSolvedLevels(e->unlocked_by, &next_world_state.solved_levels) == -1) e->locked = true; 
                    else e->locked = false;
                }
            }
            // similar code for locked_blocks
            FOR(locked_block_index, MAX_ENTITY_INSTANCE_COUNT)
            {
                Entity* lb = &next_world_state.locked_blocks[locked_block_index];
                if (lb->id == -1) continue;
                int32 find_result = findInSolvedLevels(lb->unlocked_by, &next_world_state.solved_levels);
                if (find_result != -1 && find_result != INT32_MAX)
                {
					// locked block to be unlocked
                    lb->id = -1;
                    setTileType(NONE, lb->coords);
                    setTileDirection(NORTH, lb->coords);
                }
            }
        }

        // final redo of laser buffer, after all logic is complete, for drawing
		updateLaserBuffer();

		// adjust overworld camera based on position
        if (next_world_state.in_overworld)
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

        // finished updating state
        world_state = next_world_state;

		// DRAW 3D

        // draw lasers
        updateLaserBuffer();

		FOR(laser_buffer_index, 64)
        {
            LaserBuffer lb = laser_buffer[laser_buffer_index];
            int32 color_id = 0;
            switch (lb.color)
            {
                case RED: color_id = getCube3DId(LASER_RED); break;
                case GREEN: color_id = getCube3DId(LASER_GREEN); break;
                case BLUE: color_id = getCube3DId(LASER_BLUE); break;
                default: break;
            }
            if (color_id == 0) break;

			Vec3 diff = vec3Subtract(lb.end_coords, lb.start_coords);
            Vec3 center = vec3Add(lb.start_coords, vec3Multiply(diff, 0.5));
    		Vec3 scale = {0};
            Vec4 rotation = IDENTITY_QUATERNION;

            if (!isDiagonal(lb.direction))
            {
                scale = vec3Abs(diff);
                if (scale.x == 0) scale.x = LASER_WIDTH;
                if (scale.y == 0) scale.y = LASER_WIDTH;
                if (scale.z == 0) scale.z = LASER_WIDTH;
            }
            else
            {
                scale.x = LASER_WIDTH;
                scale.y = LASER_WIDTH;
				if 		(diff.x == 0) scale.z = (float)sqrt((diff.y*diff.y) + (diff.z*diff.z));
				else if (diff.y == 0) scale.z = (float)sqrt((diff.x*diff.x) + (diff.z*diff.z));
				else if (diff.z == 0) scale.z = (float)sqrt((diff.x*diff.x) + (diff.y*diff.y));
                rotation = directionToQuaternion(lb.direction, false);
            }

            drawAsset(color_id, CUBE_3D, center, scale, rotation);
        }

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
                drawAsset(getCube3DId(draw_tile), CUBE_3D, e->position_norm, DEFAULT_SCALE, e->rotation_quat); 
            }
			else
            {
                drawAsset(getCube3DId(draw_tile), CUBE_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToQuaternion(next_world_state.buffer[tile_index + 1], false));
            }
        }

        if (world_state.player.id != -1)
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

            if (do_player_ghost) drawAsset(CUBE_3D_PLAYER_GHOST, CUBE_3D, intCoordsToNorm(world_state.player_ghost_coords), PLAYER_SCALE, directionToQuaternion(world_state.player_ghost_direction, true));
            if (do_pack_ghost)   drawAsset(CUBE_3D_PACK_GHOST,   CUBE_3D, intCoordsToNorm(world_state.pack_ghost_coords),   PLAYER_SCALE, directionToQuaternion(world_state.pack_ghost_direction, true));
        }
		if (world_state.pack.id != -1) drawAsset(CUBE_3D_PACK, CUBE_3D, world_state.pack.position_norm, PLAYER_SCALE, world_state.pack.rotation_quat);

		// draw sources 
		for (int source_index = 0; source_index < MAX_ENTITY_INSTANCE_COUNT; source_index++)
        {
            if (world_state.sources[source_index].id == -1) continue;
            int32 id = getCube3DId(getTileType(world_state.sources[source_index].coords));
            drawAsset(id, CUBE_3D, world_state.sources[source_index].position_norm, DEFAULT_SCALE, world_state.sources[source_index].rotation_quat);
        }

		// DRAW 2D
        
        // display level name
		drawDebugText(next_world_state.level_name);

        // player in_motion info
		char player_moving_text[256] = {0};
        snprintf(player_moving_text, sizeof(player_moving_text), "player moving: %d", player->in_motion);
		drawDebugText(player_moving_text);

        // player in_motion info
		char player_dir_text[256] = {0};
        snprintf(player_dir_text, sizeof(player_dir_text), "player moving: %d", player->moving_direction);
		drawDebugText(player_dir_text);

        // camera pos info
        char camera_text[256] = {0};
        snprintf(camera_text, sizeof(camera_text), "camera pos: %f, %f, %f", camera.coords.x, camera.coords.y, camera.coords.z);
        drawDebugText(camera_text);

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

            if (editor_state.selected_id > 0 && (editor_state.editor_mode == SELECT || editor_state.editor_mode == SELECT_WRITE))
            {
                drawAsset(0, OUTLINE_3D, intCoordsToNorm(editor_state.selected_coords), DEFAULT_SCALE, IDENTITY_QUATERNION);
            }
        }

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
                    drawDebugText(selected_id_text);

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

                    // TODO(spike): macro for this declaration + snprintf + drawDebugText
                    char next_level_text[256] = {0};
                    char unlocked_by_text[256] = {0};
                    snprintf(next_level_text, sizeof(next_level_text), "next_level: %s", e->next_level);
                    snprintf(unlocked_by_text, sizeof(unlocked_by_text), "unlocked_by: %s", e->unlocked_by);
                    drawDebugText(next_level_text);
                    drawDebugText(unlocked_by_text);
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
			time_until_input = EDITOR_INPUT_TIME_UNTIL_ALLOW;
        }

        if (draw_camera_boundary && next_world_state.in_overworld)
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
                    drawAsset(0, OUTLINE_3D, x_draw_coords, x_wall_scale, IDENTITY_QUATERNION);
                    drawAsset(0, OUTLINE_3D, z_draw_coords, z_wall_scale, IDENTITY_QUATERNION);
					x_draw_offset += OVERWORLD_SCREEN_SIZE_X;
                }
                x_draw_offset = 0;
                z_draw_offset += OVERWORLD_SCREEN_SIZE_Z;
            }
        }

        // decide which camera to use
        if (editor_state.do_wide_camera) camera.fov = 60.0f;
        else camera.fov = CAMERA_FOV;

        // write to file
        char level_path[64];
        buildLevelPathFromName(world_state.level_name, &level_path);
        if (time_until_input == 0 && editor_state.editor_mode == PLACE_BREAK && tick_input.i_press) 
        {
            saveLevelRewrite(level_path);
            // TODO(spike): writeSolvedLevelsToFile();
        }

        if (time_until_input == 0 && editor_state.editor_mode == PLACE_BREAK && tick_input.c_press) 
        {
            FILE* file = fopen(level_path, "rb+");
            writeCameraToFile(file, &camera);
            fclose(file);
        }

        tick_input.mouse_dx = 0;
        tick_input.text.count = 0;
        tick_input.backspace_pressed_this_frame = false;
        tick_input.enter_pressed_this_frame = false;

		if (time_until_input > 0) time_until_input--;

        rendererSubmitFrame(assets_to_load, camera);
        memset(assets_to_load, 0, sizeof(assets_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
