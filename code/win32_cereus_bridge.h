#pragma once
#include "types.h"
#include "worldstate_structs.h"

void gameInitialize(char* level_name, DisplayInfo);
void gameFrame(double delta_time, TickInput);
void gameRedraw(DisplayInfo);
void vulkanSubmitFrame(DrawCommand*, int32 draw_command_count, float global_time, Camera, ShaderMode);
void vulkanDraw();
