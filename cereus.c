#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // TODO(spike): temporary, for memset

#define local_persist static
#define global_variable static
#define internal static

const double PHYSICS_INCREMENT = 1.0/60.0;
const NormalizedCoords NORMALIZED_CONVERSION = { 2.0 / 1920.0, 2.0 / 1080.0 };
const NormalizedCoords DEFAULT_SCALE = {6, 6};

WorldState current_world_state = {0};
double accumulator = 0.0;

char* grid_tile_path = "data/sprites/grid.png";

TextureToLoad textures_to_load[128] = {0};
char* loaded_textures[128] = {0};

// modifies textures_to_load. scale input is pixels/pixel, output is multiplied 2 / screen resolution (fits in [-1, 1])
void drawSprite(char* texture_path, NormalizedCoords origin, NormalizedCoords scale)
{
    int32 texture_location = -1;
    // load has not been attempted here; find next free in textures_to_load
    for (uint32 texture_index = 0; texture_index < 128; texture_index++)
    {
        if (loaded_textures[texture_index] == texture_path)
        {
            // already loaded
			textures_to_load[texture_index].path = texture_path;
            texture_location = texture_index;
            break;
        }
        if (textures_to_load[texture_index].path == 0)
        {
            // end of list - queue the path to load, 
            textures_to_load[texture_index].path = texture_path;
            loaded_textures[texture_index] = texture_path;
            texture_location = texture_index;
            break;
        }
    }
    // adjust scale to [-1, 1] expected by graphics api
    scale = (NormalizedCoords){ scale.x * NORMALIZED_CONVERSION.x, scale.y * NORMALIZED_CONVERSION.y };

    // use instance count to go to next free slot here
	textures_to_load[texture_location].origin[textures_to_load[texture_location].instance_count] = origin;
    textures_to_load[texture_location].scale[textures_to_load[texture_location].instance_count] = scale;
    textures_to_load[texture_location].instance_count++;
}

void gameInitialise(void) {}

NormalizedCoords dummy_coords = { -0.3, 0 };
NormalizedCoords dummy_coords_2 = { 0, 0.2 };

void gameFrame(double delta_time, TickInput tick_input)
{	
   	// clamp for stalls or breakpoints
	if (delta_time > 0.1) delta_time = 0.1;

	accumulator += delta_time;
    while (accumulator >= PHYSICS_INCREMENT)
    {
        drawSprite(grid_tile_path, dummy_coords, DEFAULT_SCALE);
        drawSprite(grid_tile_path, dummy_coords_2, DEFAULT_SCALE);
        accumulator -= PHYSICS_INCREMENT;
        rendererSubmitFrame(textures_to_load, loaded_textures);
        memset(textures_to_load, 0, sizeof(textures_to_load)); // clear textures_to_load
    }
    rendererDraw();
}
