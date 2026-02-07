#pragma once

#include "types.h"
#include "worldstate_structs.h"

void gameInitialize(char* command_line);
void gameFrame(double, TickInput);
void rendererSubmitFrame(AssetToLoad assets_to_load[256], Camera camera);
void rendererDraw();
