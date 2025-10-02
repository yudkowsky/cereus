#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> //TODO(spike): also temporary, for floor

#define local_persist static
#define global_variable static
#define internal static

double PHYSICS_INCREMENT = 1.0/60.0;
NormalizedCoords NORM_DISTANCE_PER_SCREEN_PIXEL = { 2.0f / 1920.0f, 2.0f / 1080.0f };
IntCoords DEFAULT_SCALE = {8, 8};
float CAMERA_MOVEMENT_SPEED = 0.01f; // arbitrary movement per frame; temporary anyway, will want to use variable for this
float CAMERA_CLIPPING_RADIUS = 1.0f;

WorldState current_world_state = {0};
double accumulator = 0.0;

char* wall_tile_path = "data/sprites/wall.png";
IntCoords wall_tile_dim_int = { .x = 16, .y = 16 };
NormalizedCoords wall_tile_dim_norm = {0};

char* floor_tile_path = "data/sprites/floor.png";
IntCoords floor_tile_dim_int = { .x = 48 , .y = 32 };
NormalizedCoords floor_tile_dim_norm = {0};

char* box_path = "data/sprites/box.png";
IntCoords box_dim_int = { .x = 16, .y = 16};
NormalizedCoords box_dim_norm = {0};

char* player_path = "data/sprites/player.png";
IntCoords player_dim_int = { .x = 16, .y = 16 };
NormalizedCoords player_dim_norm = {0};

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

void collisionBoxSystem(NormalizedCoords* next_player_coords, float distance, bool x_direction)
{
    int32 boxes_to_move_ids[64] = {0};
    int32 boxes_to_move_count = 0;

    // populate boxes_to_move_ids and count with first 1 or 2 boxes to move 
    for (int box_id = 0; box_id < current_world_state.box_count; box_id++)
    {
        if (checkCollision(*next_player_coords, player_dim_norm, current_world_state.boxes[box_id].origin, box_dim_norm))
        {
            float abs_dy =(float)fabs(current_world_state.player_coords.y - current_world_state.boxes[box_id].origin.y);
            if (abs_dy >= box_dim_norm.y - yPixelsToNorm(0.1f) && x_direction == true) return;

            boxes_to_move_ids[boxes_to_move_count] = box_id;
            boxes_to_move_count++;
        }
    }

    if (boxes_to_move_count > 0)	
    {
        // main loop with a growing boxes_to_move_ids array
        for (int pass_nr = 0; pass_nr < 64; pass_nr++)
        {
            bool new_box_added_this_pass = false;
            for (int boxes_to_move_index = 0; boxes_to_move_index < boxes_to_move_count; boxes_to_move_index++)
            {
                NormalizedCoords box_position_after_move = {0};
                if (x_direction)
                {
                    box_position_after_move = (NormalizedCoords){ current_world_state.boxes[boxes_to_move_ids[boxes_to_move_index]].origin.x + distance, 
                                                                  current_world_state.boxes[boxes_to_move_ids[boxes_to_move_index]].origin.y };
                }
                else
                {
                    box_position_after_move = (NormalizedCoords){ current_world_state.boxes[boxes_to_move_ids[boxes_to_move_index]].origin.x, 
                                                                  current_world_state.boxes[boxes_to_move_ids[boxes_to_move_index]].origin.y + distance };
                }

				for (int wall_id = 0; wall_id < current_world_state.wall_count; wall_id++)
                {
                    if (checkCollision(current_world_state.walls[wall_id].origin, wall_tile_dim_norm, box_position_after_move, box_dim_norm))
                    {
                        if (x_direction)
                        {
							if (distance > 0) 
                            {
                                (*next_player_coords).x = current_world_state.boxes[boxes_to_move_ids[0]].origin.x - player_dim_norm.x;
                                current_world_state.d_time_until_allowed = 0;
                            }
							else 
                            {
                                (*next_player_coords).x = current_world_state.boxes[boxes_to_move_ids[0]].origin.x + box_dim_norm.x;
                                current_world_state.a_time_until_allowed = 0;
                            }
                        }
                        else
                        {
							if (distance > 0) 
                            {
                                (*next_player_coords).y = current_world_state.boxes[boxes_to_move_ids[0]].origin.y - player_dim_norm.y;
                                current_world_state.w_time_until_allowed = 0;
                            }
							else 
                            {
                                (*next_player_coords).y = current_world_state.boxes[boxes_to_move_ids[0]].origin.y + box_dim_norm.y;
                                current_world_state.s_time_until_allowed = 0;
                            }
                        }
						return;
                    }
                }

                for (int box_id = 0; box_id < current_world_state.box_count; box_id++)
                {
                    if (checkCollision(current_world_state.boxes[box_id].origin, box_dim_norm, box_position_after_move, box_dim_norm))
                    {
                        bool already_present = false;
                        for (int box_already_present_index = 0; box_already_present_index < boxes_to_move_count; box_already_present_index++)
                        {
                            if (boxes_to_move_ids[box_already_present_index] == box_id) 
                            {
                                already_present = true;
                                break;
                            }
                        }
                        if (!already_present)
                        {
                            boxes_to_move_ids[boxes_to_move_count] = box_id;
                            boxes_to_move_count++;
                            new_box_added_this_pass = true;
                            break;
                        }
                    }
                }
            }
            if (!new_box_added_this_pass) break;
        }
        
        // modify global state: move boxes in boxes_to_move_ids
        for (int box_to_move_index = 0; box_to_move_index < boxes_to_move_count; box_to_move_index++)
        {
            if (x_direction) current_world_state.boxes[boxes_to_move_ids[box_to_move_index]].origin.x += distance;
            else 			 current_world_state.boxes[boxes_to_move_ids[box_to_move_index]].origin.y += distance;
        }
    }
}

void gameInitialise(void) 
{	
    wall_tile_dim_norm  = nearestPixelFloorToNorm(wall_tile_dim_int, DEFAULT_SCALE);
    floor_tile_dim_norm = nearestPixelFloorToNorm(floor_tile_dim_int, DEFAULT_SCALE);
    box_dim_norm        = nearestPixelFloorToNorm(box_dim_int, DEFAULT_SCALE);
    player_dim_norm     = nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE);

    IntCoords player_coords_int = {16, 16};
    current_world_state.player_coords   = nearestPixelFloorToNorm(player_coords_int, DEFAULT_SCALE);
    current_world_state.player_velocity = (NormalizedCoords){ 0.0f, 0.0f };
	current_world_state.camera_coords   = (NormalizedCoords){ 0.0f, 0.0f };

	current_world_state.w_time_until_allowed = 0;
	current_world_state.a_time_until_allowed = 0;
	current_world_state.s_time_until_allowed = 0;
	current_world_state.d_time_until_allowed = 0;

    // set up walls
    IntCoords wall_coords_int[64] = { {-32,  48}, {-16,  48}, { 0 ,  48}, { 16,  48}, { 32,  48}, { 48,  48},
        						    //{-32,  32},                                                 { 48,  32},
        							  {-32,  16}, 								                  { 48,  16},
                                      {-32,  0 }, 								                  { 48,  0 },
                                      {-32, -16}, 								                  { 48, -16},
                                      {-32, -32}, {-16, -32}, { 0 , -32}, { 16, -32}, { 32, -32}, { 48, -32}  };

    current_world_state.wall_count = 18;
    for (int16 wall_index = 0; wall_index < current_world_state.wall_count; wall_index++)
    {
        current_world_state.walls[wall_index].origin = nearestPixelFloorToNorm(wall_coords_int[wall_index], DEFAULT_SCALE);
        current_world_state.walls[wall_index].id = wall_index;
    }

    // set up boxes
	IntCoords box_coords_int[64] = { { 32,  64}, { 16,  72}, { 0 ,  64}, {-16,  72}, {-32,  64}, {-48,  72},
									 { 32,  80}, { 16,  96}, { 0 ,  80},             {-32,  80}  };

    current_world_state.box_count = 10;
    for (int16 box_index = 0; box_index < current_world_state.box_count; box_index++)
    {
        current_world_state.boxes[box_index].origin = nearestPixelFloorToNorm(box_coords_int[box_index], DEFAULT_SCALE);
        current_world_state.boxes[box_index].id = box_index;
    }
}

void gameFrame(double delta_time, TickInput tick_input)
{	
   	// clamp for stalls or breakpoints
	if (delta_time > 0.1) delta_time = 0.1;

	accumulator += delta_time;

    while (accumulator >= PHYSICS_INCREMENT)
    {
		NormalizedCoords next_player_coords = current_world_state.player_coords; // prevents garbage data in global state

        if (tick_input.i_press) current_world_state.camera_coords.y += CAMERA_MOVEMENT_SPEED;
        if (tick_input.j_press) current_world_state.camera_coords.x -= CAMERA_MOVEMENT_SPEED;
        if (tick_input.k_press) current_world_state.camera_coords.y -= CAMERA_MOVEMENT_SPEED;
        if (tick_input.l_press) current_world_state.camera_coords.x += CAMERA_MOVEMENT_SPEED;

        // movement input - this entire section is terrible

		if (tick_input.w_press && tick_input.a_press && current_world_state.time_until_allowed == 0)
        {
            current_world_state.w_time_until_allowed = 8;
            current_world_state.a_time_until_allowed = 8;
            current_world_state.time_until_allowed = 8;
        }
		if (tick_input.a_press && tick_input.s_press && current_world_state.time_until_allowed == 0)
        {
            current_world_state.a_time_until_allowed = 8;
            current_world_state.s_time_until_allowed = 8;
            current_world_state.time_until_allowed = 8;
        }
		if (tick_input.s_press && tick_input.d_press && current_world_state.time_until_allowed == 0)
        {
            current_world_state.s_time_until_allowed = 8;
            current_world_state.d_time_until_allowed = 8;
            current_world_state.time_until_allowed = 8;
        }
		if (tick_input.d_press && tick_input.w_press && current_world_state.time_until_allowed == 0)
        {
            current_world_state.d_time_until_allowed = 8;
            current_world_state.w_time_until_allowed = 8;
            current_world_state.time_until_allowed = 8;
        }

        if (tick_input.w_press && current_world_state.time_until_allowed == 0)
        {
            current_world_state.w_time_until_allowed = 8;
            current_world_state.time_until_allowed = 8;
        }
        if (tick_input.a_press && current_world_state.time_until_allowed == 0) 
        {
            current_world_state.a_time_until_allowed = 8;
            current_world_state.time_until_allowed = 8;
        }
        if (tick_input.s_press && current_world_state.time_until_allowed == 0)
        {
            current_world_state.s_time_until_allowed = 8;
            current_world_state.time_until_allowed = 8;
        }
        if (tick_input.d_press && current_world_state.time_until_allowed == 0)
        {
            current_world_state.d_time_until_allowed = 8;
            current_world_state.time_until_allowed = 8;
        }

        if (current_world_state.w_time_until_allowed != 0)
        {
            if (current_world_state.w_time_until_allowed % 2 == 0) next_player_coords.y += yPixelsToNorm(2);
            current_world_state.w_time_until_allowed--;
        }
        if (current_world_state.a_time_until_allowed != 0)
        {
            if (current_world_state.a_time_until_allowed % 2 == 0) next_player_coords.x -= xPixelsToNorm(2);
            current_world_state.a_time_until_allowed--;
        }
        if (current_world_state.s_time_until_allowed != 0)
        {
            if (current_world_state.s_time_until_allowed % 2 == 0) next_player_coords.y -= yPixelsToNorm(2);
            current_world_state.s_time_until_allowed--;
        }
        if (current_world_state.d_time_until_allowed != 0)
        {
            if (current_world_state.d_time_until_allowed % 2 == 0) next_player_coords.x += xPixelsToNorm(2);
            current_world_state.d_time_until_allowed--;
        }

		if (current_world_state.time_until_allowed != 0) current_world_state.time_until_allowed--;

        // collision detection

        // handle x wall collision
		NormalizedCoords test_x_wall_collision = { next_player_coords.x, current_world_state.player_coords.y };
        bool x_collision = false;

        for (int i = 0; i < current_world_state.wall_count; i++)
        {
            if (checkCollision(current_world_state.walls[i].origin, wall_tile_dim_norm, test_x_wall_collision, player_dim_norm))
            {
                x_collision = true;
                // collision on x axis occurs; check from which direction
                if (next_player_coords.x - current_world_state.player_coords.x > 0)
                {
                    // moving right
                    next_player_coords.x = current_world_state.walls[i].origin.x - player_dim_norm.x;
                    break;
                }
                else
                {
                    // moving left
                    next_player_coords.x = current_world_state.walls[i].origin.x + wall_tile_dim_norm.x;
                    break;
                }
            }
        }

        // handle y wall collision
        NormalizedCoords test_y_wall_collision = { current_world_state.player_coords.x, next_player_coords.y };
        bool y_collision = false;

        for (int i = 0; i < current_world_state.wall_count; i++)
        {
            if (checkCollision(current_world_state.walls[i].origin, wall_tile_dim_norm, test_y_wall_collision, player_dim_norm))
            {
                y_collision = true;
                // work out from above or below
                if (next_player_coords.y - current_world_state.player_coords.y > 0)
                {
                    // moving up
                    next_player_coords.y = current_world_state.walls[i].origin.y - player_dim_norm.y;
                    break;
                }
                else
                {
                    // moving down
                    next_player_coords.y = current_world_state.walls[i].origin.y + wall_tile_dim_norm.y;
                    break;
                }
            }
        }

		// if no collisions, check if the combined diagonal movement causes collision
        if (!x_collision && !y_collision)
        {
            for (int i = 0; i < current_world_state.wall_count; i++)
            {
                if (checkCollision(current_world_state.walls[i].origin, wall_tile_dim_norm, next_player_coords, player_dim_norm))
                {
                    next_player_coords = current_world_state.player_coords; // block movement entirely
                    break;
                }
            }
        }

        // box collision calculations
        
        if (current_world_state.d_time_until_allowed != 0) {
            collisionBoxSystem(&next_player_coords, xPixelsToNorm(8), true);
        }
        if (current_world_state.a_time_until_allowed != 0) {
            collisionBoxSystem(&next_player_coords, xPixelsToNorm(-8), true);
        }
        if (current_world_state.w_time_until_allowed != 0) {
            collisionBoxSystem(&next_player_coords, yPixelsToNorm(8), false);
        }
        if (current_world_state.s_time_until_allowed != 0) {
            collisionBoxSystem(&next_player_coords, yPixelsToNorm(-8), false);
        }

        // draw walls
        for (int16 wall_index = 0; wall_index < current_world_state.wall_count; wall_index++)
        {
			drawSprite(wall_tile_path, current_world_state.walls[wall_index].origin, wall_tile_dim_norm);
        }

        // draw boxes 
        for (int16 box_index = 0; box_index < current_world_state.box_count; box_index++)
        {
			drawSprite(box_path, current_world_state.boxes[box_index].origin, box_dim_norm);
        }

		// draw player
        current_world_state.player_coords = next_player_coords;
        drawSprite(player_path, nearestPixelFloor(current_world_state.player_coords, DEFAULT_SCALE), nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE));

        rendererSubmitFrame(textures_to_load);
        memset(textures_to_load, 0, sizeof(textures_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
