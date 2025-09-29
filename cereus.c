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

char* player_path = "data/sprites/player.png";
IntCoords player_dim_int = { .x = 16, .y = 16 };

char* floor_tile_path = "data/sprites/floor.png";
IntCoords floor_tile_dim_int = { .x = 48 , .y = 32 };

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
        origin.x + dimensions.x < -CAMERA_CLIPPING_RADIUS || // norm space
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
    current_world_state.collision_boxes[current_world_state.collision_box_count].origin     = origin;
    current_world_state.collision_boxes[current_world_state.collision_box_count].dimensions = dimensions;
    current_world_state.collision_box_count++;
}

void gameInitialise(void) 
{
    current_world_state.player_coords   = (NormalizedCoords){ 0.0f, 0.0f };
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

        if (tick_input.w_press) next_player_coords.y += yPixelsToNorm(1);
        if (tick_input.a_press) next_player_coords.x -= xPixelsToNorm(1);
        if (tick_input.s_press) next_player_coords.y -= yPixelsToNorm(1);
        if (tick_input.d_press) next_player_coords.x += xPixelsToNorm(1);

		if (tick_input.z_press) current_world_state.player_velocity.y = yPixelsToNorm(2);

		// draw floor
        for (int16 i = 0; i < 7; i++)
        {
            IntCoords floor_coords_int = { i * floor_tile_dim_int.x - 96, -32 };
            NormalizedCoords floor_coords_norm = nearestPixelFloorToNorm(floor_coords_int, DEFAULT_SCALE);
            NormalizedCoords floor_tile_dim_norm = pixelsToNorm(floor_tile_dim_int, DEFAULT_SCALE);
            drawSprite(floor_tile_path, floor_coords_norm, floor_tile_dim_norm); // TODO(spike): change drawSprite and createCollisionBox to take in the same prenormalized variable
                                                                                           // for dimensions. is this not just the pixelsToNorm function?
            // createCollisionBox(floor_coords_norm, floor_tile_dim_norm);
        }

		// gravity
        current_world_state.player_velocity.y -= yPixelsToNorm(0.1f);

		// apply velocity
        next_player_coords.x += current_world_state.player_velocity.x;
        next_player_coords.y += current_world_state.player_velocity.y;

        // collision detection
        // loop over all instances of all textures in textures_to_load
        /*
        for (int16 texture_i = 0; texture_i < 128; texture_i++)
        {
            if (textures_to_load[texture_i].path == 0) break;

            for (int16 instance_i = 0; instance_i < textures_to_load[texture_i].instance_count; instance_i++)
            {
				if (next_player_coords.y <)
            }
        }
		if (next_player_coords.y )
		*/

		current_world_state.player_coords = next_player_coords;

		// draw player
        drawSprite(player_path, nearestPixelFloor(current_world_state.player_coords, DEFAULT_SCALE), nearestPixelFloorToNorm(player_dim_int, DEFAULT_SCALE));

        rendererSubmitFrame(textures_to_load);
        memset(textures_to_load, 0, sizeof(textures_to_load));
        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
