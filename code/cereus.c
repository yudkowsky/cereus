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
NormalizedCoords NORM_DISTANCE_PER_TILE = { (2.0f / 1920.0f) * 16.0f, (2.0f / 1080.0f) * 16.0f};
IntCoords DEFAULT_SCALE = {8, 8};
float CAMERA_MOVEMENT_SPEED = 0.02f;
float CAMERA_CLIPPING_RADIUS = 1.0f;
int32 UNDO_BUFFER_SIZE = 256;
int32 TILE_SIZE_PIXELS = 16;
NormalizedCoords CURSOR_DIMS = {0.01f, 0.01f};

WorldState current_world_state = {0};
IntCoords level_dimensions_int = {0};

typedef enum 
{
    TILE_NULL = -1,
    TILE_VOID = 0,
    TILE_GRID = 1,
    TILE_WALL = 2,
    TILE_BOX= 3,
    TILE_PLAYER = 5
}
TileType;

typedef struct EdtiorState
{
	bool picking;
    TileType picked_tile;
	char* picked_tile_path;
}
EditorState;
EditorState editor_state = {0};

NormalizedCoords camera_coords = {0};
WorldState undo_buffer[256] = {0};
int32 undo_buffer_position = 0;
int32 time_until_meta_input_allowed = 0;
bool level_editor_mode = false;

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

TextureToLoad textures_to_load[256] = {0};
char* loaded_textures[256] = {0};

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

NormalizedCoords nearestTileFloor(NormalizedCoords coords_input, IntCoords scale)
{
    NormalizedCoords corrected_coords = {0};
    corrected_coords.x = floorf(coords_input.x / (NORM_DISTANCE_PER_TILE.x * scale.x)) * NORM_DISTANCE_PER_TILE.x * scale.x;
    corrected_coords.y = floorf(coords_input.y / (NORM_DISTANCE_PER_TILE.y * scale.y)) * NORM_DISTANCE_PER_TILE.y * scale.y;
    return corrected_coords;
}

// modifies textures_to_load, expects origin and dimensions in norm space. scale input is pixels/pixel, output is norm space scale 
void drawSprite(char* texture_path, NormalizedCoords origin, NormalizedCoords dimensions, bool adjust_for_camera)
{
    int32 texture_location = -1;
    for (uint32 texture_index = 0; texture_index < 256; texture_index++)
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
	if(adjust_for_camera)
    {
        origin.x -= camera_coords.x;
        origin.y -= camera_coords.y;

        if (origin.x > CAMERA_CLIPPING_RADIUS ||
            origin.y > CAMERA_CLIPPING_RADIUS ||
            origin.x + dimensions.x < -CAMERA_CLIPPING_RADIUS ||
            origin.y + dimensions.y < -CAMERA_CLIPPING_RADIUS)
        {
            return;
        }
    }
    textures_to_load[texture_location].path = texture_path;
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

bool canBoxPush(NormalizedCoords collision_point, Direction direction, bool boxes_to_move[256])
{
    int32 first_box_id = -1;
    for (int box_id = 0; box_id < current_world_state.box_count; box_id++)
    {
        if (current_world_state.boxes[box_id].id == -1) continue;
        if (checkCollision(collision_point, player_dim_norm, current_world_state.boxes[box_id].origin, box_dim_norm))
        {
            first_box_id = box_id;
            boxes_to_move[box_id] = true;
            break;
        }
    }

    if (first_box_id != -1)	
    {
        // main loop with a growing boxes_to_move_ids array
        for (int pass_nr = 0; pass_nr < 256; pass_nr++)
        {
            bool new_box_added_this_pass = false;
            for (int boxes_to_move_index = 0; boxes_to_move_index < current_world_state.box_count; boxes_to_move_index++)
            {
                if (boxes_to_move[boxes_to_move_index] == false || current_world_state.boxes[boxes_to_move_index].id == -1) continue;
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
                    if (boxes_to_move[box_id] == true) continue; // already loaded
                    if (checkCollision(current_world_state.boxes[box_id].origin, box_dim_norm, box_position_after_move, box_dim_norm))
                    {
						boxes_to_move[box_id] = true;
						new_box_added_this_pass = true;
                    }
                }
            }
            if (!new_box_added_this_pass) break;
        }
        
    }
    return true;
}

void doBoxPush(bool *boxes_to_move, Direction direction)
{
    for (int boxes_to_move_index = 0; boxes_to_move_index < current_world_state.box_count; boxes_to_move_index++)
    {
        if (boxes_to_move[boxes_to_move_index] == false) continue;
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

void rotationMovement(bool clockwise, Direction input_direction, NormalizedCoords *next_player_coords, Direction *next_player_direction, bool *moved_this_frame)
{
    NormalizedCoords diagonal_potential_box_position = {0}, orthogonal_potential_box_position = {0};
    Direction 		 diagonal_push_direction = 0,           orthogonal_push_direction = 0;
	if (clockwise)
    {
		switch(input_direction)
        {
        case (NORTH):
            diagonal_potential_box_position   = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                                                                  	next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS) };
            diagonal_push_direction = SOUTH;

            orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x, 
                                                                    next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS)};
            orthogonal_push_direction = WEST;
            break;
        case (WEST):
            diagonal_potential_box_position   = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                                                                  	next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS) };
            diagonal_push_direction = EAST;

            orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS),
                                                                    next_player_coords->y }; 
            orthogonal_push_direction = SOUTH;
            break;
        case (SOUTH):
            diagonal_potential_box_position   = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                                                                  	next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS) };
            diagonal_push_direction = NORTH;

            orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x, 
                                                                    next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS)};
            orthogonal_push_direction = EAST;
            break;
        case (EAST):
            diagonal_potential_box_position   = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                                                                  	next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS) };
            diagonal_push_direction = WEST;

            orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS),
                                                                    next_player_coords->y };
            orthogonal_push_direction = NORTH;
            break;
        }
    }
	else
    {
        switch (input_direction)
        {
        case (NORTH):
            diagonal_potential_box_position   = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                                                                  	next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS) };
            diagonal_push_direction = SOUTH;

            orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x,
                                                                    next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS)};
            orthogonal_push_direction = EAST;
            break;
        case (WEST):
            diagonal_potential_box_position   = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                                                                  	next_player_coords->y - yPixelsToNorm((float)TILE_SIZE_PIXELS) };
            diagonal_push_direction = EAST;

            orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS),
                                                                    next_player_coords->y };
            orthogonal_push_direction = NORTH;
            break;
        case (SOUTH):
            diagonal_potential_box_position   = (NormalizedCoords){ next_player_coords->x + xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                                                                  	next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS) };
            diagonal_push_direction = NORTH;

            orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x,
                                                                    next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS)};
            orthogonal_push_direction = WEST;
            break;
        case (EAST):
            diagonal_potential_box_position   = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS), 
                                                                  	next_player_coords->y + yPixelsToNorm((float)TILE_SIZE_PIXELS) };
            diagonal_push_direction = WEST;

            orthogonal_potential_box_position = (NormalizedCoords){ next_player_coords->x - xPixelsToNorm((float)TILE_SIZE_PIXELS),
                                                                    next_player_coords->y };
            orthogonal_push_direction = SOUTH;
            break;
        }

    }
    for (int wall_index = 0; wall_index < current_world_state.wall_count; wall_index++)
    {    
		if (checkCollision(diagonal_potential_box_position,   box_dim_norm, current_world_state.walls[wall_index].origin, wall_dim_norm) || 
            checkCollision(orthogonal_potential_box_position, box_dim_norm, current_world_state.walls[wall_index].origin, wall_dim_norm))
        {
			return;
        }
    }

    bool diagonal_boxes_to_move[256] = {false};
    bool diagonal_push = canBoxPush(diagonal_potential_box_position, diagonal_push_direction, diagonal_boxes_to_move);
    bool orthogonal_boxes_to_move[256] = {false};
    bool orthogonal_push = canBoxPush(orthogonal_potential_box_position, orthogonal_push_direction, orthogonal_boxes_to_move);

    if (diagonal_push && orthogonal_push)
    {
        recordStateForUndo();
        doBoxPush(diagonal_boxes_to_move, diagonal_push_direction);
        doBoxPush(orthogonal_boxes_to_move, orthogonal_push_direction);
        *next_player_direction = input_direction;
        *moved_this_frame = true;
    }
}

void forwardMovement(Direction input_direction, NormalizedCoords *next_player_coords, bool *moved_this_frame)
{
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

    for (int wall_index = 0; wall_index < current_world_state.wall_count; wall_index++)
    {
        if (checkCollision(current_world_state.walls[wall_index].origin, wall_dim_norm, *next_player_coords, player_dim_norm))
        {
            switch (input_direction)
            {
                case NORTH:
                    next_player_coords->y = current_world_state.walls[wall_index].origin.y - player_dim_norm.y;
                    *moved_this_frame = false;
                    return;
                case WEST:
                    next_player_coords->x = current_world_state.walls[wall_index].origin.x + wall_dim_norm.x;
                    *moved_this_frame = false;
                    return;
                case SOUTH:
                    next_player_coords->y = current_world_state.walls[wall_index].origin.y + wall_dim_norm.y;
                    *moved_this_frame = false;
                    return;
                case EAST:
                    next_player_coords->x = current_world_state.walls[wall_index].origin.x - player_dim_norm.x;
                    *moved_this_frame = false;
                    return;
            }
        }
    }
    bool boxes_to_push[256] = {0};
    if (canBoxPush(*next_player_coords, input_direction, boxes_to_push)) 
    {
        recordStateForUndo();
        doBoxPush(boxes_to_push, input_direction);
        *moved_this_frame = true;
    }
    else *moved_this_frame = false;
    return; // can only ever push one box per forward movement
}


void handleInput(Direction input_direction, NormalizedCoords *next_player_coords, Direction *next_player_direction, bool *moved_this_frame)
{
	// check direction player is facing. if opposite, do nothing. if same, add to the direction and move. if one off, turn.
    int32 direction_switch = (input_direction - current_world_state.player_direction) % 4;
    if (direction_switch < 0) direction_switch += 4;

    switch (direction_switch)
    {
    	case 0:
        {
			forwardMovement(input_direction, next_player_coords, moved_this_frame);
            break;
        }
        case 1:
        {
            bool clockwise = false;
            rotationMovement(clockwise, input_direction, next_player_coords, next_player_direction, moved_this_frame);
            break;
        }
        case 2: break;
        case 3:
        {
            bool clockwise = true;
            rotationMovement(clockwise, input_direction, next_player_coords, next_player_direction, moved_this_frame);
            break;
        }
    }

	current_world_state.time_until_input_allowed = 8;
}

void loadFileAsLevel(char* level_path) 
{
    FILE *level_file = fopen(level_path, "rb");
    unsigned char byte = 0;

	IntCoords level_dimensions = {0};
    fread(&byte, 1, 1, level_file);
    level_dimensions.x = byte;
    fread(&byte, 1, 1, level_file);
    level_dimensions.y = byte;

    memset(current_world_state.voids, 0, sizeof(current_world_state.voids));
    current_world_state.void_count = 0;
    memset(current_world_state.grids, 0, sizeof(current_world_state.grids));
    current_world_state.grid_count = 0;
    memset(current_world_state.walls, 0, sizeof(current_world_state.walls));
    current_world_state.wall_count = 0;
    memset(current_world_state.boxes, 0, sizeof(current_world_state.boxes));
    current_world_state.box_count = 0;

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
	current_world_state.level_path = level_path;
    level_dimensions_int = level_dimensions;
}

void gameInitialise(void) 
{	
    void_dim_norm   = nearestPixelFloorToNorm(void_dim_int, DEFAULT_SCALE);
    grid_dim_norm   = nearestPixelFloorToNorm(grid_dim_int, DEFAULT_SCALE);
    wall_dim_norm   = nearestPixelFloorToNorm(wall_dim_int, DEFAULT_SCALE);
    box_dim_norm    = nearestPixelFloorToNorm(box_dim_int,  DEFAULT_SCALE);
    pack_dim_norm   = nearestPixelFloorToNorm(pack_dim_int, DEFAULT_SCALE);
    player_dim_norm = nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE);

    char* level_path_to_load = "data/levels/boxes.txt";
	camera_coords = (NormalizedCoords){ 0.7f, 1.2f };
    current_world_state.player_direction = NORTH;
    current_world_state.time_until_input_allowed = 0;

    loadFileAsLevel(level_path_to_load);

    editor_state.picking = false;
    editor_state.picked_tile = TILE_NULL;
    editor_state.picked_tile_path = 0;
}

void gameFrame(double delta_time, TickInput tick_input)
{	
   	// clamp for stalls or breakpoints
	if (delta_time > 0.1) delta_time = 0.1;

	accumulator += delta_time;

    while (accumulator >= PHYSICS_INCREMENT)
    {
        NormalizedCoords next_player_coords = current_world_state.player_coords; 
        Direction next_player_direction = current_world_state.player_direction;
        bool moved_this_frame = false;

        if (tick_input.i_press) camera_coords.y += CAMERA_MOVEMENT_SPEED;
        if (tick_input.j_press) camera_coords.x -= CAMERA_MOVEMENT_SPEED;
        if (tick_input.k_press) camera_coords.y -= CAMERA_MOVEMENT_SPEED;
        if (tick_input.l_press) camera_coords.x += CAMERA_MOVEMENT_SPEED;

        NormalizedCoords mouse_coords_adjusted = { tick_input.mouse_norm.x += camera_coords.x, tick_input.mouse_norm.y += camera_coords.y };

		if (tick_input.e_press && time_until_meta_input_allowed == 0) 
        {
            time_until_meta_input_allowed = 8;
            if (level_editor_mode) level_editor_mode = false;
            else				   level_editor_mode = true;
        }

        if (!level_editor_mode)
        {
            // game undo 
            if (tick_input.z_press && time_until_meta_input_allowed == 0)
            {
                time_until_meta_input_allowed = 8;

                int32 next_undo_buffer_position;
                if (undo_buffer_position != 0) next_undo_buffer_position = undo_buffer_position - 1;
                else next_undo_buffer_position = UNDO_BUFFER_SIZE - 1;
                if (undo_buffer[next_undo_buffer_position].level_path != 0)
                {
                    current_world_state = undo_buffer[next_undo_buffer_position]; 
                    memset(&undo_buffer[undo_buffer_position], 0, sizeof(WorldState));
                    undo_buffer_position = next_undo_buffer_position;
                }
                // else: no more undos in buffer (we are either back to start point, or we have run out of undo buffer space. cases can actually look identical; need a running variable for no. undos)
            }

            // restart level
            if (tick_input.r_press && time_until_meta_input_allowed == 0)
            {
                time_until_meta_input_allowed = 8;
                recordStateForUndo();
                loadFileAsLevel(current_world_state.level_path);
            }

            // MOVEMENT SYSTEM -> COLLISION HANDLER
            
            Direction input_direction = NORTH;
            if      (tick_input.w_press && current_world_state.time_until_input_allowed == 0) handleInput(input_direction = NORTH, &next_player_coords, &next_player_direction, &moved_this_frame);
            else if (tick_input.a_press && current_world_state.time_until_input_allowed == 0) handleInput(input_direction = WEST,  &next_player_coords, &next_player_direction, &moved_this_frame);
            else if (tick_input.s_press && current_world_state.time_until_input_allowed == 0) handleInput(input_direction = SOUTH, &next_player_coords, &next_player_direction, &moved_this_frame);
            else if (tick_input.d_press && current_world_state.time_until_input_allowed == 0) handleInput(input_direction = EAST,  &next_player_coords, &next_player_direction, &moved_this_frame);

            if (current_world_state.time_until_input_allowed != 0) current_world_state.time_until_input_allowed--;
        }
        else
        {
            // level editor mode
            if (tick_input.left_mouse_press && time_until_meta_input_allowed == 0)
            {
                time_until_meta_input_allowed = 16;
                if (editor_state.picking)
                {
					NormalizedCoords place_coords = nearestTileFloor(mouse_coords_adjusted, DEFAULT_SCALE);
                    for (int void_index = 0; void_index < current_world_state.void_count; void_index++)
                    {
                        if (current_world_state.voids[void_index].origin.x == place_coords.x && current_world_state.voids[void_index].origin.y == place_coords.y) 
                        {
                            if (current_world_state.voids[void_index].id == -1) continue;
                            current_world_state.voids[void_index].id = -1;
                            break;
                        }
                    }
                    for (int grid_index = 0; grid_index < current_world_state.grid_count; grid_index++)
                    {
                        if (current_world_state.grids[grid_index].origin.x == place_coords.x && current_world_state.grids[grid_index].origin.y == place_coords.y) 
                        {
                            if (current_world_state.grids[grid_index].id == -1) continue;
                            current_world_state.grids[grid_index].id = -1;
                            break;
                        }
                    }
                    for (int wall_index = 0; wall_index < current_world_state.wall_count; wall_index++)
                    {
                        if (current_world_state.walls[wall_index].origin.x == place_coords.x && current_world_state.walls[wall_index].origin.y == place_coords.y) 
                        {
                            if (current_world_state.walls[wall_index].id == -1) continue;
                            current_world_state.walls[wall_index].id = -1;
                            break;
                        }
                    }
                    for (int box_index = 0; box_index < current_world_state.box_count; box_index++)
                    {
                        if (current_world_state.boxes[box_index].origin.x == place_coords.x && current_world_state.boxes[box_index].origin.y == place_coords.y) 
                        {
                            if (current_world_state.boxes[box_index].id == -1) continue;
                            current_world_state.boxes[box_index].id = -1;
                            break;
                        }
                    }
                    switch (editor_state.picked_tile)
                    {
                        case TILE_VOID:
                            current_world_state.voids[current_world_state.void_count].origin = place_coords;
                            current_world_state.voids[current_world_state.void_count].id = current_world_state.void_count;
                            current_world_state.void_count++;
                            break;
                        case TILE_GRID:
                            current_world_state.grids[current_world_state.grid_count].origin = place_coords;
                            current_world_state.grids[current_world_state.grid_count].id = current_world_state.grid_count;
                            current_world_state.grid_count++;
                            break;
                        case TILE_WALL:
                            current_world_state.walls[current_world_state.wall_count].origin = place_coords;
                            current_world_state.walls[current_world_state.wall_count].id = current_world_state.wall_count;
                            current_world_state.wall_count++;
                            break;
                        case TILE_BOX:
                            current_world_state.boxes[current_world_state.box_count].origin = place_coords;
                            current_world_state.boxes[current_world_state.box_count].id = current_world_state.box_count;
                            current_world_state.box_count++;
                            current_world_state.grids[current_world_state.grid_count].origin = place_coords;
                            current_world_state.grids[current_world_state.grid_count].id = current_world_state.grid_count;
                            current_world_state.grid_count++;
                            break;
                        default:
                    		break;
                    }
                    
                    // TODO(spike): actually modify the file
                    // NormalizedCoords mouse_half_tile_offset = { mouse_coords_adjusted.x - xPixelsToNorm(8), mouse_coords_adjusted.y - yPixelsToNorm(8) };
                    // IntCoords mouse_position_int = { mouse_half_tile_offset.x / NORM_DISTANCE_PER_TILE.x, mouse_half_tile_offset.y / NORM_DISTANCE_PER_TILE.y };
					// tile_index = intCoordsToTileIndex(mouse_position_int, level_dimensions_int);
                    // write to the file, at location tileIndex, to the tile type editor_state.picked_tile
                }
            }
            else if (tick_input.middle_mouse_press && time_until_meta_input_allowed == 0)
            {
                time_until_meta_input_allowed = 8;
                if (!editor_state.picking)
                {
                    editor_state.picking = true;
                    for (int void_index = 0; void_index < current_world_state.void_count; void_index++)
                    {
                        if (current_world_state.voids[void_index].id == -1) continue;
                        if (checkCollision(current_world_state.voids[void_index].origin, void_dim_norm, mouse_coords_adjusted, CURSOR_DIMS))
                        {
                            editor_state.picked_tile = TILE_VOID;
                            editor_state.picked_tile_path = void_path;
                            break;
                        }
                    }
                    for (int grid_index = 0; grid_index < current_world_state.grid_count; grid_index++)
                    {
                        if (current_world_state.grids[grid_index].id == -1) continue;
                        if (checkCollision(current_world_state.grids[grid_index].origin, grid_dim_norm, mouse_coords_adjusted, CURSOR_DIMS))
                        {
                            editor_state.picked_tile = TILE_GRID;
                            editor_state.picked_tile_path = grid_path;
                            break;
                        }
                    }
                    for (int wall_index = 0; wall_index < current_world_state.wall_count; wall_index++)
                    {
                        if (current_world_state.walls[wall_index].id == -1) continue;
                        if (checkCollision(current_world_state.walls[wall_index].origin, wall_dim_norm, mouse_coords_adjusted, CURSOR_DIMS))
                        {
                            editor_state.picked_tile = TILE_WALL;
                            editor_state.picked_tile_path = wall_path;
                            break;
                        }
                    }
                    for (int box_index = 0; box_index < current_world_state.box_count; box_index++)
                    {
                        if (current_world_state.boxes[box_index].id == -1) continue;
                        if (checkCollision(current_world_state.boxes[box_index].origin, box_dim_norm, mouse_coords_adjusted, CURSOR_DIMS))
                        {
                            editor_state.picked_tile = TILE_BOX;
                            editor_state.picked_tile_path = box_path;
                            break;
                        }
                    }
                }
                else
                {
                    editor_state.picking = false;
                    editor_state.picked_tile = TILE_NULL;
                    editor_state.picked_tile_path = 0;
                }
            }
        }

        // DRAW SPRITES

        // draw voids; and check if equal to player or some box
        for (int16 void_index = 0; void_index < current_world_state.void_count; void_index++)
        {
            if (current_world_state.voids[void_index].id == -1) continue;
            drawSprite(void_path, current_world_state.voids[void_index].origin, void_dim_norm, true);
        }

        // draw grids, and void player
        bool player_in_void = true;
        for (int16 grid_index = 0; grid_index < current_world_state.grid_count; grid_index++)
        {
            if (current_world_state.grids[grid_index].id == -1) continue;
            drawSprite(grid_path, current_world_state.grids[grid_index].origin, grid_dim_norm, true);
            if (checkCollision(current_world_state.grids[grid_index].origin, grid_dim_norm, next_player_coords, player_dim_norm)) player_in_void = false;
        }

        // draw walls
        for (int16 wall_index = 0; wall_index < current_world_state.wall_count; wall_index++)
        {
            if (current_world_state.walls[wall_index].id == -1) continue;
            drawSprite(wall_path, current_world_state.walls[wall_index].origin, wall_dim_norm, true);
        }

        // draw boxes, and void some
        for (int16 box_index = 0; box_index < current_world_state.box_count; box_index++)
        {
            if (current_world_state.boxes[box_index].id == -1) continue;
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
            drawSprite(box_path, current_world_state.boxes[box_index].origin, box_dim_norm, true);
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
                current_world_state.player_coords = next_player_coords;
                current_world_state.player_direction = next_player_direction;
            }
            drawSprite(player_path, nearestPixelFloor(current_world_state.player_coords, DEFAULT_SCALE), nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE), true);
        }

        // draw pack behind player
        drawSprite(pack_path, getPackCoords(current_world_state.player_coords, current_world_state.player_direction), nearestPixelFloorToNorm(pack_dim_int, DEFAULT_SCALE), true);

        if (level_editor_mode) // load temporary tiles from editor on top
        {
            if (editor_state.picking)
            {
                NormalizedCoords mouse_half_tile_offset = { mouse_coords_adjusted.x - xPixelsToNorm(8), mouse_coords_adjusted.y - yPixelsToNorm(8) };
                drawSprite(editor_state.picked_tile_path, mouse_half_tile_offset, void_dim_norm, true);
            }
            
        }
    	if (time_until_meta_input_allowed != 0) time_until_meta_input_allowed--;


        rendererSubmitFrame(textures_to_load);
        memset(textures_to_load, 0, sizeof(textures_to_load));

        /* can't do this yet, need to not have ids = index... or, when filling the array, go to next -1 maybe? 
        cleanNullIds(current_world_state.voids, &current_world_state.void_count);
        cleanNullIds(current_world_state.grids, &current_world_state.grid_count);
        cleanNullIds(current_world_state.walls, &current_world_state.wall_count);
        cleanNullIds(current_world_state.boxes, &current_world_state.box_count);
        */

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
