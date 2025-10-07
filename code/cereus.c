#include "win32_cereus_bridge.h"
#include "worldstate_structs.h"
// #include <string.h> // TODO(spike): temporary, for memset
// #include <math.h> // TODO(spike): also temporary, for floor
// #include <stdio.h> // TODO(spike): "temporary", for fopen 
// #include <windows.h> // TODO(spike): ok, actually temporary this time, for outputdebugstring

#define local_persist static
#define global_variable static
#define internal static

double PHYSICS_INCREMENT = 1.0/60.0;
double accumulator = 0.0;

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
        AssetToLoad assets_to_load[256] = {0};
		rendererSubmitFrame(assets_to_load);

        accumulator -= PHYSICS_INCREMENT;
    }

    rendererDraw();
}
