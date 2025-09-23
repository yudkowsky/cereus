#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // TODO(spike): temporary, for memset

#define local_persist static
#define global_variable static
#define internal static

const double PHYSICS_INCREMENT = 1.0/60.0;

// const IntCoords SCREEN_RESOLUTION = { 1920, 1080 };
// const double NORMALIZED_X_CONVERSION = 2.0 / SCREEN_RESOLUTION.x;
// const double NORMALIZED_Y_CONVERSION = 2.0 / SCREEN_RESOLUTION.y;

WorldState current_world_state = {0};
double accumulator = 0.0;

char* grid_tile_path = "data/sprites/grid.png";

TextureToLoad textures_to_load[128] = {0};

// modifies textures_to_load
void drawSprite(char* texture_path, NormalizedCoords origin)
{
    int32 texture_location = -1;
    // load has not been attempted here; find next free in textures_to_load
    for (uint32 texture_index = 0; texture_index < 128; texture_index++)
    {
        char* current_path = textures_to_load[texture_index].path;
        if (current_path == texture_path)
        {
            // already loaded - continue to loading coordinates.
            texture_location = texture_index;
            break;
        }
        if (current_path == 0)
        {
            // end of list - queue the path to load, 
            textures_to_load[texture_index].path = texture_path;
            texture_location = texture_index;
            break;
        }
    }
	textures_to_load[texture_location].origin[textures_to_load[texture_location].instance_count].x = origin.x;
	textures_to_load[texture_location].origin[textures_to_load[texture_location].instance_count].y = origin.y;
    textures_to_load[texture_location].instance_count++;
}

void gameInitialise(void)
{
    current_world_state.pixel_size  = 6;
}

NormalizedCoords dummy_coords = { 0.0, 0.0 };

void gameFrame(double delta_time, TickInput tick_input)
{	
   	// clamp for stalls or breakpoints
	if (delta_time > 0.1) delta_time = 0.1;

	accumulator += delta_time;
    while (accumulator >= PHYSICS_INCREMENT)
    {
        drawSprite(grid_tile_path, dummy_coords);
        accumulator -= PHYSICS_INCREMENT;
        rendererSubmitFrame(current_world_state, textures_to_load);
        memset(textures_to_load, 0, sizeof(textures_to_load)); // clear textures_to_load
    }
    rendererDraw();
}
