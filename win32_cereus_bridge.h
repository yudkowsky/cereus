#pragma once

#include "types.h"
#include "worldstate_structs.h"

void gameInitialise(void);
void gameFrame(double, TickInput);
void rendererSubmitFrame(WorldState current_world_state, TextureToLoad textures[128]);
void rendererDraw();
