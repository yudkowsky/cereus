#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> // TODO(spike): also temporary, for floor
#include <stdio.h> // TODO(spike): "temporary", for fopen 
#include <windows.h> // TODO(spike): ok, actually temporary this time, for outputdebugstring

#define local_persist static
#define global_variable static
#define internal static

double PHYSICS_INCREMENT = 1.0/60.0;
NormalizedCoords NORM_DISTANCE_PER_SCREEN_PIXEL = { 2.0f / 1920.0f, 2.0f / 1080.0f };
IntCoords DEFAULT_SCALE = {8, 8};
float CAMERA_MOVEMENT_SPEED = 0.02f;
float CAMERA_CLIPPING_RADIUS = 1.0f;
int32 UNDO_BUFFER_SIZE = 256;
int32 TILE_SIZE_PIXELS = 16;

WorldState current_world_state = {0};

WorldState undo_buffer[256] = {0};
int32 undo_buffer_position = 0;
int32 z_time_until_allowed = 0;

double accumulator = 0.0;

char* void_path = "data/sprites/void.png";
IntCoords void_dim_int = { 16, 16 };
NormalizedCoords void_dim_norm = {0};

char* grid_path = "data/sprites/grid.png";
IntCoords grid_dim_int = { 16, 16 };
NormalizedCoords grid_dim_norm = {0};

char* wall_path = "data/sprites/wall.png";
IntCoords wall_dim_int = { 16, 16 };
NormalizedCoords wall_dim_norm = {0};

char* box_path = "data/sprites/box.png";
IntCoords box_dim_int = { 16, 16 };
NormalizedCoords box_dim_norm = {0};

char* pack_path = "data/sprites/pack.png";
IntCoords pack_dim_int = { 16, 16 };
NormalizedCoords pack_dim_norm = {0};

char* player_path = "data/sprites/player.png";
IntCoords player_dim_int = { 16, 16 };
NormalizedCoords player_dim_norm = {0};

typedef enum 
{
    TILE_VOID = 0,
    TILE_GRID = 1,
    TILE_WALL = 2,
    TILE_BOX= 3,
    TILE_PLAYER = 5
}
TileType;

TextureToLoad textures_to_load[128] = {0};
char* loaded_textures[128] = {0};

// input is amount of pixels in game space, output fits in [-1, 1]
float xPixelsToNorm(float pixels)
{
    return pixels * NORM_DISTANCE_PER_SCREEN_PIXEL.x * DEFAULT_SCALE.x;
}
float yPixelsToNorm(float pixels)
{
    return pixels * NORM_DISTANCE_PER_SCREEN_PIXEL.y * DEFAULT_SCALE.y;
}
NormalizedCoords pixelsToNorm(IntCoords coords, IntCoords scale)
{
    NormalizedCoords normalized_coords = {coords.x * NORM_DISTANCE_PER_SCREEN_PIXEL.x * scale.x, coords.y * NORM_DISTANCE_PER_SCREEN_PIXEL.y * scale.y };
    return normalized_coords;
}

// adjusts a very small amount, to lie perfectly in the middle of a pixel (minimizes floating point error)
NormalizedCoords nearestPixelFloor(NormalizedCoords coords_input, IntCoords scale)
{
	NormalizedCoords corrected_coords = {0};
    corrected_coords.x = floorf(coords_input.x / (NORM_DISTANCE_PER_SCREEN_PIXEL.x * scale.x) + 0.5f) * NORM_DISTANCE_PER_SCREEN_PIXEL.x * scale.x;
    corrected_coords.y = floorf(coords_input.y / (NORM_DISTANCE_PER_SCREEN_PIXEL.y * scale.y) + 0.5f) * NORM_DISTANCE_PER_SCREEN_PIXEL.y * scale.y;
    return corrected_coords;
}

NormalizedCoords nearestPixelFloorToNorm(IntCoords coords, IntCoords scale)
{
	return nearestPixelFloor(pixelsToNorm(coords, scale), scale);
}

bool normCoordsIsEqual(NormalizedCoords coords_1, NormalizedCoords coords_2)
{
    float dx = coords_1.x - coords_2.x;
    float dy = coords_1.y - coords_2.y;
    return (fabs(dx) < xPixelsToNorm(0.05f) && fabs(dy) < yPixelsToNorm(0.05f));
}

// modifies textures_to_load, expects origin and dimensions in norm space. scale input is pixels/pixel, output is norm space scale 
void drawSprite(char* texture_path, NormalizedCoords origin, NormalizedCoords dimensions)
{
    int32 texture_location = -1;
    for (uint32 texture_index = 0; texture_index < 128; texture_index++)
    {
        if (loaded_textures[texture_index] == texture_path)
        {
            // already loaded
            texture_location = texture_index;
            break;
        }
        if (textures_to_load[texture_index].path == 0)
        {
            // end of list - queue the path to load, 
            loaded_textures[texture_index] = texture_path;
            texture_location = texture_index;
            break;
        }
    }
	// adjust origin based on camera location
	origin.x -= current_world_state.camera_coords.x;
	origin.y -= current_world_state.camera_coords.y;

	// don't pass to textures_to_load if entire tetxure is outside of norm space
    if (origin.x > CAMERA_CLIPPING_RADIUS ||
        origin.y > CAMERA_CLIPPING_RADIUS ||
        origin.x + dimensions.x < -CAMERA_CLIPPING_RADIUS ||
        origin.y + dimensions.y < -CAMERA_CLIPPING_RADIUS)
    {
        return;
    }

    textures_to_load[texture_location].path = texture_path;

    // use instance count to go to next free slot here
	textures_to_load[texture_location].origin[textures_to_load[texture_location].instance_count] = origin;
    textures_to_load[texture_location].dimensions[textures_to_load[texture_location].instance_count] = dimensions;
    textures_to_load[texture_location].instance_count++;
}

bool checkCollision(NormalizedCoords object_1_origin, NormalizedCoords object_1_dimension, NormalizedCoords object_2_origin, NormalizedCoords object_2_dimension)
{
    object_1_origin    = nearestPixelFloor(object_1_origin,    DEFAULT_SCALE);
    object_1_dimension = nearestPixelFloor(object_1_dimension, DEFAULT_SCALE);
    object_2_origin    = nearestPixelFloor(object_2_origin,    DEFAULT_SCALE);
    object_2_dimension = nearestPixelFloor(object_2_dimension, DEFAULT_SCALE);

	// somewhat temporary fix
	object_1_dimension.x -= xPixelsToNorm(0.05f);
	object_1_dimension.y -= yPixelsToNorm(0.05f);
	object_2_dimension.x -= xPixelsToNorm(0.05f);
	object_2_dimension.y -= yPixelsToNorm(0.05f);

    if (object_1_origin.x < object_2_origin.x + object_2_dimension.x && object_2_origin.x < object_1_origin.x + object_1_dimension.x &&
        object_1_origin.y < object_2_origin.y + object_2_dimension.y && object_2_origin.y < object_1_origin.y + object_1_dimension.y)
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool boxPush(NormalizedCoords collision_point, Direction direction)
{
    bool boxes_to_move_bools[64] = {false};
    int32 first_box_id = -1;

    // populate 
    for (int box_id = 0; box_id < current_world_state.box_count; box_id++)
    {
        if (current_world_state.boxes[box_id].id == -1) continue;
        if (checkCollision(collision_point, player_dim_norm, current_world_state.boxes[box_id].origin, box_dim_norm))
        {
            first_box_id = box_id;
            boxes_to_move_bools[box_id] = true;
            break;
        }
    }

    if (first_box_id != -1)	
    {
        // main loop with a growing boxes_to_move_ids array
        for (int pass_nr = 0; pass_nr < 64; pass_nr++)
        {
            bool new_box_added_this_pass = false;
            for (int boxes_to_move_index = 0; boxes_to_move_index < current_world_state.box_count; boxes_to_move_index++)
            {
                if (boxes_to_move_bools[boxes_to_move_index] == false || current_world_state.boxes[boxes_to_move_index].id == -1) continue;
                NormalizedCoords box_position_after_move = {0};
                switch (direction)
                {
                    case NORTH:
                        box_position_after_move = (NormalizedCoords){ current_world_state.boxes[boxes_to_move_index].origin.x, 
                        											  current_world_state.boxes[boxes_to_move_index].origin.y + yPixelsToNorm((float)TILE_SIZE_PIXELS)};
                        break;
                    case WEST:
                        box_position_after_move = (NormalizedCoords){ current_world_state.boxes[boxes_to_move_index].origin.x - xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                        											  current_world_state.boxes[boxes_to_move_index].origin.y };
                        break;
                    case SOUTH:
                        box_position_after_move = (NormalizedCoords){ current_world_state.boxes[boxes_to_move_index].origin.x, 
                        											  current_world_state.boxes[boxes_to_move_index].origin.y - yPixelsToNorm((float)TILE_SIZE_PIXELS)};
                        break;
                    case EAST:
                        box_position_after_move = (NormalizedCoords){ current_world_state.boxes[boxes_to_move_index].origin.x + xPixelsToNorm((float)TILE_SIZE_PIXELS),
                        											  current_world_state.boxes[boxes_to_move_index].origin.y };
                        break;
                }

				for (int wall_id = 0; wall_id < current_world_state.wall_count; wall_id++)
                {
                    if (checkCollision(current_world_state.walls[wall_id].origin, wall_dim_norm, box_position_after_move, box_dim_norm))
                    {
                        // collision occurs due to box movement
                        current_world_state.time_until_input_allowed = 0;
                        return false;
                    }
                }

                for (int box_id = 0; box_id < current_world_state.box_count; box_id++)
                {
                    if (current_world_state.boxes[box_id].id == -1) continue;
                    if (boxes_to_move_bools[box_id] == true) continue; // already loaded
                    if (checkCollision(current_world_state.boxes[box_id].origin, box_dim_norm, box_position_after_move, box_dim_norm))
                    {
						boxes_to_move_bools[box_id] = true;
						new_box_added_this_pass = true;
                    }
                }
            }
            if (!new_box_added_this_pass) break;
        }
        
        // modify global state: move boxes in boxes_to_move_ids
        for (int boxes_to_move_index = 0; boxes_to_move_index < current_world_state.box_count; boxes_to_move_index++)
        {
            if (boxes_to_move_bools[boxes_to_move_index] == false) continue;
            switch (direction)
            {
                case NORTH:
                    current_world_state.boxes[boxes_to_move_index].origin.y += yPixelsToNorm((float)TILE_SIZE_PIXELS);
                    break;
                case WEST:
                    current_world_state.boxes[boxes_to_move_index].origin.x -= xPixelsToNorm((float)TILE_SIZE_PIXELS);
                    break;
                case SOUTH:
                    current_world_state.boxes[boxes_to_move_index].origin.y -= yPixelsToNorm((float)TILE_SIZE_PIXELS);
                    break;
                case EAST:
                    current_world_state.boxes[boxes_to_move_index].origin.x += xPixelsToNorm((float)TILE_SIZE_PIXELS);
                    break;
            }
        }
    }
    return true;
}

NormalizedCoords tileIndexToNormCoords(int16 tile_index, IntCoords level_dimensions)
{
	IntCoords position_int = { 16 * (tile_index % level_dimensions.x), (level_dimensions.y - tile_index / level_dimensions.x) * 16 };
    return nearestPixelFloorToNorm(position_int, DEFAULT_SCALE);
}

void recordStateForUndo()
{
    undo_buffer[undo_buffer_position] = current_world_state;
    undo_buffer_position = (undo_buffer_position + 1) % UNDO_BUFFER_SIZE;
}

NormalizedCoords getPackCoords(NormalizedCoords player_coords, Direction player_facing)
{
    NormalizedCoords pack_coords = player_coords;
    switch (player_facing)
    {
        case NORTH: 
            pack_coords.y = player_coords.y -= yPixelsToNorm(16);
            break;
        case WEST:  
            pack_coords.x = player_coords.x += xPixelsToNorm(16);
            break;
        case SOUTH: 
            pack_coords.y = player_coords.y += yPixelsToNorm(16);
			break;
        case EAST:  
            pack_coords.x = player_coords.x -= xPixelsToNorm(16);
            break;
    }
    return pack_coords;
}

int32 spikeModInt(int32 dividend, int32 modulus)
{
    int32 residue = dividend % modulus;
    if (residue < 0) residue += modulus;
    return residue;
}

void handleInput(Direction input_direction, NormalizedCoords *next_player_coords, Direction *next_player_direction, bool *moved_this_frame)
{
	// input is allowed (already checked for time)
    
	// check direction player is facing. if opposite, do nothing. if same, add to the direction and move. if one off, turn.
    switch (spikeModInt(input_direction - current_world_state.player_direction, 4))
    {
    	case 0:
        {
			// move in direction pressed
			switch (input_direction)
            {
                case (NORTH):
                    next_player_coords->y += yPixelsToNorm(16);
                    break;
                case (WEST):
                    next_player_coords->x -= xPixelsToNorm(16);
                    break;
                case (SOUTH):
                    next_player_coords->y -= yPixelsToNorm(16);
                    break;
                case (EAST):
                    next_player_coords->x += xPixelsToNorm(16);
                    break;
            }
            *moved_this_frame = true;
            break;
        }
        case 1:
        {
            // turn ACW
			NormalizedCoords diagonal_potential_box_position = {0}, orthogonal_potential_box_position = {0};
    		Direction 		 diagonal_push_direction = 0,           orthogonal_push_direction = 0;
			switch (input_direction)
            {
                case (NORTH):
                    diagonal_potential_box_position = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                        												  next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS) };
                    diagonal_push_direction = SOUTH;

                    orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x,
																			next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS)};
                    orthogonal_push_direction = EAST;
                    break;
                case (WEST):
                    diagonal_potential_box_position = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                        												  next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS) };
                    diagonal_push_direction = EAST;

                    orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS),
																			next_player_coords->y };
                    orthogonal_push_direction = NORTH;
                    break;
                case (SOUTH):
                    diagonal_potential_box_position = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS), 
																		  next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS) };
                    diagonal_push_direction = NORTH;

                    orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x,
																			next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS)};
                    orthogonal_push_direction = WEST;
                    break;
                case (EAST):
                    diagonal_potential_box_position = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                        												  next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS) };
                    diagonal_push_direction = WEST;

                    orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS),
																			next_player_coords->y };
                    orthogonal_push_direction = SOUTH;
                    break;
            }
			// diagonal push happens even if orthogonal fails...

			bool diagonal_push = boxPush(diagonal_potential_box_position, diagonal_push_direction);
            bool orthogonal_push = boxPush(orthogonal_potential_box_position, orthogonal_push_direction);

            if (diagonal_push && orthogonal_push)
            {
                *next_player_direction = input_direction;
                *moved_this_frame = true;
            }
            break;
        }
        case 2:
        {
            // input_direction is opposite to player_direction, do nothing
            break;
        }
        case 3:
        {
            // turn CW
			NormalizedCoords diagonal_potential_box_position = {0}, orthogonal_potential_box_position = {0};
    		Direction 		 diagonal_push_direction = 0,           orthogonal_push_direction = 0;
			switch (input_direction)
            {
                case (NORTH):
                    diagonal_potential_box_position = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                        												  next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS) };
                    diagonal_push_direction = SOUTH;

                    orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x, 
																			next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS)};
                    orthogonal_push_direction = WEST;
                    break;
                case (WEST):
                    diagonal_potential_box_position = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                        												  next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS) };
                    diagonal_push_direction = EAST;

                    orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS),
																			next_player_coords->y }; 
                    orthogonal_push_direction = SOUTH;
                    break;
                case (SOUTH):
                    diagonal_potential_box_position = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                        												  next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS) };
                    diagonal_push_direction = NORTH;

                    orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x, 
																			next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS)};
                    orthogonal_push_direction = EAST;
                    break;
                case (EAST):
                    diagonal_potential_box_position = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                        												  next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS) };
                    diagonal_push_direction = WEST;

                    orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS),
																			next_player_coords->y };
                    orthogonal_push_direction = NORTH;
                    break;
            }
			// diagonal push happens even if orthogonal fails...

			bool diagonal_push = boxPush(diagonal_potential_box_position, diagonal_push_direction);
            bool orthogonal_push = boxPush(orthogonal_potential_box_position, orthogonal_push_direction);

            if (diagonal_push && orthogonal_push)
            {
                *next_player_direction = input_direction;
                *moved_this_frame = true;
            }
            break;
        }
    }

	current_world_state.time_until_input_allowed = 8;
}

void gameInitialise(void) 
{	
    void_dim_norm   = nearestPixelFloorToNorm(void_dim_int, DEFAULT_SCALE);
    grid_dim_norm   = nearestPixelFloorToNorm(grid_dim_int, DEFAULT_SCALE);
    wall_dim_norm   = nearestPixelFloorToNorm(wall_dim_int, DEFAULT_SCALE);
    box_dim_norm    = nearestPixelFloorToNorm(box_dim_int,  DEFAULT_SCALE);
    pack_dim_norm   = nearestPixelFloorToNorm(pack_dim_int, DEFAULT_SCALE);
    player_dim_norm = nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE);

	current_world_state.level_path = "data/levels/boxes.txt";
    current_world_state.player_direction = NORTH;
	current_world_state.camera_coords = (NormalizedCoords){ 0.7f, 1.2f };
    current_world_state.time_until_input_allowed = 0;

    // need to fill various current_world_state structs based on level
    FILE *level_file = fopen(current_world_state.level_path, "rb");
    unsigned char byte = 0;

	IntCoords level_dimensions = {0};
    fread(&byte, 1, 1, level_file);
    level_dimensions.x = byte;
    fread(&byte, 1, 1, level_file);
    level_dimensions.y = byte;

    for (int16 tile_index = 0; tile_index < level_dimensions.x * level_dimensions.y; tile_index++)
    {
        fread(&byte, 1, 1, level_file);
		switch (byte)
        {
        	case TILE_VOID:
                current_world_state.voids[current_world_state.void_count].origin = tileIndexToNormCoords(tile_index, level_dimensions);
                current_world_state.voids[current_world_state.void_count].id = current_world_state.void_count;
                current_world_state.void_count++;
                break;
            case TILE_GRID:
                current_world_state.grids[current_world_state.grid_count].origin = tileIndexToNormCoords(tile_index, level_dimensions);
                current_world_state.grids[current_world_state.grid_count].id = current_world_state.grid_count;
                current_world_state.grid_count++;
            	break;
            case TILE_WALL:
                current_world_state.walls[current_world_state.wall_count].origin = tileIndexToNormCoords(tile_index, level_dimensions);
                current_world_state.walls[current_world_state.wall_count].id = current_world_state.wall_count;
                current_world_state.wall_count++;
            	break;
            case TILE_BOX:
                current_world_state.boxes[current_world_state.box_count].origin = tileIndexToNormCoords(tile_index, level_dimensions);
                current_world_state.boxes[current_world_state.box_count].id = current_world_state.box_count;
                current_world_state.box_count++;

                current_world_state.grids[current_world_state.grid_count].origin = tileIndexToNormCoords(tile_index, level_dimensions);
                current_world_state.grids[current_world_state.grid_count].id = current_world_state.grid_count;
                current_world_state.grid_count++;
            	break;
            case TILE_PLAYER:
                current_world_state.player_coords = tileIndexToNormCoords(tile_index, level_dimensions);

                current_world_state.grids[current_world_state.grid_count].origin = tileIndexToNormCoords(tile_index, level_dimensions);
                current_world_state.grids[current_world_state.grid_count].id = current_world_state.grid_count;
                current_world_state.grid_count++;
            	break;
            default:
            	break;
        }
    }
    current_world_state.player_spawn_point = current_world_state.player_coords; 
}

void gameFrame(double delta_time, TickInput tick_input)
{	
   	// clamp for stalls or breakpoints
	if (delta_time > 0.1) delta_time = 0.1;

	accumulator += delta_time;

    while (accumulator >= PHYSICS_INCREMENT)
    {
		// HANDLE UNDO PRESS

    	if (tick_input.z_press && z_time_until_allowed == 0)
        {
            z_time_until_allowed = 8;

            int32 next_undo_buffer_position;
            if (undo_buffer_position != 0) next_undo_buffer_position = undo_buffer_position - 1;
            else next_undo_buffer_position = UNDO_BUFFER_SIZE - 1;

            if (undo_buffer[next_undo_buffer_position].level_path != 0)
            {
            	current_world_state = undo_buffer[next_undo_buffer_position]; 
                memset(&undo_buffer[undo_buffer_position], 0, sizeof(WorldState));
            	undo_buffer_position = next_undo_buffer_position;
            }
            // else: no more undos in buffer (either back to start point, or run out of undo buffer space)
        }

        if (tick_input.i_press) current_world_state.camera_coords.y += CAMERA_MOVEMENT_SPEED;
        if (tick_input.j_press) current_world_state.camera_coords.x -= CAMERA_MOVEMENT_SPEED;
        if (tick_input.k_press) current_world_state.camera_coords.y -= CAMERA_MOVEMENT_SPEED;
        if (tick_input.l_press) current_world_state.camera_coords.x += CAMERA_MOVEMENT_SPEED;

        // MOVEMENT SYSTEM

		NormalizedCoords next_player_coords = current_world_state.player_coords; 
        Direction next_player_direction = current_world_state.player_direction;

        bool moved_this_frame = false;
        
        Direction input_direction = NORTH;
		if (tick_input.w_press && current_world_state.time_until_input_allowed == 0) handleInput(input_direction = NORTH, &next_player_coords, &next_player_direction, &moved_this_frame);
		if (tick_input.a_press && current_world_state.time_until_input_allowed == 0) handleInput(input_direction = WEST,  &next_player_coords, &next_player_direction, &moved_this_frame);
		if (tick_input.s_press && current_world_state.time_until_input_allowed == 0) handleInput(input_direction = SOUTH, &next_player_coords, &next_player_direction, &moved_this_frame);
		if (tick_input.d_press && current_world_state.time_until_input_allowed == 0) handleInput(input_direction = EAST,  &next_player_coords, &next_player_direction, &moved_this_frame);

        if (z_time_until_allowed != 0) z_time_until_allowed--;
		if (current_world_state.time_until_input_allowed != 0) current_world_state.time_until_input_allowed--;

        // PLAYER WALL COLLISION DETECTION

        // handle x wall collision
		NormalizedCoords test_x_wall_collision = { next_player_coords.x, current_world_state.player_coords.y };
        bool x_collision = false;

        for (int i = 0; i < current_world_state.wall_count; i++)
        {
            if (checkCollision(current_world_state.walls[i].origin, wall_dim_norm, test_x_wall_collision, player_dim_norm))
            {
                x_collision = true;
                // collision on x axis occurs; check from which direction
                if (next_player_coords.x - current_world_state.player_coords.x > 0)
                {
                    // trying to move right
                    next_player_coords.x = current_world_state.walls[i].origin.x - player_dim_norm.x;
                    moved_this_frame = false;
                    break;
                }
                else
                {
                    // trying to move left
                    next_player_coords.x = current_world_state.walls[i].origin.x + wall_dim_norm.x;
                    moved_this_frame = false;
                    break;
                }
            }
        }

        // handle y wall collision
        NormalizedCoords test_y_wall_collision = { current_world_state.player_coords.x, next_player_coords.y };
        bool y_collision = false;

        for (int i = 0; i < current_world_state.wall_count; i++)
        {
            if (checkCollision(current_world_state.walls[i].origin, wall_dim_norm, test_y_wall_collision, player_dim_norm))
            {
                y_collision = true;
                // work out from above or below
                if (next_player_coords.y - current_world_state.player_coords.y > 0)
                {
                    // trying to move up
                    next_player_coords.y = current_world_state.walls[i].origin.y - player_dim_norm.y;
                    moved_this_frame = false;
                    break;
                }
                else
                {
                    // trying to move down
                    next_player_coords.y = current_world_state.walls[i].origin.y + wall_dim_norm.y;
                    moved_this_frame = false;
                    break;
                }
            }
        }

		// DRAW SPRITES

        // draw voids; and check if equal to player or some box
		bool player_in_void = true;

        for (int16 void_index = 0; void_index < current_world_state.void_count; void_index++)
        {
			drawSprite(void_path, current_world_state.voids[void_index].origin, void_dim_norm);
        }

        // draw grids, and void player
        for (int16 grid_index = 0; grid_index < current_world_state.grid_count; grid_index++)
        {
			drawSprite(grid_path, current_world_state.grids[grid_index].origin, grid_dim_norm);
            if (checkCollision(current_world_state.grids[grid_index].origin, grid_dim_norm, next_player_coords, player_dim_norm)) player_in_void = false;
        }

        // draw walls
        for (int16 wall_index = 0; wall_index < current_world_state.wall_count; wall_index++)
        {
			drawSprite(wall_path, current_world_state.walls[wall_index].origin, wall_dim_norm);
        }

        // draw boxes, and void some
        for (int16 box_index = 0; box_index < current_world_state.box_count; box_index++)
        {
            if (current_world_state.boxes[box_index].id == -1) continue; // if voided
            bool delete_box = true;
            for (int16 grid_index = 0; grid_index < current_world_state.grid_count; grid_index++)
            {
				if (checkCollision(current_world_state.grids[grid_index].origin, grid_dim_norm, current_world_state.boxes[box_index].origin, box_dim_norm)) 
                {
                    delete_box = false;
                	break;
                }
            }

            if (delete_box) 
            {
                current_world_state.boxes[box_index].id = -1;
                continue;
            }
			drawSprite(box_path, current_world_state.boxes[box_index].origin, box_dim_norm);
        }

		// draw player
        if (player_in_void) 
        {
            current_world_state.player_coords = current_world_state.player_spawn_point;
            current_world_state.player_direction = NORTH;
        }
        else
        {
            if (moved_this_frame)
            {
                recordStateForUndo();
                current_world_state.player_coords = next_player_coords;
                current_world_state.player_direction = next_player_direction;
            }
            drawSprite(player_path, nearestPixelFloor(current_world_state.player_coords, DEFAULT_SCALE), nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE));
        }

        // draw pack behind player
        drawSprite(pack_path, getPackCoords(current_world_state.player_coords, current_world_state.player_direction), nearestPixelFloorToNorm(pack_dim_int, DEFAULT_SCALE));

        rendererSubmitFrame(textures_to_load);
        memset(textures_to_load, 0, sizeof(textures_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
