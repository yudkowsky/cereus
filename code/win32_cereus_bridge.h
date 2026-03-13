#pragma once
#include "types.h"
#include "worldstate_structs.h"

void gameInitialize(char*, DisplayInfo);
void gameFrame(double, TickInput);
void gameRedraw(DisplayInfo);
void vulkanSubmitFrame(DrawCommand*, int32 draw_command_count, Camera, ShaderMode);
void vulkanDraw();
