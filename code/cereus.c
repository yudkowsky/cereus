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

typedef struct WorldState
{
    // maybe wrap player up into an entity later, if we want to handle animation rotation and player direction separately, for example.
	Int3 player_coords;
    Direction player_direction;
	
    int8 buffer[4096]; // same format as file - 1 byte information per tile
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

double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0.0;

float TAU = 6.28318530f;

float SENSITIVITY = 0.005f;
float MOVE_STEP = 0.05f;
Vec3 DEFAULT_SCALE = { 1.0f, 1.0f, 1.0f };
Vec3 PLAYER_SCALE = { 0.75f, 0.75f, 0.75f };
float RAYCAST_SEEK_LENGTH = 20.0f;
int32 META_INPUT_TIME_ALLOW = 8;

Camera camera = {0};
Int3 AXIS_X = { 1, 0, 0 };
Int3 AXIS_Y = { 0, 1, 0 };
Int3 AXIS_Z = { 0, 0, 1 };
float camera_yaw = 0.0f;
float camera_pitch = 0.0f;

char* level_path = "w:/cereus/data/levels/level_1.txt"; // absolute path required to modify original file
Int3 level_dim = {0};

WorldState world_state = {0};
WorldState next_world_state = {0};
EditorState editor_state = {0};

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

Vec3 vec3Negative(Vec3 coords) {
    return (Vec3){ -coords.x, -coords.y, -coords.z }; }

Int3 int3Negative(Int3 coords) {
    return (Int3){ -coords.x, -coords.y, -coords.z }; }

Vec3 vec3Add(Vec3 a, Vec3 b) {
	return (Vec3){ a.x+b.x, a.y+b.y, a.z+b.z }; }

Int3 int3Add(Int3 a, Int3 b) {	
	return (Int3){ a.x+b.x, a.y+b.y, a.z+b.z }; }

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

TileType getTileAtCoords(Int3 coords) {
    return next_world_state.buffer[coordsToBufferIndex(coords)]; }

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
void drawAsset(char* path, AssetType type, Vec3 coords, Vec3 scale, Direction direction)
{
    // for breakpoint
    if (path == player_path)
    {
        int foo = 0;
        (void)foo;
    }

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

void gameInitialise(void) 
{	
    loadFileToBuffer(level_path);

	// loop though and find where player was placed
    for (int buffer_index = 0; buffer_index < level_dim.x*level_dim.y*level_dim.z; buffer_index++)
    {
    	if (next_world_state.buffer[buffer_index] == PLAYER) 
        {
            next_world_state.player_coords = bufferIndexToCoords(buffer_index);
            break;
        }
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
                time_until_input = META_INPUT_TIME_ALLOW;
            }
            if (time_until_input == 0 && (tick_input.w_press || tick_input.a_press || tick_input.s_press || tick_input.d_press))
            {
				//Int3 next_player_coords = next_world_state.player_coords;
                Direction input_direction = 0;
                Int3 next_player_coords = {0};
                Direction next_player_direction = 0;
                if 		(tick_input.w_press) input_direction = NORTH; 
                else if (tick_input.a_press) input_direction = WEST; 
                else if (tick_input.s_press) input_direction = SOUTH; 
                else if (tick_input.d_press) input_direction = EAST; 

                if (input_direction == next_world_state.player_direction || true)
                {
               		// already facing this direction; attempt to move 
                    if 		(tick_input.w_press) next_player_coords = int3Add(next_world_state.player_coords, int3Negative(AXIS_Z));
                    else if (tick_input.a_press) next_player_coords = int3Add(next_world_state.player_coords, int3Negative(AXIS_X));
                    else if (tick_input.s_press) next_player_coords = int3Add(next_world_state.player_coords, AXIS_Z);
                    else if (tick_input.d_press) next_player_coords = int3Add(next_world_state.player_coords, AXIS_X);
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
                        default:
                        {
                            // if movement goes through, delete old buffer position and write new one
                            setTileAtCoords(NONE, next_world_state.player_coords);
                            next_world_state.player_coords = next_player_coords;
                            setTileAtCoords(PLAYER, next_world_state.player_coords);
                        }
                    }
                }
                else
                {
                    // attempt to turn. just turn for now
                    
        		}


                time_until_input = 8;
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
                time_until_input = META_INPUT_TIME_ALLOW;
            }

			if (time_until_input == 0 && tick_input.j_press)
            {
                if (normCoordsWithinLevelBounds(camera.coords))
                {
					setTileAtCoords(editor_state.picked_tile, normCoordsToInt(camera.coords));
                    time_until_input = META_INPUT_TIME_ALLOW;
                }
            }

            // inputs that require raycast
			if (time_until_input == 0 && (tick_input.left_mouse_press || tick_input.right_mouse_press || tick_input.middle_mouse_press))
            {
                Vec3 neg_z_basis = {0, 0, -1};
                RaycastHit raycast_output = raycastHitCube(camera.coords, vec3RotateByQuaternion(neg_z_basis, camera.rotation), RAYCAST_SEEK_LENGTH);
                if (tick_input.left_mouse_press) setTileAtCoords(NONE, raycast_output.hit_coords);
                else if (tick_input.right_mouse_press) setTileAtCoords(editor_state.picked_tile, raycast_output.place_coords);
                else if (tick_input.middle_mouse_press) editor_state.picked_tile = getTileAtCoords(raycast_output.hit_coords); 
                time_until_input = META_INPUT_TIME_ALLOW;
            }
            if (time_until_input == 0 && tick_input.l_press)
            {
                editor_state.picked_tile++;
                if (editor_state.picked_tile == 6) editor_state.picked_tile = VOID;
                time_until_input = META_INPUT_TIME_ALLOW;
            }
        }

        // finished updating state
        world_state = next_world_state;

        // draw cubes
        for (int32 tile_index = 0; tile_index < level_dim.x*level_dim.y*level_dim.z; tile_index++)
        {
			TileType tile = world_state.buffer[tile_index];
			if (tile == 5) drawAsset(player_path, CUBE_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), PLAYER_SCALE, world_state.player_direction);
			else if (tile != NONE) drawAsset(getPath(tile), CUBE_3D, intCoordsToNorm(bufferIndexToCoords(tile_index)), DEFAULT_SCALE, NORTH);
        }

		if (time_until_input != 0) time_until_input--;

        // don't want to corrupt file 
        // if (editor_state.editor_mode && tick_input.i_press) writeBufferToFile(level_path);

        rendererSubmitFrame(assets_to_load, camera);
        memset(assets_to_load, 0, sizeof(assets_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
