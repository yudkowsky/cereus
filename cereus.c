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

WorldState current_world_state = {0};
double accumulator = 0.0;

char* grid_tile_path = "data/sprites/grid.png";
IntCoords grid_tile_dim_int = { .x = 16, .y = 16 };
NormalizedCoords grid_tile_dim_norm = {0};

char* player_path = "data/sprites/player.png";
IntCoords player_dim_int = { .x = 16, .y = 16 };
NormalizedCoords player_dim_norm = {0};

char* floor_tile_path = "data/sprites/floor.png";
IntCoords floor_tile_dim_int = { .x = 48 , .y = 32 };
NormalizedCoords floor_tile_dim_norm = {0};

TextureToLoad textures_to_load[128] = {0};
char* loaded_textures[128] = {0};

Rect collision_boxes[64] = {0};
int32 collision_box_count = 0;

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

// adjusts a very small amount, to lie perfectly on a pixel boundary
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

// expects origin and dimensions in norm space
void createCollisionBox(NormalizedCoords origin, NormalizedCoords dimensions)
{
    collision_boxes[collision_box_count].origin.x   = origin.x;
    collision_boxes[collision_box_count].origin.y   = origin.y;
    collision_boxes[collision_box_count].dimensions = dimensions;
    collision_box_count++;
}

void gameInitialise(void) 
{	
    player_dim_norm     = nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE);
    grid_tile_dim_norm  = nearestPixelFloorToNorm(grid_tile_dim_int, DEFAULT_SCALE);
    floor_tile_dim_norm = nearestPixelFloorToNorm(floor_tile_dim_int, DEFAULT_SCALE);

    current_world_state.player_coords   = (NormalizedCoords){ -0.5f, -0.2f };
    current_world_state.player_velocity = (NormalizedCoords){ 0.0f, 0.0f };
	current_world_state.camera_coords   = (NormalizedCoords){ 0.0f, 0.0f };
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

        if (tick_input.w_press) next_player_coords.y += yPixelsToNorm(1.5);
        if (tick_input.a_press) next_player_coords.x -= xPixelsToNorm(1.5);
        if (tick_input.s_press) next_player_coords.y -= yPixelsToNorm(1.5);
        if (tick_input.d_press) next_player_coords.x += xPixelsToNorm(1.5);

		// draw floor
        for (int16 i = 0; i < 4; i++)
        {
            IntCoords floor_coords_int = { i * floor_tile_dim_int.x - 96, -48 };
            NormalizedCoords floor_coords_norm   = nearestPixelFloorToNorm(floor_coords_int, DEFAULT_SCALE);
            drawSprite(floor_tile_path, floor_coords_norm, floor_tile_dim_norm);

            createCollisionBox(floor_coords_norm, floor_tile_dim_norm);
        }

        // collision detection

        // handle x collision
		NormalizedCoords test_x_collision = current_world_state.player_coords;
        test_x_collision.x = next_player_coords.x;

        for (int i = 0; i < collision_box_count; i++)
        {
			if (collision_boxes[i].origin.x < test_x_collision.x + player_dim_norm.x && test_x_collision.x < collision_boxes[i].origin.x + collision_boxes[i].dimensions.x &&
			    collision_boxes[i].origin.y < test_x_collision.y + player_dim_norm.y && test_x_collision.y < collision_boxes[i].origin.y + collision_boxes[i].dimensions.y)
            {
                // collision on x axis occurs; check from which direction
                if (next_player_coords.x - current_world_state.player_coords.x > 0)
                {
                    // moving right
                    next_player_coords.x = collision_boxes[i].origin.x - player_dim_norm.x;
                    break;
                }
                else
                {
                    // moving left
                    next_player_coords.x = collision_boxes[i].origin.x + collision_boxes[i].dimensions.x;
                    break;
                }
            }
        }

        // handle y collision
        NormalizedCoords test_y_collision = current_world_state.player_coords;
        test_y_collision.y = next_player_coords.y;

        for (int i = 0; i < collision_box_count; i++)
        {
			if (collision_boxes[i].origin.x < test_y_collision.x + player_dim_norm.x && test_y_collision.x < collision_boxes[i].origin.x + collision_boxes[i].dimensions.x &&
			    collision_boxes[i].origin.y < test_y_collision.y + player_dim_norm.y && test_y_collision.y < collision_boxes[i].origin.y + collision_boxes[i].dimensions.y)
            {
                // work out from above or below
                if (next_player_coords.y - current_world_state.player_coords.y > 0)
                {
                    // moving up
                    next_player_coords.y = collision_boxes[i].origin.y - player_dim_norm.y;
                    break;
                }
                else
                {
                    // moving down
                    next_player_coords.y = collision_boxes[i].origin.y + collision_boxes[i].dimensions.y;
                    break;
                }
            }
        }

		// draw player
        current_world_state.player_coords = next_player_coords;
        drawSprite(player_path, nearestPixelFloor(current_world_state.player_coords, DEFAULT_SCALE), nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE));

        rendererSubmitFrame(textures_to_load);

        // zero the per-frame arrays
        memset(textures_to_load, 0, sizeof(textures_to_load));
        memset(collision_boxes, 0, sizeof(collision_boxes));
        collision_box_count = 0;

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
