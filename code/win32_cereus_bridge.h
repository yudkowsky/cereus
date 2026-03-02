#pragma once
#include "types.h"
#include "worldstate_structs.h"

void gameInitialize(char*, DisplayInfo);
void gameFrame(double, TickInput);
void vulkanSubmitFrame(DrawCommand*, int32, Camera);
void vulkanDraw();
