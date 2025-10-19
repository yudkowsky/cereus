#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"

#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> // TODO(spike): also temporary, for sin/cos
#include <stdio.h> // TODO(spike): "temporary", for fopen 
// #include <windows.h> // TODO(spike): ok, actually temporary this time, for outputdebugstring

#define local_persist static
#define global_variable static
#define internal static

typedef enum TileType
{
    NONE    = 0,
    VOID    = 1,
    GRID    = 2,
    WALL    = 3,
    BOX     = 4,
    PLAYER  = 5,
    MIRROR  = 6,
    CRYSTAL = 7,

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

typedef struct Entity
{
    Int3 coords;
    Vec3 position_norm;
    Direction direction;
    Vec4 rotation_quat;
    int32 id;

    // only sources/lasers/other colored objects
    Color color;

    // only player
    bool hit_by_red;
    bool hit_by_green;
    bool hit_by_blue;
}
Entity;

typedef struct Animation
{
    int32 frames_left;
    Vec3* position_to_change;
    Vec4* rotation_to_change;
    Vec3 position[32];
    Vec4 rotation[32];
}
Animation;

typedef struct WorldState
{
    uint8 buffer[16384]; // 2 bytes info per tile 
    Entity player;
    Entity boxes[32];
    Entity mirrors[32];
    Entity sources[32];
    Entity crystals[32];
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
    Int3 previous_coords[32];
    Int3 new_coords[32];
    TileType type[32];
    Entity* pointer_to_entity[32];
    int32 count;
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

double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0.0;

float TAU = 6.28318530f;

float SENSITIVITY = 0.005f;
float MOVE_STEP = 0.05f;
Vec3 DEFAULT_SCALE = { 1.0f,   1.0f,   1.0f  };
Vec3 PLAYER_SCALE  = { 0.75f,  0.75f,  0.75f };
Vec3 ORTHOGONAL_LASER_SCALE = { 0.125f, 0.125f, 1.0f   };
Vec3 DIAGONAL_LASER_SCALE   = { 0.125f, 0.125f, 1.415f };
float RAYCAST_SEEK_LENGTH = 20.0f;
int32 INPUT_TIME_UNTIL_ALLOW = 8;
int32 ANIMATION_TIME = 8;
int32 MAX_ENTITY_INSTANCE_COUNT = 32;
int32 MAX_ENTITY_PUSH_COUNT = 32;
int32 MAX_ANIMATION_COUNT = 32;
int32 MAX_LASER_TRAVEL_DISTANCE = 48;
int32 MAX_PSEUDO_SOURCE_COUNT = 128;

Int3 AXIS_X = { 1, 0, 0 };
Int3 AXIS_Y = { 0, 1, 0 };
Int3 AXIS_Z = { 0, 0, 1 };
Vec3 IDENTITY_TRANSLATION = { 0, 0, 0 };
Vec4 IDENTITY_QUATERNION = { 0, 0, 0, 1};

Camera camera = {0};
float camera_yaw = 0.0f;
float camera_pitch = 0.0f;

char* level_path = "w:/cereus/data/levels/level-2.txt"; // absolute path required to modify original file
Int3 level_dim = {0};

WorldState world_state = {0};
WorldState next_world_state = {0};
EditorState editor_state = {0};
Animation animations[32];

int32 time_until_input = 0;

char* void_path    = "data/sprites/void.png";
char* grid_path    = "data/sprites/grid.png";
char* wall_path    = "data/sprites/wall.png";
char* box_path     = "data/sprites/box.png";
char* player_path  = "data/sprites/player.png";
char* mirror_path  = "data/sprites/mirror.png";
char* crystal_path = "data/sprites/crystal.png";

char* laser_red_path     = "data/sprites/laser-red.png";
char* laser_green_path   = "data/sprites/laser-green.png";
char* laser_blue_path    = "data/sprites/laser-blue.png";
char* laser_magenta_path = "data/sprites/laser-magenta.png";
char* laser_yellow_path  = "data/sprites/laser-yellow.png";
char* laser_cyan_path    = "data/sprites/laser-cyan.png";
char* laser_white_path   = "data/sprites/laser-white.png";

char* source_red_path     = "data/sprites/source-red.png";
char* source_green_path   = "data/sprites/source-green.png";
char* source_blue_path    = "data/sprites/source-blue.png";
char* source_magenta_path = "data/sprites/source-magenta.png";
char* source_yellow_path  = "data/sprites/source-yellow.png";
char* source_cyan_path    = "data/sprites/source-cyan.png";
char* source_white_path   = "data/sprites/source-white.png";

AssetToLoad assets_to_load[256] = {0};

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

Vec3 vec3Negative(Vec3 coords) {
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
    Entity *group_pointer = 0;
    if (isSource(tile)) group_pointer = next_world_state.sources;
    else switch(tile)
    {
        case PLAYER:  return &next_world_state.player;
        case BOX:     group_pointer = next_world_state.boxes;    break;
        case MIRROR:  group_pointer = next_world_state.mirrors;  break;
        case CRYSTAL: group_pointer = next_world_state.crystals; break;
        default: return 0;
    }
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (int3IsEqual(group_pointer[entity_index].coords, coords)) return &group_pointer[entity_index];
    }
    return 0;
}

Direction getEntityDirection(Int3 coords) 
{
	Entity *entity_pointer = getEntityPointer(coords);
    return entity_pointer->direction;
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

int32 getEntityCount(Entity *pointer_to_array)
{
    int32 count = 0;
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
	{
		if (pointer_to_array[entity_index].id == -1) continue;
        count++;
    }
    return count;
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
            yaw  = 0.0f;         roll = -0.125f * TAU;
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
            yaw  = 0.0f;         roll = 0.125f * TAU;
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
	/*
    if (do_yaw)  return quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), yaw);
    if (do_roll && roll_z)  return quaternionFromAxisAngle(intCoordsToNorm(AXIS_Z), roll);
    if (do_roll && !roll_z) return quaternionFromAxisAngle(intCoordsToNorm(AXIS_X), roll);
    */
    return IDENTITY_QUATERNION;
}

void setEntityInstanceInGroup(Entity* group_pointer, Int3 coords, Direction direction, Color color) 
{
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (group_pointer[entity_index].id != -1) continue;
        group_pointer[entity_index].coords = coords;
        group_pointer[entity_index].position_norm = intCoordsToNorm(coords); 
        group_pointer[entity_index].direction = direction;
        group_pointer[entity_index].rotation_quat = directionToQuaternion(direction, true);
        group_pointer[entity_index].id = entity_index;
        group_pointer[entity_index].color = color;
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

    uint8 buffer[16384]; // level_dim.x*level_dim.y*level_dim.z * 2 for color bytes
	fread(&buffer, 1, level_dim.x*level_dim.y*level_dim.z, file);
	fclose(file);
    memcpy(next_world_state.buffer, buffer, level_dim.x*level_dim.y*level_dim.z);
}

void writeBufferToFile(char* path)
{
    FILE *file = fopen(path, "rb+");
    fseek(file, 4, SEEK_CUR);
	fwrite(world_state.buffer, 1, 16384, file);
    fclose(file);
}

// DRAW ASSET

char* getPath(TileType tile)
{
    switch(tile)
    {
        case NONE:    return 0;
        case VOID:    return void_path;
        case GRID:    return grid_path;
        case WALL:    return wall_path;
        case BOX:     return box_path;
        case PLAYER:  return player_path;
        case MIRROR:  return mirror_path;
        case CRYSTAL: return crystal_path;

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

void drawEntityLoop(Entity* group_pointer, char* path, AssetType type, Vec3 scale)
{
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
    {
        if (group_pointer[entity_index].id == -1) continue;
        drawAsset(path, type, group_pointer[entity_index].position_norm, scale, group_pointer[entity_index].rotation_quat);
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

// ANIMATION HELPER 

Vec3 rollingAxis(Direction direction)
{
	Vec3 up = { 0.0f, 1.0f, 0.0f };
    Vec3 rolling = {0};
    switch (direction)
    {
        case NORTH: rolling = intCoordsToNorm(int3Negate(AXIS_Z)); break;
        case WEST:  rolling = intCoordsToNorm(int3Negate(AXIS_X)); break;
        case SOUTH: rolling = intCoordsToNorm(AXIS_Z);	      	   break;
        case EAST:  rolling = intCoordsToNorm(AXIS_X);		 	   break;
        default: return IDENTITY_TRANSLATION;
    }
	return vec3CrossProduct(up, rolling);
}

// ANIMATIONS

void createInterpolationAnimation(Vec3 position_a, Vec3 position_b, Vec3* position_to_change, Vec4 rotation_a, Vec4 rotation_b, Vec4* rotation_to_change)
{
    // find next free in animations
    int32 animation_index = -1;
    for (int find_anim_index = 0; find_anim_index < MAX_ANIMATION_COUNT; find_anim_index++)
    {
        if (animations[find_anim_index].frames_left == 0)
        {
            animation_index = find_anim_index;
            animations[animation_index] = (Animation){0};
            break;
        }
    }

    // set frame count. the last frame here is the true/correct position
    animations[animation_index].frames_left = ANIMATION_TIME;

	Vec3 translation_per_frame   = vec3ScalarMultiply(vec3Subtract(position_b, position_a), (float)(1.0f/ANIMATION_TIME));

    if (!vec3IsZero(translation_per_frame))
    {
        animations[animation_index].position_to_change = position_to_change;
        for (int frame_index = 0; frame_index < animations[animation_index].frames_left; frame_index++)
        {
            animations[animation_index].position[animations[animation_index].frames_left-(1+frame_index)] 
            = vec3Add(*position_to_change, vec3ScalarMultiply(translation_per_frame, (float)(1+frame_index)));
        }
    }
    if (!quaternionIsZero(quaternionSubtract(rotation_b, rotation_a)))
    {
        animations[animation_index].rotation_to_change = rotation_to_change;
        if (quaternionDot(rotation_a, rotation_b) < 0.0f) rotation_b = quaternionScalarMultiply(rotation_b, -1.0f);
        for (int frame_index = 0; frame_index < animations[animation_index].frames_left; frame_index++)
        {
            float param = (float)(frame_index + 1) / animations[animation_index].frames_left;
	    	animations[animation_index].rotation[animations[animation_index].frames_left-(1+frame_index)] 
            = quaternionNormalize(quaternionAdd(quaternionScalarMultiply(rotation_a, 1.0f - param), quaternionScalarMultiply(rotation_b, param)));
        }
    }
}

void createRollingAnimation(Vec3 position, Direction direction, Vec3* position_to_change, Vec4 rotation_a, Vec4 rotation_b, Vec4* rotation_to_change)
{	
    // find next free in animations
    int32 animation_index = -1;
    for (int find_anim_index = 0; find_anim_index < MAX_ANIMATION_COUNT; find_anim_index++)
    {
        if (animations[find_anim_index].frames_left == 0)
        {
            animation_index = find_anim_index;
            animations[animation_index] = (Animation){0};
            break;
        }
    }
    
    animations[animation_index].frames_left = ANIMATION_TIME;
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
	float d_theta_per_frame = (TAU*0.25f)/(float)ANIMATION_TIME;

    for (int frame_index = 0; frame_index < animations[animation_index].frames_left; frame_index++)
    {
        // rotation
        float param = (float)(frame_index + 1) / animations[animation_index].frames_left;
        animations[animation_index].rotation[animations[animation_index].frames_left-(1+frame_index)] 
        = quaternionNormalize(quaternionAdd(quaternionScalarMultiply(rotation_a, 1.0f - param), quaternionScalarMultiply(rotation_b, param)));
        
        // translation
		float theta = (frame_index+1) * d_theta_per_frame;
        Vec4 roll = quaternionFromAxisAngle(axis, theta);
        Vec3 relative_rotation = vec3RotateByQuaternion(pivot_to_cube_center, roll);
        animations[animation_index].position[animations[animation_index].frames_left-(1+frame_index)] = vec3Add(pivot_point, relative_rotation);
    }
}

// PUSH HELPER (ALTHOUGH 3/4 OF THIS IS FOR LASERS)

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
    }
    return (Int3){0};
}

// PUSH / ROLL ENTITES

bool canPush(Int3 coords, Direction direction)
{
    Int3 current_coords = coords;
    TileType current_tile;
    for (int push_index = 0; push_index < MAX_ENTITY_PUSH_COUNT; push_index++) 
    {
    	current_coords = getNextCoords(current_coords, direction);
        current_tile = getTileType(current_coords);
        if (!intCoordsWithinLevelBounds(current_coords)) return false;
        if (isSource(current_tile)) return false;
        if (current_tile == WALL) return false;
        if (current_tile == NONE) return true;
    }
    return false; // only here if hit the max entity push count
}

void push(Int3 coords, Direction direction)
{
    Push entities_to_push = {0}; 
	Int3 current_coords = coords;
    for (int push_index = 0; push_index < MAX_ENTITY_PUSH_COUNT; push_index++)
    {
        entities_to_push.type[push_index] = getTileType(current_coords);
		entities_to_push.previous_coords[push_index] = current_coords;
        entities_to_push.pointer_to_entity[push_index] = getEntityPointer(current_coords);
        current_coords = getNextCoords(current_coords, direction);
        entities_to_push.new_coords[push_index] = current_coords; 
        entities_to_push.count++;
        if (getTileType(current_coords) == NONE) break;
    }
    for (int entity_index = 0; entity_index < entities_to_push.count; entity_index++)
    {
        entities_to_push.pointer_to_entity[entity_index]->coords = entities_to_push.new_coords[entity_index];
        createInterpolationAnimation(intCoordsToNorm(entities_to_push.previous_coords[entity_index]),
                                     intCoordsToNorm(entities_to_push.new_coords[entity_index]),
                                     &entities_to_push.pointer_to_entity[entity_index]->position_norm,
                                     IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0);
        if (entity_index == 0) setTileType(NONE, entities_to_push.pointer_to_entity[entity_index]->coords); // will necessarily be overwritten by player / object doing the pushing; keeping for safety for now
        setTileType(entities_to_push.type[entity_index], entities_to_push.new_coords[entity_index]);
    }
}

// encoding new dir by prev dir and push dir rather than by final quat. assumes 6-dim dir
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

void roll(Int3 coords, Direction direction)
{
    Entity* pointer = getEntityPointer(coords);
	setTileType(NONE, coords);
    setTileType(MIRROR, getNextCoords(coords, direction));
	pointer->coords = getNextCoords(coords, direction);
	Vec4 quaternion_transform = quaternionNormalize(quaternionMultiply(quaternionFromAxisAngle(rollingAxis(direction), 0.25f*TAU), directionToQuaternion(pointer->direction, true)));
    
    createRollingAnimation(intCoordsToNorm(coords), 
            			   direction,
                           &pointer->position_norm, 
                           directionToQuaternion(pointer->direction, true), 
                           quaternion_transform,
        				   &pointer->rotation_quat);
	Direction new_direction = getNextMirrorState(pointer->direction, direction);
    pointer->direction = new_direction;
}

// LASERS

bool canMirrorReflect(Direction laser_direction, Direction mirror_direction)
{
    /*
    switch (laser_direction)
    {
        case NORTH: 
        case SOUTH: switch (mirror_direction)
        {
            case WEST: 
            case EAST: return false;
            default: return true;
        }
        case WEST:
        case EAST: switch (mirror_direction)
        {
            case NORTH: 
            case SOUTH: return false;
            default: return true;
        }
        case UP:
        case DOWN: switch (mirror_direction)
        {
            case UP: 
            case DOWN: return false;
            default: return true;
        }
        default: return false;
    }
    */
    
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

void addPrimarySource(Entity* new_source_pointer, Entity* original_source_pointer, int32 *total_source_count, Color color)
{
    new_source_pointer->coords = original_source_pointer->coords;
    new_source_pointer->direction = original_source_pointer->direction;
    new_source_pointer->id = 10000 + original_source_pointer->id;
    new_source_pointer->color = color;
    (*total_source_count)++;
}

bool isDiagonal(Direction direction)
{
    if (direction == NORTH || direction == SOUTH || direction == WEST || direction == EAST || direction == UP || direction == DOWN) return false;
    else return true;
}

void gameInitialise(void) 
{	
    loadFileToBuffer(level_path);

    memset(next_world_state.boxes,    -1, sizeof(next_world_state.boxes)); 
    memset(next_world_state.mirrors,  -1, sizeof(next_world_state.mirrors));
    memset(next_world_state.crystals, -1, sizeof(next_world_state.crystals));
    memset(next_world_state.sources,  -1, sizeof(next_world_state.sources));
    Entity *pointer = 0;
    for (int buffer_index = 0; buffer_index < 2 * level_dim.x*level_dim.y*level_dim.z; buffer_index += 2)
    {
        TileType buffer_contents = next_world_state.buffer[buffer_index];
        if (buffer_contents == BOX)     pointer = next_world_state.boxes;
        if (buffer_contents == MIRROR)  pointer = next_world_state.mirrors;
        if (buffer_contents == CRYSTAL) pointer = next_world_state.crystals;
        if (isSource(buffer_contents))  pointer = next_world_state.sources;
        if (pointer != 0)
        {
            int32 count = getEntityCount(pointer);
			pointer[count].coords = bufferIndexToCoords(buffer_index);
            pointer[count].position_norm = intCoordsToNorm(pointer[count].coords);
            pointer[count].direction = next_world_state.buffer[buffer_index + 1]; 
            pointer[count].rotation_quat = directionToQuaternion(pointer[count].direction, true);
            pointer[count].color = getEntityColor(pointer[count].coords);
            pointer[count].id = getEntityCount(pointer);
            pointer = 0;
        }
        else if (next_world_state.buffer[buffer_index] == PLAYER) // special case for player, since there is only one
        {
            next_world_state.player.coords = bufferIndexToCoords(buffer_index);
            next_world_state.player.position_norm = intCoordsToNorm(next_world_state.player.coords);
            next_world_state.player.direction = next_world_state.buffer[buffer_index + 1];
            next_world_state.player.rotation_quat = directionToQuaternion(next_world_state.player.direction, true);
            next_world_state.player.id = 0;
        }
    }
    next_world_state.player.coords = (Int3){3,1,3};
    next_world_state.player.position_norm = intCoordsToNorm(next_world_state.player.coords);

	camera.coords = (Vec3){10, 12, 15};
    camera_yaw = 0; // towards -z; north
    camera_pitch = -TAU * 0.18f; // look down-ish
    Vec4 quaternion_yaw   = quaternionFromAxisAngle(intCoordsToNorm(AXIS_Y), camera_yaw);
    Vec4 quaternion_pitch = quaternionFromAxisAngle(intCoordsToNorm(AXIS_X), camera_pitch);
    camera.rotation  = quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));
    world_state = next_world_state;
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
                time_until_input = INPUT_TIME_UNTIL_ALLOW;
            }
            if (time_until_input == 0 && (tick_input.w_press || tick_input.a_press || tick_input.s_press || tick_input.d_press))
            {
                Direction input_direction = 0;
                Int3 next_player_coords = {0};
                if 		(tick_input.w_press) input_direction = NORTH; 
                else if (tick_input.a_press) input_direction = WEST; 
                else if (tick_input.s_press) input_direction = SOUTH; 
                else if (tick_input.d_press) input_direction = EAST; 

                if (input_direction == next_world_state.player.direction)
                {
               		// already facing this direction; attempt to move 
                    bool move_player = false;
                    if 		(tick_input.w_press) next_player_coords = int3Add(next_world_state.player.coords, int3Negate(AXIS_Z));
                    else if (tick_input.a_press) next_player_coords = int3Add(next_world_state.player.coords, int3Negate(AXIS_X));
                    else if (tick_input.s_press) next_player_coords = int3Add(next_world_state.player.coords, AXIS_Z);
                    else if (tick_input.d_press) next_player_coords = int3Add(next_world_state.player.coords, AXIS_X);
                    TileType next_tile = getTileType(next_player_coords);
					if (!isSource(next_tile)) switch (next_tile)
                    {
                        case VOID: break;
                        case GRID: break;
                        case WALL: break;
                        case BOX:
                        case CRYSTAL:
                        {
                            if (canPush(next_player_coords, input_direction)) 
                            {
                                push(next_player_coords, input_direction);
								move_player = true;
                            }
                            break;
                        }
                        case MIRROR:
                        {
                            if (canPush(next_player_coords, input_direction))
                            {
                                // currently not allowing roll unless there is free space ahead
                                TileType push_tile = getTileType(getNextCoords(next_player_coords, input_direction));
                                if (push_tile != NONE) break; // push(getNextCoords(next_player_coords, input_direction), input_direction);
                                roll(next_player_coords, input_direction);
                                move_player = true;
                            }
                            break;
                        }
                        default:
                        {
                            move_player = true;
                        }
                    }
                    if (move_player)
                    {
                        // don't allow walking off edge
                        if (getTileType(int3Add(next_player_coords, int3Negate(AXIS_Y))) != NONE)
                        {
                            createInterpolationAnimation(intCoordsToNorm(next_world_state.player.coords), 
                                                         intCoordsToNorm(next_player_coords), 
                                                         &next_world_state.player.position_norm,
                                                         IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0);
                            setTileType(NONE, next_world_state.player.coords);
                            next_world_state.player.coords = next_player_coords;
                            setTileType(PLAYER, next_world_state.player.coords);
                        }
                    }
                }
                else
                {
                   	bool opposite = (abs(input_direction - next_world_state.player.direction) == 2);
                    if (!opposite) 
                    {
                        createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0, 
                                					 directionToQuaternion(next_world_state.player.direction, true), 
                                                     directionToQuaternion(input_direction, true), 
                                                     &next_world_state.player.rotation_quat);
                        next_world_state.player.direction = input_direction;
                    }
        		}
                time_until_input = INPUT_TIME_UNTIL_ALLOW;
            }
        }
        else
        {
            Vec3 right_camera_basis, forward_camera_basis;
            cameraBasisFromYaw(camera_yaw, &right_camera_basis, &forward_camera_basis);

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
                time_until_input = INPUT_TIME_UNTIL_ALLOW;
            }

			if (time_until_input == 0 && tick_input.j_press)
            {
                if (normCoordsWithinLevelBounds(camera.coords))
                {
					setTileType(VOID, normCoordsToInt(camera.coords));
                    time_until_input = INPUT_TIME_UNTIL_ALLOW;
                }
            }

            // inputs that require raycast
			else if (time_until_input == 0 && (tick_input.left_mouse_press || tick_input.right_mouse_press || tick_input.middle_mouse_press || tick_input.r_press))
            {
                Vec3 neg_z_basis = {0, 0, -1};
            	RaycastHit raycast_output = raycastHitCube(camera.coords, vec3RotateByQuaternion(neg_z_basis, camera.rotation), RAYCAST_SEEK_LENGTH);

                if (tick_input.left_mouse_press && raycast_output.hit) 
                {
                    Entity *entity_pointer = getEntityPointer(raycast_output.hit_coords);
                    if (entity_pointer != 0)
                    {
                        entity_pointer->coords = (Int3){0};
                        entity_pointer->position_norm = (Vec3){0};
                        entity_pointer->id = -1;
                    }
                    setTileType(NONE, raycast_output.hit_coords);
                }
                else if (tick_input.right_mouse_press && raycast_output.hit) 
                {
                    Entity *group_pointer = 0;
                    if (isSource(editor_state.picked_tile)) 
                    {
                        setEntityInstanceInGroup(next_world_state.sources, raycast_output.place_coords, NORTH, getEntityColor(raycast_output.place_coords));
                    }
                    else 
                    {
                        switch (editor_state.picked_tile)
                        {
                            case BOX:     group_pointer = next_world_state.boxes;    break;
                            case MIRROR:  group_pointer = next_world_state.mirrors;  break;
                            case CRYSTAL: group_pointer = next_world_state.crystals; break;
                            default: group_pointer = 0;
                        }
                        if (group_pointer != 0) setEntityInstanceInGroup(group_pointer, raycast_output.place_coords, NORTH, NO_COLOR);
                    }
                    setTileType(editor_state.picked_tile, raycast_output.place_coords); 
                }
                else if (tick_input.r_press && raycast_output.hit)
                {   
                    Direction direction = getTileDirection(raycast_output.hit_coords);
                    if (direction == DOWN) direction = NORTH;
                    else direction++;
                	next_world_state.buffer[coordsToBufferIndexDirection(raycast_output.hit_coords)] = direction;
                    Entity *entity_pointer = getEntityPointer(raycast_output.hit_coords);
                    if (entity_pointer != 0)
                    {
                        entity_pointer->direction = direction;
                    	entity_pointer->rotation_quat = directionToQuaternion(direction, false);
                	}
                }
                else if (tick_input.middle_mouse_press && raycast_output.hit) editor_state.picked_tile = getTileType(raycast_output.hit_coords);

                time_until_input = INPUT_TIME_UNTIL_ALLOW;
            }
			else if (time_until_input == 0 && tick_input.l_press)
            {
				editor_state.picked_tile++;
                if      (editor_state.picked_tile == PLAYER) editor_state.picked_tile = MIRROR;
                else if (editor_state.picked_tile == CRYSTAL + 1) editor_state.picked_tile = SOURCE_RED;
                else if (editor_state.picked_tile == SOURCE_WHITE + 1) editor_state.picked_tile = VOID;
                time_until_input = INPUT_TIME_UNTIL_ALLOW;
            }
        }

        // world logic
        // falling objects should go here

        // do animations
        
		for (int animation_index = 0; animation_index < MAX_ANIMATION_COUNT; animation_index++)
        {
			if (animations[animation_index].frames_left == 0) continue;
			if (animations[animation_index].position_to_change != 0) *animations[animation_index].position_to_change = animations[animation_index].position[animations[animation_index].frames_left-1];
			if (animations[animation_index].rotation_to_change != 0) *animations[animation_index].rotation_to_change = animations[animation_index].rotation[animations[animation_index].frames_left-1];
            animations[animation_index].frames_left--;
        }

        // finished updating state
        world_state = next_world_state;

        // calculate where lasers are into ephemeral laser buffer // TODO(spike): figure out if i should allocate this memory before and just zero it, or if i should allocate it on the stack per frame.
        LaserBuffer laser_buffer[1024] = {0};
        int32 laser_tile_count = 0;
        int32 total_source_count = getEntityCount(next_world_state.sources);
        Entity sources_as_primary[128]; 
        memset(sources_as_primary, -1, sizeof(sources_as_primary)); // TODO(spike): better zeroing function to be used here also
        memcpy(sources_as_primary, next_world_state.sources, sizeof(Entity) * total_source_count);

        for (int source_index = 0; source_index < MAX_PSEUDO_SOURCE_COUNT; source_index++)
        {
            Entity* entity_pointer = &sources_as_primary[source_index];
            if (entity_pointer->id == -1) continue;
            Direction current_direction = entity_pointer->direction;
            Int3 current_coords = getNextCoords(entity_pointer->coords, current_direction);

            switch (entity_pointer->color) 
            {
                case MAGENTA: addPrimarySource(&sources_as_primary[total_source_count], entity_pointer, &total_source_count, BLUE);  break;
                case YELLOW:  addPrimarySource(&sources_as_primary[total_source_count], entity_pointer, &total_source_count, GREEN); break;
                case CYAN:    addPrimarySource(&sources_as_primary[total_source_count], entity_pointer, &total_source_count, BLUE); break;
                case WHITE:
              	{
					addPrimarySource(&sources_as_primary[total_source_count], entity_pointer, &total_source_count, GREEN); 
                    addPrimarySource(&sources_as_primary[total_source_count], entity_pointer, &total_source_count, BLUE); // creates a duplicate ID here. still unclear if i even want to use these though
                    break;
              	}
                default: break;
            }

            for (int laser_index = 0; laser_index < MAX_LASER_TRAVEL_DISTANCE; laser_index++)
            {
                LaserColor laser_color = colorToLaserColor(entity_pointer->color);
                if (!intCoordsWithinLevelBounds(current_coords)) break;
                if (getTileType(current_coords) == PLAYER)
                {
                    // TODO(spike): think about ordering here. right now there is 1f delay: abilities only calculated after checking if allowed to push
                    break;
                }
                else if (getTileType(current_coords) == CRYSTAL)
                {
                    if (!isParallelToXZ(current_direction)) break; // let crystal break beam if not coming at angle flat on the y axis

					if (laser_color.red) current_direction = getRedDirectionAtCrystal(current_direction); 
					else if (laser_color.green) current_direction = current_direction;
					else if (laser_color.blue) current_direction = getBlueDirectionAtCrystal(current_direction); 
                }
                else if (getTileType(current_coords) == MIRROR)
                {
                    bool can_reflect = canMirrorReflect(current_direction, getEntityDirection(current_coords));
					if (can_reflect) 
                    {
                        current_direction = getNextLaserDirectionMirror(current_direction, getEntityDirection(current_coords));
                    }
					else break;
                }
                else if (getTileType(current_coords) != NONE) break;

				if      (laser_color.red)   laser_buffer[laser_tile_count].color.red   = true; 
				else if (laser_color.green) laser_buffer[laser_tile_count].color.green = true; // else here ensures magenta -> red, yellow -> red, cyan -> green for non-primaries.
				else if (laser_color.blue)  laser_buffer[laser_tile_count].color.blue  = true; // the rest are aleady in sources_as_primary, and will be added to laser_buffer at the end of the loop
                laser_buffer[laser_tile_count].direction = current_direction;
                laser_buffer[laser_tile_count].coords    = current_coords;
                laser_tile_count++;

                current_coords = getNextCoords(current_coords, current_direction);
            }
        }

        // draw lasers
        for (int laser_index = 0; laser_index < laser_tile_count; laser_index++)
        {
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

        // draw static objects
        for (int tile_index = 0; tile_index < 2 * level_dim.x*level_dim.y*level_dim.z; tile_index += 2)
        {
			TileType tile = world_state.buffer[tile_index];
			if (tile == PLAYER || tile == BOX || tile == MIRROR || isSource(tile) || tile == CRYSTAL) continue;
			if (tile != NONE)   drawAsset(getPath(tile), CUBE_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToQuaternion(next_world_state.buffer[tile_index + 1], false));
        }

        // draw non-colored entities
        drawEntityLoop(world_state.boxes,    box_path,     CUBE_3D, DEFAULT_SCALE);
        drawEntityLoop(world_state.mirrors,  mirror_path,  CUBE_3D, DEFAULT_SCALE);
        drawEntityLoop(world_state.crystals, crystal_path, CUBE_3D, DEFAULT_SCALE);
        drawAsset(player_path, CUBE_3D, world_state.player.position_norm, PLAYER_SCALE, world_state.player.rotation_quat);

		// draw colored entites
		for (int source_index = 0; source_index < MAX_ENTITY_INSTANCE_COUNT; source_index++)
        {
            char* path = getPath(getTileType(world_state.sources[source_index].coords));
            drawAsset(path, CUBE_3D, world_state.sources[source_index].position_norm, DEFAULT_SCALE, world_state.sources[source_index].rotation_quat);
        }

        if (editor_state.editor_mode && tick_input.i_press) writeBufferToFile(level_path);

		if (time_until_input != 0) time_until_input--;

        rendererSubmitFrame(assets_to_load, camera);
        memset(assets_to_load, 0, sizeof(assets_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
