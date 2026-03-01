#pragma once

#include "types.h"
#include "worldstate_structs.h"

void gameInitialize(char* command_line);
void gameFrame(double, TickInput);
void rendererSubmitFrame(DrawCommand* draw_commands, int32 draw_command_count, Camera camera);
void rendererDraw();
