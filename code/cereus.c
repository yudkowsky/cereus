#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"

#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> // TODO(spike): also temporary, for sin/cos
#include <stdio.h> // TODO(spike): "temporary", for fopen 
// #include <windows.h> // TODO(spike): ok, actually temporary this time, for outputdebugstring

#define local_persist static
#define global_variable static
#define internal static

/*
typedef struct Entity
{
    Int3 coords_int;
    Vec3 coords_norm;
    Direction direciton_int;
    Vec4 rotation_quat;
	int32 id;
}
Entity;
*/

typedef enum EntityType
{
    NONE = 0,
    VOID = 1,
    GRID = 2,
    WALL = 3,
	BOX = 4,
    PLAYER = 5,
}
EntityType;

typedef struct WorldState
{
	Int3 player_coords;
	
    int8 tile_buffer[4096]; // same format as file - 1 byte information per tile

    // implicit coords of voids, grids, walls... entities where i don't care about indivitual ids or rotation
    int32 voids[1024];
    int32 grids[1024];
    int32 walls[1024];

    // intformation on entites where i care about individuality, rotating them, giving them basic animation
    //Entity boxes[1024];
    //Entity player;
}

WorldState;

typedef struct EditorState
{
    bool editor_mode;
    EntityType picked_entity;
}
EditorState;

typedef struct RaycastHit
{
    bool hit;
}
RaycastHit;

double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0.0;

float TAU = 6.28318530f;

Camera camera = { {0, 0, 4}, {0, 0, 0, 1} };
float camera_yaw = 0.0f;
float camera_pitch = 0.0f;

float SENSITIVITY = 0.005f;
float MOVE_STEP = 0.05f;
Vec3 DEFAULT_SCALE = { 1.0f, 1.0f, 1.0f };
float RAYCAST_SEEK_LENGTH = 20.0f;

WorldState world_state = {0};
WorldState next_world_state = {0};
EditorState editor_state = {0};
char* level_path = "w:/cereus/data/levels/level_1.txt"; // absolute path required to modify original file
Int3 level_dim = {0};
int32 time_until_meta_input = 0;

char* loaded_texture_paths[256] = {0};
AssetToLoad assets_to_load[256] = {0};
int32 assent_to_load_count = 0;

char* grid_path   = "data/sprites/grid.png";
char* wall_path   = "data/sprites/wall.png";
char* box_path    = "data/sprites/box.png";
char* player_path = "data/sprites/player.png";

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

Vec4 quaternionScalarMultiply(Vec4 quaternion, float scalar)
{
    return (Vec4){ quaternion.x*scalar, quaternion.y*scalar, quaternion.z*scalar, quaternion.w*scalar };
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
        case NORTH: yaw = 0.0f; 		break;
        case WEST:  yaw = 0.25f  * TAU; break;
        case SOUTH: yaw = -0.25f * TAU; break;
        case EAST:  yaw = 0.5f   * TAU; break;
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

bool intCoordsIsEqual(Int3 a, Int3 b) {
    return (a.x == b.x && a.y == b.y && a.z == b.z); }

// ENTITY HANDLING

void createEntity(EntityType type, Int3 coords, Direction direction)
{
    switch (type)
    {
        case VOID:
        {
            next_world_state.voids[next_world_state.void_count].coords = coords;
            next_world_state.voids[next_world_state.void_count].direction = direction;
            next_world_state.voids[next_world_state.void_count].id = next_world_state.void_count;
            next_world_state.void_count++;
            break;
        }
        case GRID: 
        {
            next_world_state.grids[next_world_state.grid_count].coords = coords; 
            next_world_state.grids[next_world_state.grid_count].direction = direction;
            next_world_state.grids[next_world_state.grid_count].id = next_world_state.grid_count;
            next_world_state.grid_count++;
            break;
        }
        case WALL:
        {
            next_world_state.walls[next_world_state.wall_count].coords = coords;
            next_world_state.walls[next_world_state.wall_count].direction = direction;
            next_world_state.walls[next_world_state.wall_count].id = next_world_state.wall_count;
            next_world_state.wall_count++;
            break;
        }
        case BOX:
        {
            next_world_state.boxes[next_world_state.box_count].coords = coords;
            next_world_state.boxes[next_world_state.box_count].direction = direction;
            next_world_state.boxes[next_world_state.box_count].id = next_world_state.box_count;
            next_world_state.box_count++;
            break;
        }
        default:
        {
            break;
        }
    }
}

// TODO(spike): don't loop over all things for one block - instead read file at that location.
// probably don't actually read file, so set up some sort of file buffer to be loaded in (also means less writes to file)
// TODO(spike): fix system with pointer here. two functions, one for type?
Entity getEntityInfo(Int3 coords, EntityType *type)
{
    Entity entity_info = {0};
	for (int void_index = 0; void_index < next_world_state.void_count; void_index++)
    {
		if (intCoordsIsEqual(next_world_state.voids[void_index].coords, coords))
        {
            if (next_world_state.voids[void_index].id == -1) continue;
            entity_info.coords = next_world_state.voids[void_index].coords;
            entity_info.direction = next_world_state.voids[void_index].direction;
            entity_info.id = next_world_state.voids[void_index].id;
            *type = VOID;
            return entity_info;
        }
    }
	for (int grid_index = 0; grid_index < next_world_state.grid_count; grid_index++)
    {
		if (intCoordsIsEqual(next_world_state.grids[grid_index].coords, coords)) 
        {
            if (next_world_state.walls[grid_index].id == -1) continue;
            entity_info.coords = next_world_state.grids[grid_index].coords;
            entity_info.direction = next_world_state.grids[grid_index].direction;
            entity_info.id = next_world_state.grids[grid_index].id;
            *type = GRID;
            return entity_info;
        }
    }
	for (int wall_index = 0; wall_index < next_world_state.wall_count; wall_index++)
    {
		if (intCoordsIsEqual(next_world_state.walls[wall_index].coords, coords)) 
        {
            if (next_world_state.walls[wall_index].id == -1) continue;
            entity_info.coords = next_world_state.walls[wall_index].coords;
            entity_info.direction = next_world_state.walls[wall_index].direction;
            entity_info.id = next_world_state.walls[wall_index].id;
            *type = WALL;
            return entity_info;
        }
    }
	for (int box_index = 0; box_index < next_world_state.box_count; box_index++)
    {
		if (intCoordsIsEqual(next_world_state.boxes[box_index].coords, coords)) 
        {
            if (next_world_state.boxes[box_index].id == -1) continue;
            entity_info.coords = next_world_state.boxes[box_index].coords;
            entity_info.direction = next_world_state.boxes[box_index].direction;
            entity_info.id = next_world_state.boxes[box_index].id;
            *type = BOX;
            return entity_info;
        }
    }
    return entity_info;
}

void voidEntityAtId(EntityType type, int32 id)
{
    switch(type)
    {
        case VOID:
        {
            next_world_state.voids[id].id = -1;
            return;
        }
        case GRID:
        {
            next_world_state.grids[id].id = -1;
            return;
        }
        case WALL:
        {
            next_world_state.walls[id].id = -1;
            return;
        }
        case BOX:
        {
            next_world_state.boxes[id].id = -1;
            return;
        }
        default:
        {
            return;
        }
    }
}

// FILE I/O

Int3 byteIndexToCoords(int32 byte_index)
{
	Int3 coords = {0};
    coords.x = byte_index % level_dim.x;
    coords.y = byte_index / (level_dim.x * level_dim.z);
	coords.z = (byte_index / level_dim.x) % level_dim.z;
    return coords;
}

void writeAssetToLevel(char* path, EntityType type, Int3 coords)
{
	// get index based on level_dim
    int32 byte_index = level_dim.x*level_dim.z*coords.y + level_dim.x*coords.z + coords.x;
    FILE *file = fopen(path, "rb+");
    fseek(file, byte_index + 4, SEEK_SET);
    fputc(type, file);
    fclose(file);
}

void loadFileAsLevel(char* path)
{
    next_world_state = (WorldState){0};

	int32 level_size_bytes = level_dim.x*level_dim.y*level_dim.z;
	FILE *file = fopen(path, "rb");
    unsigned char byte = 0;
    fseek(file, 4, SEEK_SET);

    for (int32 byte_index = 0; byte_index < level_size_bytes; byte_index++)
    {
		fread(&byte, 1, 1, file);

        if (byte != 5) createEntity(byte, byteIndexToCoords(byte_index), NORTH);
        else next_world_state.player_coords = byteIndexToCoords(byte_index);
    }
}

// DRAW ASSET

// takes integer coords and converts to Vec3 before passing to assets_to_load
// scale = 1
// rotation from ROTATION enum to quaternion
// assuming one path -> one asset type.
void drawAsset(char* path, AssetType type, Int3 coords, Direction direction)
{
    // check loaded_assets. if this char, continue at this index. if == 0, put path and type, and continue with this index.
	int32 asset_location = -1;
    for (int32 asset_index = 0; asset_index < 1024; asset_index++)
    {
        if (loaded_texture_paths[asset_index] == path)
        {
            asset_location = asset_index;
            break;
        }
        if (loaded_texture_paths[asset_index] == 0)
        {
            loaded_texture_paths[asset_index] = path;
            asset_location = asset_index;
            break;
        }
    }
    assets_to_load[asset_location].path = path;
    assets_to_load[asset_location].type = type;

    switch (type)
    {
        case SPRITE_2D:
        {
            return;
        }
        case CUBE_3D:
        {
            assets_to_load[asset_location].coords[assets_to_load[asset_location].instance_count]   = intCoordsToNorm(coords);
            assets_to_load[asset_location].scale[assets_to_load[asset_location].instance_count]    = DEFAULT_SCALE;
            assets_to_load[asset_location].rotation[assets_to_load[asset_location].instance_count] = directionToQuaternion(direction);
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

    EntityType current_cube_type;
    Entity current_cube_info;

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

        current_cube_info = getEntityInfo(current_cube, &current_cube_type);
        if (current_cube_type != NONE && current_cube_info.id != -1)
        {
            output.hit = true;
            output.hit_entity_info = current_cube_info;
            output.hit_entity_type = current_cube_type;
            output.place_coords = previous_cube;
            return output;
        }
    }
    return output;
}

void gameInitialise(void) 
{	
    // get level dimensions
    FILE *file = fopen(level_path, "rb");
	unsigned char byte = 0;
    fseek(file, 1, SEEK_CUR); // skip the first byte
	fread(&byte, 1, 1, file);
    level_dim.x = byte;
    fread(&byte, 1, 1, file);
    level_dim.y = byte;
    fread(&byte, 1, 1, file);
    level_dim.z = byte;
    fclose(file);

    // writeAssetToLevel(level_path, WALL, test_coords, level_dim);

    loadFileAsLevel(level_path);

    world_state = next_world_state;
}

void gameFrame(double delta_time, TickInput tick_input)
{	
	if (delta_time > 0.1) delta_time = 0.1;
	accumulator += delta_time;

    camera_yaw   += tick_input.mouse_dx * SENSITIVITY;
    if (camera_yaw >  3.14159265f) camera_yaw -= 2.0f*3.14159265f;
    if (camera_yaw < -3.14159265f) camera_yaw += 2.0f*3.14159265f;

    camera_pitch += tick_input.mouse_dy * SENSITIVITY;
    float pitch_limit = 1.553343f;
    if (camera_pitch >  pitch_limit) camera_pitch =  pitch_limit; 
    if (camera_pitch < -pitch_limit) camera_pitch = -pitch_limit; 

    Vec3 default_quaternion_yaw   = { 0, 1, 0 };
    Vec3 default_quaternion_pitch = { 1, 0, 0 };
    Vec4 quaternion_yaw   = quaternionFromAxisAngle(default_quaternion_yaw,   camera_yaw);
    Vec4 quaternion_pitch = quaternionFromAxisAngle(default_quaternion_pitch, camera_pitch);
    camera.rotation  = quaternionNormalize(quaternionMultiply(quaternion_yaw, quaternion_pitch));

    while (accumulator >= PHYSICS_INCREMENT)
    {
		next_world_state = world_state;

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
        if (tick_input.space_press) camera.coords.y += 0.05f;
        if (tick_input.shift_press) camera.coords.y -= 0.05f;

        if (!editor_state.editor_mode)
        {
			if (time_until_meta_input == 0 && tick_input.e_press) 
            {
                editor_state.editor_mode = true;
                time_until_meta_input = 8;
            }
        }
        else
        {
			if (time_until_meta_input == 0 && tick_input.e_press) 
            {
                editor_state.editor_mode = false;
                time_until_meta_input = 8;
            }

			if (time_until_meta_input == 0 && tick_input.j_press)
            {
                Int3 int_editor_position = normCoordsToInt(camera.coords);
                if (intCoordsWithinLevelBounds(int_editor_position))
                {
                    createEntity(WALL, int_editor_position, NORTH);
                    writeAssetToLevel(level_path, editor_state.picked_entity, int_editor_position);
                    time_until_meta_input = 8;
                }
            }

            // inputs that require raycast
			if (time_until_meta_input == 0 && (tick_input.left_mouse_press || tick_input.right_mouse_press || tick_input.middle_mouse_press))
            {
                Vec3 neg_z_basis = {0, 0, -1};
                RaycastHit raycast_output = raycastHitCube(camera.coords, vec3RotateByQuaternion(neg_z_basis, camera.rotation), RAYCAST_SEEK_LENGTH);

                if (tick_input.left_mouse_press)   
                {
                    voidEntityAtId(raycast_output.hit_entity_type, raycast_output.hit_entity_info.id);
                    writeAssetToLevel(level_path, NONE, raycast_output.hit_entity_info.coords);
                }
                else if (tick_input.right_mouse_press)  
                {
                    createEntity(editor_state.picked_entity, raycast_output.place_coords, NORTH);
                    writeAssetToLevel(level_path, editor_state.picked_entity, raycast_output.place_coords);
                }
                else if (tick_input.middle_mouse_press) 
                {
                    editor_state.picked_entity = raycast_output.hit_entity_type; 
                }
                time_until_meta_input = 8;
            }
        }

        // finished updating state
        world_state = next_world_state;

        for (int wall_index = 0; wall_index < world_state.wall_count; wall_index++)
        {
            if (world_state.walls[wall_index].id == -1) continue;
            drawAsset(wall_path, CUBE_3D, world_state.walls[wall_index].coords, NORTH);
        }

		if (time_until_meta_input != 0) time_until_meta_input--;

        rendererSubmitFrame(assets_to_load, camera);
        memset(assets_to_load, 0, sizeof(assets_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
