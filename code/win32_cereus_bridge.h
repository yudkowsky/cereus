#pragma once

#include "types.h"
#include "worldstate_structs.h"

void gameInitialise(void);
void gameFrame(double, TickInput);
void rendererSubmitFrame(AssetToLoad assets_to_load[128]);
void rendererDraw();
