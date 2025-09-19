#pragma once

#include "types.h"
#include "worldstate_structs.h"

void gameInitialise(void);
void gameFrame(double, TickInput);
void rendererSubmitFrame(WorldState* previous_world_state, WorldState* current_world_state, double interpolation_fraction);
