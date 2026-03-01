#pragma once

#include "types.h"
#include "worldstate_structs.h"

void gameInitialize(char*);
void gameFrame(double, TickInput);
void rendererSubmitFrame(DrawCommand*, int32, Camera);
void rendererDraw();
