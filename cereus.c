#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
#include <string.h> // TODO(spike): temporary, for memset
#include <math.h> //TODO(spike): also temporary, for floor

#define local_persist static
#define global_variable static
#define internal static

double PHYSICS_INCREMENT = 1.0/60.0;
NormalizedCoords NORMALIZED_CONVERSION = { 2.0f / 1920.0f, 2.0f / 1080.0f };
IntCoords DEFAULT_SCALE = {10, 10};
float CAMERA_MOVEMENT_SPEED = 0.01f; // arbitrary movement per frame; temporary anyway, will want to use variable for this

WorldState current_world_state = {0};
double accumulator = 0.0;

char* grid_tile_path = "data/sprites/grid.png";
char* player_path = "data/sprites/player.png";

TextureToLoad textures_to_load[128] = {0};
char* loaded_textures[128] = {0};

float xPixelsToNorm(int32 pixels)
{
    return pixels * NORMALIZED_CONVERSION.x * DEFAULT_SCALE.x;
}

float yPixelsToNorm(int32 pixels)
{
    return pixels * NORMALIZED_CONVERSION.y * DEFAULT_SCALE.y;
}

NormalizedCoords nearestPixelFloor(NormalizedCoords coords_input)
{
	NormalizedCoords corrected_coords = {0};
    corrected_coords.x = floorf(coords_input.x / (NORMALIZED_CONVERSION.x * DEFAULT_SCALE.x) + 0.5f) * NORMALIZED_CONVERSION.x * DEFAULT_SCALE.x;
    corrected_coords.y = floorf(coords_input.y / (NORMALIZED_CONVERSION.y * DEFAULT_SCALE.y) + 0.5f) * NORMALIZED_CONVERSION.y * DEFAULT_SCALE.y;
    return corrected_coords;
}

// modifies textures_to_load. scale input is pixels/pixel, output is multiplied 2 / screen resolution (fits in [-1, 1])
void drawSprite(char* texture_path, NormalizedCoords origin, IntCoords scale)
{
    int32 texture_location = -1;
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
    NormalizedCoords normalized_scale = (NormalizedCoords){ scale.x * NORMALIZED_CONVERSION.x, scale.y * NORMALIZED_CONVERSION.y };

	// adjust origin based on camera location
	origin.x += current_world_state.camera_coords.x;
	origin.y += current_world_state.camera_coords.y;
    		
    // use instance count to go to next free slot here
	textures_to_load[texture_location].origin[textures_to_load[texture_location].instance_count] = origin;
    textures_to_load[texture_location].scale[textures_to_load[texture_location].instance_count] = normalized_scale;
    textures_to_load[texture_location].instance_count++;
}

void gameInitialise(void) 
{
    current_world_state.player_coords = (NormalizedCoords){ 0.0f, 0.0f };
	current_world_state.camera_coords = (NormalizedCoords){ 0.0f, 0.0f };
}

void gameFrame(double delta_time, TickInput tick_input)
{	
   	// clamp for stalls or breakpoints
	if (delta_time > 0.1) delta_time = 0.1;

	accumulator += delta_time;

    while (accumulator >= PHYSICS_INCREMENT)
    {
        // just doesn't work when i put it in here, need to check why
    }

    if (tick_input.w_press) current_world_state.player_coords.y += yPixelsToNorm(1);
    if (tick_input.a_press) current_world_state.player_coords.x -= xPixelsToNorm(1);
    if (tick_input.s_press) current_world_state.player_coords.y -= yPixelsToNorm(1);
    if (tick_input.d_press) current_world_state.player_coords.x += xPixelsToNorm(1);

    if (tick_input.i_press) current_world_state.camera_coords.y += CAMERA_MOVEMENT_SPEED;
    if (tick_input.j_press) current_world_state.camera_coords.x -= CAMERA_MOVEMENT_SPEED;
    if (tick_input.k_press) current_world_state.camera_coords.y -= CAMERA_MOVEMENT_SPEED;
    if (tick_input.l_press) current_world_state.camera_coords.x += CAMERA_MOVEMENT_SPEED;

    for (int i = 0; i < 5; i++)
    {
        for (int j = 0; j < 6; j++)
        {
            NormalizedCoords tile_coords = { xPixelsToNorm(j * 16 - 48), yPixelsToNorm(i * 16 - 48) };
            drawSprite(grid_tile_path, nearestPixelFloor(tile_coords), DEFAULT_SCALE);
        }
    }

    drawSprite(player_path, nearestPixelFloor(current_world_state.player_coords), DEFAULT_SCALE);

    rendererSubmitFrame(textures_to_load);
    memset(textures_to_load, 0, sizeof(textures_to_load)); // clear textures_to_load

    accumulator -= PHYSICS_INCREMENT;
    rendererDraw();
}
