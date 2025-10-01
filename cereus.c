#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> //TODO(spike): also temporary, for floor

#define local_persist static
#define global_variable static
#define internal static

double PHYSICS_INCREMENT = 1.0/60.0;
NormalizedCoords NORM_DISTANCE_PER_SCREEN_PIXEL = { 2.0f / 1920.0f, 2.0f / 1080.0f };
IntCoords DEFAULT_SCALE = {6, 6};
float CAMERA_MOVEMENT_SPEED = 0.01f; // arbitrary movement per frame; temporary anyway, will want to use variable for this
float CAMERA_CLIPPING_RADIUS = 1.0f;
float PIXEL_MOVEMENT_PER_FRAME = 1.0f;

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

// modifies textures_to_load
// expects origin and dimensions in norm space.
// scale input is pixels/pixel, output is norm space scale 
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
	origin.x += current_world_state.camera_coords.x;
	origin.y += current_world_state.camera_coords.y;

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

void gameInitialise(void) 
{	
    wall_tile_dim_norm  = nearestPixelFloorToNorm(wall_tile_dim_int, DEFAULT_SCALE);
    floor_tile_dim_norm = nearestPixelFloorToNorm(floor_tile_dim_int, DEFAULT_SCALE);
    box_dim_norm        = nearestPixelFloorToNorm(box_dim_int, DEFAULT_SCALE);
    player_dim_norm     = nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE);

    current_world_state.player_coords   = (NormalizedCoords){ 0.0f, 0.0f };
    current_world_state.player_velocity = (NormalizedCoords){ 0.0f, 0.0f };
	current_world_state.camera_coords   = (NormalizedCoords){ 0.0f, 0.0f };

	current_world_state.w_time_until_allowed = 0;
	current_world_state.a_time_until_allowed = 0;
	current_world_state.s_time_until_allowed = 0;
	current_world_state.d_time_until_allowed = 0;

    // set up walls
    IntCoords wall_coords_int[64] = { {-32,  48}, {-16,  48}, { 0 ,  48}, { 16,  48}, { 32,  48}, { 48,  48},
        							  {-32,  32},                                                 { 48,  32},
        							  {-32,  16}, 								                  { 48,  16},
                                      {-32,  0 }, 								                  { 48,  0 },
                                      {-32, -16}, 								                  { 48, -16},
                                      {-32, -32}, {-16, -32}, { 0 , -32}, { 16, -32}, { 32, -32}, { 48, -32}  };

    current_world_state.wall_count = 20;
    for (int16 wall_index = 0; wall_index < current_world_state.wall_count; wall_index++)
    {
        current_world_state.walls[wall_index].origin = nearestPixelFloorToNorm(wall_coords_int[wall_index], DEFAULT_SCALE);
        current_world_state.walls[wall_index].id = wall_index;
    }

    // set up boxes
	IntCoords box_coords_int[64] = { {0, 16}, {64, 0} };

    current_world_state.box_count = 2;
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

        // movement input
        if (tick_input.w_press) 
        {
			if (current_world_state.w_time_until_allowed == 0 && current_world_state.s_time_until_allowed == 0) current_world_state.w_time_until_allowed = 8; 
        }
        if (tick_input.a_press) 
        {
			if (current_world_state.a_time_until_allowed == 0 && current_world_state.d_time_until_allowed == 0) current_world_state.a_time_until_allowed = 8; 
        }
        if (tick_input.s_press) 
        {
			if (current_world_state.s_time_until_allowed == 0 && current_world_state.w_time_until_allowed == 0) current_world_state.s_time_until_allowed = 8; 
        }
        if (tick_input.d_press) 
        {
			if (current_world_state.d_time_until_allowed == 0 && current_world_state.a_time_until_allowed == 0) current_world_state.d_time_until_allowed = 8; 
        }

        // handle movement
        if (current_world_state.w_time_until_allowed != 0) 
        {
            next_player_coords.y += yPixelsToNorm(PIXEL_MOVEMENT_PER_FRAME);
            current_world_state.w_time_until_allowed--;
        }
        if (current_world_state.a_time_until_allowed != 0) 
        {
            next_player_coords.x -= xPixelsToNorm(PIXEL_MOVEMENT_PER_FRAME);
            current_world_state.a_time_until_allowed--;
        }
        if (current_world_state.s_time_until_allowed != 0) 
        {
            next_player_coords.y -= yPixelsToNorm(PIXEL_MOVEMENT_PER_FRAME);
            current_world_state.s_time_until_allowed--;
        }
        if (current_world_state.d_time_until_allowed != 0) 
        {
            next_player_coords.x += xPixelsToNorm(PIXEL_MOVEMENT_PER_FRAME);
            current_world_state.d_time_until_allowed--;
        }

        // collision detection

        // handle x wall collision
		NormalizedCoords test_x_wall_collision = current_world_state.player_coords;
        test_x_wall_collision.x = next_player_coords.x;

        for (int i = 0; i < current_world_state.wall_count; i++)
        {
            if (checkCollision(current_world_state.walls[i].origin, wall_tile_dim_norm, nearestPixelFloor(test_x_wall_collision, DEFAULT_SCALE), player_dim_norm))
            {
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
        NormalizedCoords test_y_wall_collision = current_world_state.player_coords;
        test_y_wall_collision.y = next_player_coords.y;

        for (int i = 0; i < current_world_state.wall_count; i++)
        {
            if (checkCollision(current_world_state.walls[i].origin, wall_tile_dim_norm, nearestPixelFloor(test_y_wall_collision, DEFAULT_SCALE), player_dim_norm))
            {
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

        // check x box collision
        NormalizedCoords test_x_box_collision = { next_player_coords.x, current_world_state.player_coords.y};
        bool full_break = false;
        bool half_break = false;

        for (int initial_collision_index = 0; initial_collision_index < current_world_state.box_count; initial_collision_index++)
        {
            if (checkCollision(current_world_state.boxes[initial_collision_index].origin, box_dim_norm, nearestPixelFloor(test_x_box_collision, DEFAULT_SCALE), player_dim_norm))
            {
                int32 boxes_to_move_ids[64] = {0};
                boxes_to_move_ids[0] = current_world_state.boxes[initial_collision_index].id; 
                int32 boxes_to_move_count = 1;
                NormalizedCoords next_box_position = { current_world_state.boxes[initial_collision_index].origin.x + xPixelsToNorm(8), current_world_state.boxes[initial_collision_index].origin.y };
				bool continue_chain = false;

				// work out if pushing to left or right
                if (next_player_coords.x - current_world_state.player_coords.x > 0)
                {
                    // pushing to the right
					

					// for (all boxes; index = place_in_box_chain)
                    	// for (all walls; index = wall_index)
                        	// if (next_box_position, box_dim_norm, current_world_state.walls[wall_index].origin, wall_tile_dim_norm))
                            	// there is collision; 
                                // player pos back to initial collision index box +- some dimension factor
                                // break out of ALL loops (even top one)
                                // full_break = true;
                                // break;
                        // if (full_break) break;

                        // for (all boxes; index = box_index)
                    		// if (checkCollision(next_box_position, box_dim_norm, current_world_state.boxes[box_index].origin, box_dim_norm))
                    			// if (boxes_to_move_ids[boxes_to_move_count - 1] == current_world_state.boxes[box_index].id) continue;
								// boxes_to_move_ids[boxes_to_move_count] = current_world_state.boxes[box_index].id;
                                // boxes_to_move_count++;
                                // next_box_position = { current_world_state.boxes[box_index].origin.x + xPixelsToNorm(8), current_world_state.boxes[box_index].origin.y };
                                // continue_chain = true;
                                // break;
                        // if (continue_chain) continue;

                        // if here, then there is air in front of us, so we can move!
                        // for (box_to_move_count; index = box_to_move_index)
                        	// for (box_in_state_index)
                            	// if (current_world_state.boxes[box_in_state_index].id == box_to_move[box_to_move_index])
                                	// current_world_state.boxes[box_in_state_index].origin.x += xPixelsToNorm(8); 
                                    // break;
                            // break out of all loops EXCEPT the top one
                            // half_break = true;
                            // break;
                        // if (half_break) break;


                }
                else
                {
                    // pushing left
                }
                if (full_break == true) break;
            }
        }

        // check y box collision
        NormalizedCoords test_y_box_collision = current_world_state.player_coords;
        test_y_box_collision.y = next_player_coords.y;

        for (int i = 0; i < current_world_state.box_count; i++)
        {
            if (checkCollision(current_world_state.boxes[i].origin, box_dim_norm, nearestPixelFloor(test_y_box_collision, DEFAULT_SCALE), player_dim_norm))
            {
				// work out if pushing up or down
                if (next_player_coords.y - current_world_state.player_coords.y > 0)
                {
                    // pushing up
                }
                else
                {
                    // pushing down
                }
            }
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
