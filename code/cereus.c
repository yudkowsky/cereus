#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"

#include <string.h> // TODO(spike): temporary, for memset
// #include <math.h> // TODO(spike): also temporary, for floor
// #include <stdio.h> // TODO(spike): "temporary", for fopen 
// #include <windows.h> // TODO(spike): ok, actually temporary this time, for outputdebugstring

#define local_persist static
#define global_variable static
#define internal static

double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0.0;

AssetToLoad assets_to_load[256] = {0};
int32 assent_to_load_count = 0;

Camera camera = { {0, 0, 4}, {0, 0, 0, 1} };

char* box_path = "data/sprites/box.png";
Int2 box_dim = { 16, 16 };

void gameInitialise(void) 
{	
}

void gameFrame(double delta_time, TickInput tick_input)
{	
   	// clamp for stalls or breakpoints
	if (delta_time > 0.1) delta_time = 0.1;

	accumulator += delta_time;

    while (accumulator >= PHYSICS_INCREMENT)
    {
        if (tick_input.w_press) camera.coords.z -= 0.05f;
        if (tick_input.a_press) camera.coords.x -= 0.05f;
        if (tick_input.s_press) camera.coords.z += 0.05f;
        if (tick_input.d_press) camera.coords.x += 0.05f;

        Vec3 box_coords   = { 0.0f, 0.0f, 0.0f };
        Vec3 box_scale    = { 1.0f, 1.0f, 1.0f };
        Vec4 box_rotation = { 1.0f, 0.2f, 0.5f, 0.5f };

		assets_to_load[0].path = box_path;
        assets_to_load[0].type = CUBE_3D;
        assets_to_load[0].coords[0] = box_coords;
        assets_to_load[0].scale[0] = box_scale;
        assets_to_load[0].rotation[0] = box_rotation;
        assets_to_load[0].asset_count = 1;

        rendererSubmitFrame(assets_to_load, camera);
        memset(assets_to_load, 0, sizeof(assets_to_load));

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
