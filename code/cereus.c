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
    NONE = 0,
    VOID = 1,
    GRID = 2,
    WALL = 3,
    BOX = 4,
    PLAYER = 5,
}
TileType;

typedef struct Entity
{
    Int3 coords;
    Vec3 position_norm;
    Direction direction;
    Vec4 rotation_quat;
    int32 id;
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
    int8 buffer[4096]; // same format as file - 1 byte information per tile
    Entity player;
    Entity boxes[32];
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
    Entity* pointer_to_entity[32];
    int32 count;
}
Push;

double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0.0;

float TAU = 6.28318530f;

float SENSITIVITY = 0.005f;
float MOVE_STEP = 0.05f;
Vec3 DEFAULT_SCALE = { 1.0f, 1.0f, 1.0f };
Vec3 PLAYER_SCALE = { 0.75f, 0.75f, 0.75f };
float RAYCAST_SEEK_LENGTH = 20.0f;
int32 INPUT_TIME_UNTIL_ALLOW = 8;
int32 MAX_ENTITY_INSTANCE_COUNT = 32; // TODO(spike): use this everywhere rather than 32
int32 MAX_ENTITY_PUSH_COUNT = 32;

// TODO(spike): doesn't really fit as a vec3 but set this when working on animations
Vec3 IDENTITY_TRANSLATION = { 0, 0, 0 };
Int3 AXIS_X = { 1, 0, 0 };
Int3 AXIS_Y = { 0, 1, 0 };
Int3 AXIS_Z = { 0, 0, 1 };
Vec4 IDENTITY_QUATERNION = { 0, 0, 0, 1};

Camera camera = {0};
float camera_yaw = 0.0f;
float camera_pitch = 0.0f;

char* level_path = "w:/cereus/data/levels/level_1.txt"; // absolute path required to modify original file
Int3 level_dim = {0};

WorldState world_state = {0};
WorldState next_world_state = {0};
EditorState editor_state = {0};
Animation animations[32];

int32 time_until_input = 0;

char* void_path   = "data/sprites/void.png";
char* grid_path   = "data/sprites/grid.png";
char* wall_path   = "data/sprites/wall.png";
char* box_path    = "data/sprites/box.png";
char* player_path = "data/sprites/player.png";

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

Vec4 directionToQuaternion(Direction direction)
{
    float yaw = 0.0f;
    switch (direction)
    {
        case NORTH: yaw = 0.0f; 		  break;
        case WEST:  yaw = 0.25f * TAU;    break;
        case SOUTH: yaw = 0.5f * TAU;     break;
        case EAST:  yaw = -0.25f   * TAU; break;
    }
    Vec3 axis = {0, 1, 0};
    return quaternionFromAxisAngle(axis, yaw);
}

// VARIOUS BASIC HELPER FUNCTIONS

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

Int3 int3Negative(Int3 coords) {
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

// BUFFER INTERFACING

int32 coordsToBufferIndex(Int3 coords)
{
    return level_dim.x*level_dim.z*coords.y + level_dim.x*coords.z + coords.x; 
}

Int3 bufferIndexToCoords(int32 buffer_index)
{
	Int3 coords = {0};
    coords.x = buffer_index % level_dim.x;
    coords.y = buffer_index / (level_dim.x * level_dim.z);
	coords.z = (buffer_index / level_dim.x) % level_dim.z;
    return coords;
}

void setTileAtCoords(TileType type, Int3 coords) {
    next_world_state.buffer[coordsToBufferIndex(coords)] = type; }

// TODO(spike): simplify
TileType getTileAtCoords(Int3 coords) {
    TileType tile = next_world_state.buffer[coordsToBufferIndex(coords)];
    return tile;
}

int32 getEntityCount(Entity *pointer_to_array)
{
    int32 count = 0;
    // loop over entity array
    for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
	{
		if (pointer_to_array[entity_index].id == -1) continue;
        count++;
    }
    return count;
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

    uint8 buffer[4096];
	fread(&buffer, 1, level_dim.x*level_dim.y*level_dim.z, file);
	fclose(file);
    memcpy(next_world_state.buffer, buffer, level_dim.x*level_dim.y*level_dim.z);
}

void writeBufferToFile(char* path)
{
    FILE *file = fopen(path, "rb+");
    fseek(file, 4, SEEK_CUR);
	fwrite(world_state.buffer, 1, 4096, file);
    fclose(file);
}

// DRAW ASSET

char* getPath(TileType tile)
{
    switch(tile)
    {
        case NONE:   return 0;
        case VOID:   return void_path;
        case GRID:   return grid_path;
        case WALL:   return wall_path;
        case BOX:    return box_path;
        case PLAYER: return player_path;
        default: return 0;
    }
}

// takes integer coords and converts to Vec3 before passing to assets_to_load
// scale = 1
// rotation from ROTATION enum to quaternion
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

// RAYCAST THING FOR EDITOR PLACE/DESTROY

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

        TileType tile = getTileAtCoords(current_cube);
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

// ANIMATIONS

void createInterpolationAnimation(Vec3 position_a, Vec3 position_b, Vec3* position_to_change, Vec4 rotation_a, Vec4 rotation_b, Vec4* rotation_to_change)
{
    // find next free in Animations
    int32 animation_index = -1;
    for (int find_anim_index = 0; find_anim_index < 32; find_anim_index++)
    {
        if (animations[find_anim_index].frames_left == 0)
        {
            animation_index = find_anim_index;
            animations[animation_index] = (Animation){0};
            break;
        }
    }

    // set frame count. the last frame here is the true/correct position
    animations[animation_index].frames_left = 8;

    // get difference between coords
	Vec3 translation_per_frame = (vec3ScalarMultiply      (vec3Subtract      (position_b, position_a), (float)(1.0f/animations[animation_index].frames_left)));
    Vec4 rotation_per_frame    = (quaternionScalarMultiply(quaternionSubtract(rotation_a, rotation_b), (float)(1.0f/animations[animation_index].frames_left)));

    if (!vec3IsZero(translation_per_frame))
    {
        animations[animation_index].position_to_change = position_to_change;
        for (int frame_index = 0; frame_index < animations[animation_index].frames_left; frame_index++)
        {
            animations[animation_index].position[animations[animation_index].frames_left-(1+frame_index)] 
            = vec3Add(*position_to_change, vec3ScalarMultiply(translation_per_frame, (float)(1+frame_index)));
        }
    }
    if(!quaternionIsZero(rotation_per_frame))
    {
        animations[animation_index].rotation_to_change = rotation_to_change;
        
        if (quaternionDot(rotation_a, rotation_b) < 0.0f) rotation_b = quaternionScalarMultiply(rotation_b, -1.0f);
        for (int frame_index = 0; frame_index < animations[animation_index].frames_left; frame_index++)
        {
            float t = (float)(frame_index + 1) / animations[animation_index].frames_left;
	    	animations[animation_index].rotation[animations[animation_index].frames_left-(1+frame_index)] 
            = quaternionNormalize(quaternionAdd(quaternionScalarMultiply(rotation_a, 1.0f - t), quaternionScalarMultiply(rotation_b, t)));
        }
    }
}

// PUSH ENTITES

Int3 getNextCoords(Int3 coords, Direction direction)
{
	switch (direction)
    {
        case NORTH: return int3Add(coords, int3Negative(AXIS_Z)); // rename negate
        case WEST:  return int3Add(coords, int3Negative(AXIS_X));
        case SOUTH: return int3Add(coords, AXIS_Z);
        case EAST:  return int3Add(coords, AXIS_X);
    }
    return (Int3){0};
}

Entity* getEntityPointer(Int3 coords)
{
	TileType type = getTileAtCoords(coords);
    Entity *group_pointer = 0;
    switch (type)
    {
        case PLAYER: return &next_world_state.player; // special case for player
        case BOX: group_pointer = next_world_state.boxes;
        //case ENTITY: group_pointer = next_world_state.entity; <- in general
        for (int entity_index = 0; entity_index < MAX_ENTITY_INSTANCE_COUNT; entity_index++)
        {
            if (int3IsEqual(group_pointer[entity_index].coords, coords)) return &group_pointer[entity_index];
        }
        default: return (Entity*){0};
    }
}

bool canPush(Int3 coords, Direction direction)
{
    Int3 current_coords = coords;
    TileType current_tile;
    for (int push_index = 0; push_index < MAX_ENTITY_PUSH_COUNT; push_index++) // could theoretically be more entities to push here
    {
    	current_coords = getNextCoords(current_coords, direction);
        current_tile = getTileAtCoords(current_coords);
        if (!intCoordsWithinLevelBounds(current_coords)) return false;
        if (current_tile == WALL) return false;
        if (current_tile == NONE) return true;
    }
    return false;
}

Push push(Int3 coords, Direction direction)
{
	// fill Push with previous coords per entity, next coords per entity, pointer to that entity - and amount of entities to push 
    // will use to call an animation, and for that i need:
    // pos1, pos2, and adress of entity
    // to get address: switch on type of entity, loop through that type of entity for the one with that location.
    // maybe don't switch case? just get array of those entities as a pointer, and loop through the array at that pointer to find coords, for all pointers there?
    Push entities_to_push = {0}; // MAX_ENTITY_PUSH_COUNT
	Int3 current_coords = coords;
    for (int push_index = 0; push_index < MAX_ENTITY_PUSH_COUNT; push_index++)
    {
		entities_to_push.previous_coords[push_index] = current_coords;
        entities_to_push.pointer_to_entity[push_index] = getEntityPointer(current_coords);
        current_coords = getNextCoords(current_coords, direction);
        entities_to_push.new_coords[push_index] = current_coords; 
        entities_to_push.count++;
        if (getTileAtCoords(current_coords) == NONE) break;
    }
    return entities_to_push;
}

void gameInitialise(void) 
{	
    loadFileToBuffer(level_path);

    memset(next_world_state.boxes, -1, sizeof(next_world_state.boxes)); // TODO(spike): function to zero WorldState in a better way
    Entity *pointer = 0;
    for (int buffer_index = 0; buffer_index < level_dim.x*level_dim.y*level_dim.z; buffer_index++)
    {
        if (next_world_state.buffer[buffer_index] == BOX) pointer = next_world_state.boxes;
        if (pointer != 0)
        {
            int32 count = getEntityCount(pointer);
			pointer[count].coords = bufferIndexToCoords(buffer_index);
            pointer[count].position_norm = intCoordsToNorm(pointer[count].coords);
            pointer[count].direction = NORTH;
            pointer[count].rotation_quat = directionToQuaternion(pointer[count].direction);
            pointer[count].id = getEntityCount(pointer);
            pointer = 0;
        }
        else if (next_world_state.buffer[buffer_index] == PLAYER) // special case for player, since there is only one
        {
            next_world_state.player.coords = bufferIndexToCoords(buffer_index);
            next_world_state.player.position_norm = intCoordsToNorm(next_world_state.player.coords);
            next_world_state.player.direction = NORTH;
            next_world_state.player.rotation_quat = directionToQuaternion(next_world_state.player.direction);
            next_world_state.player.id = 0;
        }

        /*
		if (next_world_state.buffer[buffer_index] == PLAYER) 
        {
            next_world_state.player.coords = bufferIndexToCoords(buffer_index);
            next_world_state.player.position_norm = intCoordsToNorm(next_world_state.player.coords);
            next_world_state.player.direction = NORTH;
            next_world_state.player.rotation_quat = directionToQuaternion(NORTH); 
        }
		else if (next_world_state.buffer[buffer_index] == BOX)
        {
            next_world_state.boxes[box_count].coords = bufferIndexToCoords(buffer_index);
            next_world_state.boxes[box_count].position_norm = intCoordsToNorm(next_world_state.boxes[box_count].coords);
            next_world_state.boxes[box_count].direction = NORTH;
            next_world_state.boxes[box_count].rotation_quat = directionToQuaternion(NORTH);
            next_world_state.boxes[box_count].id = box_count;
            box_count++;
        }
        */
    }

	camera.coords = (Vec3){3, 8, 15};
    camera_yaw = 0; // towards -z; north
    camera_pitch = -TAU * 0.25f; // look straight down
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
                    if 		(tick_input.w_press) next_player_coords = int3Add(next_world_state.player.coords, int3Negative(AXIS_Z));
                    else if (tick_input.a_press) next_player_coords = int3Add(next_world_state.player.coords, int3Negative(AXIS_X));
                    else if (tick_input.s_press) next_player_coords = int3Add(next_world_state.player.coords, AXIS_Z);
                    else if (tick_input.d_press) next_player_coords = int3Add(next_world_state.player.coords, AXIS_X);
                    TileType next_tile = getTileAtCoords(next_player_coords);
                    switch (next_tile)
                    {
                        case VOID:
                        case GRID:
                        case WALL:
                        {
                            // no movement - do nothing
							break;
                        }
                        case BOX:
                        {
                            bool can_push = canPush(next_player_coords, input_direction);
                            if (can_push) 
                            {
                                Push entities_to_push = push(next_player_coords, input_direction);
                                for (int entity_index = 0; entity_index < entities_to_push.count; entity_index++)
                                {
                                    createInterpolationAnimation(intCoordsToNorm(entities_to_push.previous_coords[entity_index]),
                                            					 intCoordsToNorm(entities_to_push.new_coords[entity_index]),
            													 &entities_to_push.pointer_to_entity[entity_index]->position_norm,
                                                                 IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0);
                                    if (entity_index == 0) setTileAtCoords(NONE, entities_to_push.pointer_to_entity[entity_index]->coords);
									entities_to_push.pointer_to_entity[entity_index]->coords = entities_to_push.new_coords[entity_index];
                                    setTileAtCoords(BOX, entities_to_push.new_coords[entity_index]);
                                }

                                // player
                                createInterpolationAnimation(intCoordsToNorm(next_world_state.player.coords), 
                                        					 intCoordsToNorm(next_player_coords), 
                                                			 &next_world_state.player.position_norm,
                                                             IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0);
                                setTileAtCoords(NONE, next_world_state.player.coords);
                                next_world_state.player.coords = next_player_coords;
                                setTileAtCoords(PLAYER, next_player_coords);
                            }
                            break;
                        }
                        default:
                        {
                            // check if would walk off ledge
                            if (getTileAtCoords(int3Add(next_player_coords, int3Negative(AXIS_Y))) != NONE)
                            {
                                createInterpolationAnimation(intCoordsToNorm(next_world_state.player.coords), 
                                        					 intCoordsToNorm(next_player_coords), 
                                                			 &next_world_state.player.position_norm,
                                                             IDENTITY_QUATERNION, IDENTITY_QUATERNION, 0);
                                setTileAtCoords(NONE, next_world_state.player.coords);
                                next_world_state.player.coords = next_player_coords;
                                setTileAtCoords(PLAYER, next_world_state.player.coords);
                                break;
                            }
                        }
                    }
                }
                else
                {
                   	bool opposite = (abs(input_direction - next_world_state.player.direction) == 2);
                    if (!opposite) 
                    {
                        createInterpolationAnimation(IDENTITY_TRANSLATION, IDENTITY_TRANSLATION, 0, 
                                					 directionToQuaternion(next_world_state.player.direction), 
                                                     directionToQuaternion(input_direction), 
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

            // TODO(spike): set this up with the box update thing
            /*
			if (time_until_input == 0 && tick_input.j_press)
            {
                if (normCoordsWithinLevelBounds(camera.coords))
                {
					setTileAtCoords(editor_state.picked_tile, normCoordsToInt(camera.coords));
                    time_until_input = INPUT_TIME_UNTIL_ALLOW;
                }
            }
            */

            // inputs that require raycast
			if (time_until_input == 0 && (tick_input.j_press || tick_input.k_press || tick_input.z_press))
            {
                Vec3 neg_z_basis = {0, 0, -1};
                RaycastHit raycast_output = raycastHitCube(camera.coords, vec3RotateByQuaternion(neg_z_basis, camera.rotation), RAYCAST_SEEK_LENGTH);
                if (tick_input.j_press) 
                {
                    // TODO(spike): could use pointer here to update state
                    if (getTileAtCoords(raycast_output.hit_coords) == BOX)
                    {
						for (int box_index = 0; box_index < 32; box_index++)
                        {
                            if (!int3IsEqual(next_world_state.boxes[box_index].coords, raycast_output.hit_coords)) continue;
                            next_world_state.boxes[box_index].coords = (Int3){0};
                            next_world_state.boxes[box_index].position_norm = (Vec3){0};
                            next_world_state.boxes[box_index].id = -1;
                            break;
                        }
                    }
                    setTileAtCoords(NONE, raycast_output.hit_coords);
                }
                else if (tick_input.k_press) 
                {
                    // TODO(spike): could also use pointer here
                    if (editor_state.picked_tile == BOX)
                    {	
                        for (int box_index = 0; box_index < 32; box_index++)
                        {
                            if (next_world_state.boxes[box_index].id != -1) continue;
                            next_world_state.boxes[box_index].coords = raycast_output.place_coords;
                            next_world_state.boxes[box_index].position_norm = intCoordsToNorm(raycast_output.place_coords);
                            next_world_state.boxes[box_index].direction = NORTH;
                            next_world_state.boxes[box_index].rotation_quat = directionToQuaternion(NORTH);
                            next_world_state.boxes[box_index].id = box_index;
                            break;
                        }
                    }
                    setTileAtCoords(editor_state.picked_tile, raycast_output.place_coords); // TODO(spike): next: debug this, i think it's not writing to file properly.
                }
                else if (tick_input.z_press) editor_state.picked_tile = getTileAtCoords(raycast_output.hit_coords); 
                time_until_input = INPUT_TIME_UNTIL_ALLOW;
            }
            if (time_until_input == 0 && tick_input.l_press)
            {
                editor_state.picked_tile++;
                if (editor_state.picked_tile == 6) editor_state.picked_tile = VOID;
                time_until_input = INPUT_TIME_UNTIL_ALLOW;
            }
        }

        // do animations
        
		for (int animation_index = 0; animation_index < 32; animation_index++)
        {
			if (animations[animation_index].frames_left == 0) continue;
			if (animations[animation_index].position_to_change != 0) *animations[animation_index].position_to_change = animations[animation_index].position[animations[animation_index].frames_left-1];
			if (animations[animation_index].rotation_to_change != 0) *animations[animation_index].rotation_to_change = animations[animation_index].rotation[animations[animation_index].frames_left-1];
            animations[animation_index].frames_left--;
        }

        // finished updating state
        world_state = next_world_state;

        // draw cubes
        for (int32 tile_index = 0; tile_index < level_dim.x*level_dim.y*level_dim.z; tile_index++)
        {
			TileType tile = world_state.buffer[tile_index];

            // entities rendered differently because i want to render them based on norm_coords. so we loop here
			if (tile == PLAYER) drawAsset(player_path, CUBE_3D, world_state.player.position_norm, PLAYER_SCALE, world_state.player.rotation_quat);
			else if (tile == BOX) // doesn't matter what order we render boxes in, all rendered the same. so just loop over boxes array and render
            {
                for (int box_index = 0; box_index < 32; box_index++)
                {
                    if (world_state.boxes[box_index].id == -1) continue;
                    drawAsset(box_path, CUBE_3D, world_state.boxes[box_index].position_norm, DEFAULT_SCALE, world_state.boxes[box_index].rotation_quat);
                }
            }

            // if no animations just render normally
			else if (tile != NONE) drawAsset(getPath(tile), CUBE_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, directionToQuaternion(NORTH));
        }

		if (time_until_input != 0) time_until_input--;

        if (editor_state.editor_mode && tick_input.i_press) writeBufferToFile(level_path);

        rendererSubmitFrame(assets_to_load, camera);
        memset(assets_to_load, 0, sizeof(assets_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
